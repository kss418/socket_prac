#include "../include/epoll_registry.hpp"
#include "../include/epoll_utility.hpp"
#include <sys/epoll.h>
#include <unistd.h>
#include <fcntl.h>

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
    reg_q.push({std::move(fd), interest});
    uint64_t v = 1;
    ssize_t n = ::write(evfd.get(), &v, sizeof(v));
    if(n == -1){
        int ec = errno;
        if(ec == EAGAIN) return;
        handle_error("request_register/write failed", error_code::from_errno(ec));
    }
}

void epoll_registry::request_unregister(int fd){ 
    unreg_q.push(fd); 
    uint64_t v = 1;
    ssize_t n = ::write(evfd.get(), &v, sizeof(v));
    if(n == -1){
        int ec = errno;
        if(ec == EAGAIN) return;
        handle_error("request_unregister/write failed", error_code::from_errno(ec));
    }
}

void epoll_registry::consume_wakeup(){
    uint64_t v = 0;
    while(true){
        ssize_t n = ::read(evfd.get(), &v, sizeof(v));
        if(n == -1){
            int ec = errno;
            if(ec == EAGAIN) return;
            handle_error("consume_wakeup/read failed", error_code::from_errno(ec));
        }
        if(n == 0) return;
    }
}

void epoll_registry::work(){
    consume_wakeup();
    while(!reg_q.empty()){
        unique_fd fd = std::move(reg_q.front().first);
        uint32_t interest = reg_q.front().second;
        reg_q.pop();

        auto reg_exp = register_fd(std::move(fd), interest);
        if(!reg_exp) handle_error("work/register_fd failed", reg_exp);
    }

    while(!unreg_q.empty()){
        int fd = unreg_q.front(); unreg_q.pop();
        auto unreg_exp = unregister_fd(fd);
        if(!unreg_exp) handle_error("epoll_registry/unregister_fd failed", unreg_exp);
    }
}

epoll_registry::socket_info_it epoll_registry::find(int fd){ return infos.find(fd); }
epoll_registry::socket_info_it epoll_registry::end(){ return infos.end(); }
int epoll_registry::get_epfd() const{ return epfd.get(); }
int epoll_registry::get_evfd() const{ return evfd.get(); }