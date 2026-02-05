#include "../include/epoll_registry.hpp"
#include "../include/epoll_utility.hpp"
#include <sys/epoll.h>
#include <unistd.h>
#include <fcntl.h>

std::expected <int, error_code> epoll_registry::register_client(unique_fd client_fd, uint32_t interest){
    int fd = client_fd.get();
    if(fd == -1){
        handle_error("register_client/fd error", error_code::from_errno(EINVAL));
        return std::unexpected(error_code::from_errno(EINVAL));
    }

    if(infos.contains(fd)){
        handle_error("register_client/fd existed error", error_code::from_errno(EEXIST));
        return std::unexpected(error_code::from_errno(EEXIST));
    }

    auto nonblocking_exp = epoll_utility::set_nonblocking(fd);
    if(!nonblocking_exp){
        handle_error("register_client/set_nonblocking failed", nonblocking_exp);
        return std::unexpected(nonblocking_exp.error());
    }

    auto ep_exp = make_peer_endpoint(fd);
    if(!ep_exp){
        handle_error("register_client/make_peer_endpoint failed", ep_exp);
        return std::unexpected(ep_exp.error());
    }

    auto init_str_exp = ep_exp->init_string();
    if(!init_str_exp){
        handle_error("register_client/init_string failed", init_str_exp);
        return std::unexpected(init_str_exp.error());
    }

    auto add_ep_exp = epoll_utility::add_fd(epfd.get(), fd, interest);
    if(!add_ep_exp){
        handle_error("register_client/add_fd failed", add_ep_exp);
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

std::expected <int, error_code> epoll_registry::register_listener(int fd){
    if(fd == -1){
        handle_error("register_listener/fd error", error_code::from_errno(EINVAL));
        return std::unexpected(error_code::from_errno(EINVAL));
    }

    auto nonblocking_exp = epoll_utility::set_nonblocking(fd);
    if(!nonblocking_exp){
        handle_error("register_listener/set_nonblocking failed", nonblocking_exp);
        return std::unexpected(nonblocking_exp.error());
    }

    auto add_ep_exp = epoll_utility::add_fd(epfd.get(), fd, EPOLLIN);
    if(!add_ep_exp){
        handle_error("register_client/add_fd failed", add_ep_exp);
        return std::unexpected(add_ep_exp.error());
    }
    return fd;
}

std::expected <void, error_code> epoll_registry::unregister(int fd){
    if(fd == -1){
        handle_error("unregister/fd error", error_code::from_errno(EINVAL));
        return std::unexpected(error_code::from_errno(EINVAL));
    }

    auto del_ep_exp = epoll_utility::del_fd(epfd.get(), fd);
    if(!del_ep_exp) handle_error("unregister/del_ep failed", del_ep_exp);

    if(infos.contains(fd)) infos.erase(fd);
    return {};
}

epoll_registry::socket_info_it epoll_registry::find(int fd){ return infos.find(fd); }
epoll_registry::socket_info_it epoll_registry::end(){ return infos.end(); }
int epoll_registry::get_epfd() const{ return epfd.get(); }