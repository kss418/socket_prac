#include "reactor/epoll_registry.hpp"
#include "reactor/epoll_utility.hpp"
#include <cerrno>
#include <sys/epoll.h>

epoll_registry::epoll_registry(epoll_registry&& other) noexcept{
    std::scoped_lock lock(other.cmd_mtx);
    epfd = std::move(other.epfd);
    wake_fd = std::move(other.wake_fd);
    cmd_q = std::move(other.cmd_q);
    infos = std::move(other.infos);
}

epoll_registry& epoll_registry::operator=(epoll_registry&& other) noexcept{
    if(this == &other) return *this;
    std::scoped_lock lock(cmd_mtx, other.cmd_mtx);
    epoll_wakeup::operator=(std::move(other));
    cmd_q = std::move(other.cmd_q);
    infos = std::move(other.infos);
    return *this;
}

std::expected <int, error_code> epoll_registry::register_fd(unique_fd client_fd, uint32_t interest){
    int fd = client_fd.get();
    if(fd == -1){
        handle_error("register_fd/fd error", error_code::from_errno(EINVAL));
        return std::unexpected(error_code::from_errno(EINVAL));
    }

    if(infos.contains(fd)){
        handle_error("register_fd/fd existed error", error_code::from_errno(EEXIST));
        return std::unexpected(error_code::from_errno(EEXIST));
    }

    auto nonblocking_exp = epoll_utility::set_nonblocking(fd);
    if(!nonblocking_exp){
        handle_error("register_fd/set_nonblocking failed", nonblocking_exp);
        return std::unexpected(nonblocking_exp.error());
    }

    auto ep_exp = make_peer_endpoint(fd);
    if(!ep_exp){
        handle_error("register_fd/make_peer_endpoint failed", ep_exp);
        return std::unexpected(ep_exp.error());
    }

    auto init_str_exp = ep_exp->init_string();
    if(!init_str_exp){
        handle_error("register_fd/init_string failed", init_str_exp);
        return std::unexpected(init_str_exp.error());
    }

    auto add_ep_exp = epoll_utility::add_fd(epfd.get(), fd, interest);
    if(!add_ep_exp){
        handle_error("register_fd/add_fd failed", add_ep_exp);
        return std::unexpected(add_ep_exp.error());
    }
    
    socket_info si{};
    si.ufd = std::move(client_fd);
    si.ep = std::move(*ep_exp);
    si.interest = interest;

    infos.emplace(fd, std::move(si));
    std::cout << to_string(si.ep) << " is connected" << "\n";
    return fd;
}

std::expected <void, error_code> epoll_registry::unregister_fd(int fd){
    if(fd == -1){
        handle_error("unregister_fd/fd error", error_code::from_errno(EINVAL));
        return std::unexpected(error_code::from_errno(EINVAL));
    }

    auto del_ep_exp = epoll_utility::del_fd(epfd.get(), fd);
    if(!del_ep_exp) handle_error("unregister_fd/del_ep failed", del_ep_exp);

    if(infos.contains(fd)) infos.erase(fd);
    return {};
}

void epoll_registry::request_register(unique_fd fd, uint32_t interest){ 
    {
        std::lock_guard<std::mutex> lock(cmd_mtx);
        cmd_q.emplace(register_command{std::move(fd), interest});
    }
    request_wakeup();
}

void epoll_registry::request_unregister(int fd){ 
    {
        std::lock_guard<std::mutex> lock(cmd_mtx);
        cmd_q.emplace(unregister_command{fd});
    }
    request_wakeup();
}

void epoll_registry::request_worker(int fd, command_codec::command cmd, bool close){ 
    {
        std::lock_guard<std::mutex> lock(cmd_mtx);
        cmd_q.emplace(worker_result_command{fd, std::move(cmd), close});
    }
    request_wakeup();
}

void epoll_registry::work(){
    consume_wakeup();

    std::queue<command> pending_cmd;
    {
        std::lock_guard<std::mutex> lock(cmd_mtx);
        std::swap(pending_cmd, cmd_q);
    }

    while(!pending_cmd.empty()){
        command cmd = std::move(pending_cmd.front());
        pending_cmd.pop();

        if(std::holds_alternative<register_command>(cmd)){
            auto reg_cmd = std::get<register_command>(std::move(cmd));
            auto reg_exp = register_fd(std::move(reg_cmd.fd), reg_cmd.interest);
            if(!reg_exp) handle_error("work/register_fd failed", reg_exp);
        }
        else if(std::holds_alternative<unregister_command>(cmd)){
            int fd = std::get<unregister_command>(cmd).fd;
            auto unreg_exp = unregister_fd(fd);
            if(!unreg_exp) handle_error("epoll_registry/unregister_fd failed", unreg_exp);
        }
        else if(std::holds_alternative<worker_result_command>(cmd)){
            auto worker_cmd = std::get<worker_result_command>(std::move(cmd));
            int fd = worker_cmd.fd;
            auto c = std::move(worker_cmd.cmd);
            bool close = worker_cmd.close;

            auto it = infos.find(fd);
            if(it == infos.end()) continue;
            auto& si = it->second;

            bool was_pending = si.send.has_pending();
            si.send.append(c);
            if(!was_pending && si.send.has_pending()){
                si.interest |= EPOLLOUT;
                auto mod_exp = epoll_utility::update_interest(epfd.get(), fd, si, si.interest);
                if(!mod_exp){
                    handle_error("work/update_interest failed", mod_exp);
                    unregister_fd(fd);
                    continue;
                }
            }

            if(close) unregister_fd(fd);
        }
    }
}

epoll_registry::socket_info_it epoll_registry::find(int fd){ return infos.find(fd); }
epoll_registry::socket_info_it epoll_registry::end(){ return infos.end(); }
