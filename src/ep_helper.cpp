#include "../include/ep_helper.hpp"
#include <sys/epoll.h>
#include <fcntl.h>

std::expected <void, error_code> set_nonblocking(int fd){
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

std::expected <void, error_code> ep_add(int epfd, int fd, uint32_t events){
    epoll_event ev{};
    ev.events = events;
    ev.data.fd = fd;
    int ec = ::epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev);
    if(ec == -1){
        int en = errno;
        return std::unexpected(error_code::from_errno(en));
    }
    return {};
}

std::expected <void, error_code> ep_mod(int epfd, int fd, uint32_t events){
    epoll_event ev{};
    ev.events = events;
    ev.data.fd = fd;
    int ec = ::epoll_ctl(epfd, EPOLL_CTL_MOD, fd, &ev);
    if(ec == -1){
        int ec = errno;
        return std::unexpected(error_code::from_errno(ec));
    }
    return {};
}

std::expected <void, error_code> ep_del(int epfd, int fd){
    int ec = ::epoll_ctl(epfd, EPOLL_CTL_DEL, fd, nullptr);
    if(ec == -1){
        int ec = errno;
        return std::unexpected(error_code::from_errno(ec));
    }
    return {};
}