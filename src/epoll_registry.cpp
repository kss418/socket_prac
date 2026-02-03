#include "../include/epoll_registry.hpp"
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

    auto nonblocking_exp = set_nonblocking(fd);
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

    auto add_ep_exp = add_fd(fd, interest);
    if(!add_ep_exp){
        handle_error("register_client/add_fd failed", add_ep_exp);
        return std::unexpected(add_ep_exp.error());
    }
    
    socket_info si{};
    si.ufd = std::move(client_fd);
    si.ep = std::move(*ep_exp);
    si.interest = interest;

    infos.emplace(fd, std::move(si));
    return fd;
}

std::expected <int, error_code> epoll_registry::register_listener(int fd){
    if(fd == -1){
        handle_error("register_listener/fd error", error_code::from_errno(EINVAL));
        return std::unexpected(error_code::from_errno(EINVAL));
    }

    auto nonblocking_exp = set_nonblocking(fd);
    if(!nonblocking_exp){
        handle_error("register_listener/set_nonblocking failed", nonblocking_exp);
        return std::unexpected(nonblocking_exp.error());
    }

    auto add_ep_exp = add_fd(fd, EPOLLIN);
    if(!add_ep_exp){
        handle_error("register_client/add_fd failed", add_ep_exp);
        return std::unexpected(add_ep_exp.error());
    }
    return {};
}

std::expected <void, error_code> epoll_registry::unregister(int fd){
     if(fd == -1){
        handle_error("unregister/fd error", error_code::from_errno(EINVAL));
        return std::unexpected(error_code::from_errno(EINVAL));
    }

    auto del_ep_exp = del_fd(fd);
    if(!del_ep_exp) handle_error("unregister/del_ep failed", del_ep_exp);

    if(infos.contains(fd)) infos.erase(fd);
    return {};
}
    
std::expected <void, error_code> epoll_registry::update_interest(int fd, uint32_t interest){
    epoll_event ev{};
    ev.events = interest;
    ev.data.fd = fd;
    int ec = ::epoll_ctl(epfd.get(), EPOLL_CTL_MOD, fd, &ev);
    if(ec == -1){
        int ec = errno;
        return std::unexpected(error_code::from_errno(ec));
    }
    return {};
}

std::expected <void, error_code> epoll_registry::set_nonblocking(int fd){
    int flags = ::fcntl(fd, F_GETFL, 0);
    if(flags == -1){
        int ec = errno;
        return std::unexpected(error_code::from_errno(ec));
    }

    int ec = ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    if(ec == -1){
        int ec = errno;
        return std::unexpected(error_code::from_errno(ec));
    }
    return {};
}

std::expected <void, error_code> epoll_registry::add_fd(int fd, uint32_t interest){
    epoll_event ev{};
    ev.events = interest;
    ev.data.fd = fd;
    int ec = ::epoll_ctl(epfd.get(), EPOLL_CTL_ADD, fd, &ev);
    if(ec == -1){
        int en = errno;
        return std::unexpected(error_code::from_errno(en));
    }
    return {};
}

std::expected <void, error_code> epoll_registry::del_fd(int fd){
    int ec = ::epoll_ctl(epfd.get(), EPOLL_CTL_DEL, fd, nullptr);
    if(ec == -1){
        int ec = errno;
        return std::unexpected(error_code::from_errno(ec));
    }
    return {};
}

epoll_registry::socket_info_it epoll_registry::find(int fd){ return infos.find(fd); }
epoll_registry::socket_info_it epoll_registry::end(){ return infos.end(); }
int epoll_registry::get_epfd() const{ return epfd.get(); }