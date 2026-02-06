#include "reactor/epoll_utility.hpp"
#include <sys/epoll.h>
#include <fcntl.h>

std::expected <void, error_code> epoll_utility::set_nonblocking(int fd){
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

std::expected <void, error_code> epoll_utility::add_fd(int epfd, int fd, uint32_t interest){
    epoll_event ev{};
    ev.events = interest;
    ev.data.fd = fd;
    int ec = ::epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev);
    if(ec == -1){
        int en = errno;
        return std::unexpected(error_code::from_errno(en));
    }
    return {};
}

std::expected <void, error_code> epoll_utility::del_fd(int epfd, int fd){
    int ec = ::epoll_ctl(epfd, EPOLL_CTL_DEL, fd, nullptr);
    if(ec == -1){
        int ec = errno;
        return std::unexpected(error_code::from_errno(ec));
    }
    return {};
}

std::expected <void, error_code> epoll_utility::update_interest(int epfd, int fd, socket_info& si, uint32_t interest){
    si.interest = interest;
    epoll_event ev{};
    ev.events = interest;
    ev.data.fd = fd;
    int ec = ::epoll_ctl(epfd, EPOLL_CTL_MOD, fd, &ev);
    if(ec == -1){
        int ec = errno;
        return std::unexpected(error_code::from_errno(ec));
    }
    return {};
}