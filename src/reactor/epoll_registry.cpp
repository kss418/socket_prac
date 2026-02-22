#include "reactor/epoll_registry.hpp"
#include "core/logger.hpp"
#include "net/tls_context.hpp"
#include "reactor/epoll_utility.hpp"
#include <cerrno>
#include <sys/epoll.h>

epoll_registry::epoll_registry(epoll_wakeup wakeup, tls_context& tls_ctx) :
    epoll_wakeup(std::move(wakeup)), tls_ctx(tls_ctx){}

std::expected <int, error_code> epoll_registry::register_fd(unique_fd client_fd, uint32_t interest){
    int fd = client_fd.get();
    if(fd == -1){
        logger::log_error("fd error", "epoll_registry::register_fd()", error_code::from_errno(EINVAL));
        return std::unexpected(error_code::from_errno(EINVAL));
    }

    if(infos.contains(fd)){
        logger::log_warn("fd existed error", "epoll_registry::register_fd()", error_code::from_errno(EEXIST));
        return std::unexpected(error_code::from_errno(EEXIST));
    }

    auto nonblocking_exp = epoll_utility::set_nonblocking(fd);
    if(!nonblocking_exp){
        logger::log_error("set_nonblocking failed", "epoll_registry::register_fd()", nonblocking_exp);
        return std::unexpected(nonblocking_exp.error());
    }

    auto ep_exp = make_peer_endpoint(fd);
    if(!ep_exp){
        logger::log_error("make_peer_endpoint failed", "epoll_registry::register_fd()", ep_exp);
        return std::unexpected(ep_exp.error());
    }

    auto init_str_exp = ep_exp->init_string();
    if(!init_str_exp){
        logger::log_error("init_string failed", "epoll_registry::register_fd()", init_str_exp);
        return std::unexpected(init_str_exp.error());
    }

    auto add_ep_exp = epoll_utility::add_fd(epfd.get(), fd, interest);
    if(!add_ep_exp){
        logger::log_error("add_fd failed", "epoll_registry::register_fd()", add_ep_exp);
        return std::unexpected(add_ep_exp.error());
    }

    auto tls_exp = tls_session::create_server(tls_ctx, fd);
    if(!tls_exp){
        logger::log_error("tls_session create failed", "epoll_registry::register_fd()", tls_exp);
        return std::unexpected(tls_exp.error());
    }
    
    auto [it, inserted] = infos.emplace(
        fd,
        socket_info{
            .tls = std::move(*tls_exp),
            .interest = interest,
            .ufd = std::move(client_fd),
            .ep = std::move(*ep_exp)
        }
    );

    std::cout << to_string(it->second.ep) << " is connected" << "\n";
    return fd;
}

std::expected <void, error_code> epoll_registry::unregister_fd(int fd){
    if(fd == -1){
        logger::log_error("fd error", "epoll_registry::unregister_fd()", error_code::from_errno(EINVAL));
        return std::unexpected(error_code::from_errno(EINVAL));
    }

    auto it = infos.find(fd);
    if(it == infos.end()) return {};

    auto del_ep_exp = epoll_utility::del_fd(epfd.get(), fd);
    if(!del_ep_exp){
        const error_code& ec = del_ep_exp.error();
        bool ignorable = ec.domain == error_domain::errno_domain
            && (ec.code == ENOENT || ec.code == EBADF);
        if(!ignorable) logger::log_error("del_fd failed", "epoll_registry::unregister_fd()", del_ep_exp);
    }

    infos.erase(it);
    return {};
}

std::expected <void, error_code> epoll_registry::sync_interest(int fd, socket_info& si){
    auto mod_exp = epoll_utility::update_interest(epfd.get(), fd, si, si.interest);
    if(!mod_exp){
        unregister_fd(fd);
        return std::unexpected(mod_exp.error());
    }
    return {};
}

std::expected <void, error_code> epoll_registry::append_send(
    int fd,
    socket_info& si,
    const command_codec::command& cmd
){
    if(!si.send.append(cmd)) return {};

    si.interest |= EPOLLOUT;
    auto sync_exp = sync_interest(fd, si);
    if(!sync_exp){
        logger::log_warn("sync_interest failed", "epoll_registry::append_send()", sync_exp);
        return std::unexpected(sync_exp.error());
    }

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

void epoll_registry::request_send(int fd, command_codec::command cmd){ 
    {
        std::lock_guard<std::mutex> lock(cmd_mtx);
        cmd_q.emplace(send_one_command{fd, std::move(cmd)});
    }
    request_wakeup();
}

void epoll_registry::request_broadcast(int send_fd, command_codec::command cmd){ 
    {
        std::lock_guard<std::mutex> lock(cmd_mtx);
        cmd_q.emplace(broadcast_command{send_fd, std::move(cmd)});
    }
    request_wakeup();
}

void epoll_registry::request_change_nickname(int send_fd, std::string nick){ 
    {
        std::lock_guard<std::mutex> lock(cmd_mtx);
        cmd_q.emplace(change_nickname_command{send_fd, std::move(nick)});
    }
    request_wakeup();
}

void epoll_registry::handle_command(register_command&& cmd){
    auto reg_exp = register_fd(std::move(cmd.fd), cmd.interest);
    if(!reg_exp) logger::log_warn("register_command failed", "epoll_registry::handle_command()", reg_exp);
}

void epoll_registry::handle_command(const unregister_command& cmd){
    auto unreg_exp = unregister_fd(cmd.fd);
    if(!unreg_exp) logger::log_warn("unregister_command failed", "epoll_registry::handle_command()", unreg_exp);
}

void epoll_registry::handle_command(send_one_command&& cmd){
    auto it = infos.find(cmd.fd);
    if(it == infos.end()) return;

    auto& si = it->second;
    auto append_exp = append_send(cmd.fd, si, cmd.cmd);
    if(!append_exp) return;
}

void epoll_registry::handle_command(broadcast_command&& cmd){
    if(const auto* response = std::get_if<command_codec::cmd_response>(&cmd.cmd)){
        std::string nickname = "guest";
        auto sender_it = infos.find(cmd.fd);
        if(sender_it != infos.end() && !sender_it->second.nickname.empty()){
            nickname = sender_it->second.nickname;
        }

        command_codec::command named_msg =
            command_codec::cmd_response{nickname + ": " + response->text};
        for(auto& [fd, si] : infos){
            auto append_exp = append_send(fd, si, named_msg);
            if(!append_exp) continue;
        }
        return;
    }

    for(auto& [fd, si] : infos){
        auto append_exp = append_send(fd, si, cmd.cmd);
        if(!append_exp) continue;
    }
}

void epoll_registry::handle_command(change_nickname_command&& cmd){
    auto it = infos.find(cmd.fd);
    if(it == infos.end()) return;

    it->second.nickname = std::move(cmd.nick);
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
        std::visit([this](auto&& c){ handle_command(std::move(c)); }, std::move(cmd));
    }
}

epoll_registry::socket_info_it epoll_registry::find(int fd){ return infos.find(fd); }
epoll_registry::socket_info_it epoll_registry::end(){ return infos.end(); }
