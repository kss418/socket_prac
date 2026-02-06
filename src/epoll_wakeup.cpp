#include "../include/epoll_wakeup.hpp"
#include <cerrno>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <unistd.h>

epoll_wakeup::epoll_wakeup(unique_fd epfd, unique_fd wake_fd) :
    epfd(std::move(epfd)), wake_fd(std::move(wake_fd)){}

std::expected<epoll_wakeup, error_code> epoll_wakeup::create(){
    unique_fd epfd(::epoll_create1(EPOLL_CLOEXEC));
    if(!epfd){
        int ec = errno;
        return std::unexpected(error_code::from_errno(ec));
    }

    unique_fd wake_fd(::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC));
    if(!wake_fd){
        int ec = errno;
        return std::unexpected(error_code::from_errno(ec));
    }

    epoll_event ev{};
    ev.events = EPOLLIN;
    ev.data.fd = wake_fd.get();
    if(::epoll_ctl(epfd.get(), EPOLL_CTL_ADD, wake_fd.get(), &ev) == -1){
        int ec = errno;
        return std::unexpected(error_code::from_errno(ec));
    }

    return epoll_wakeup(std::move(epfd), std::move(wake_fd));
}

int epoll_wakeup::get_epfd() const{ return epfd.get(); }
int epoll_wakeup::get_wake_fd() const{ return wake_fd.get(); }

void epoll_wakeup::request_wakeup() const{
    uint64_t v = 1;
    ssize_t n = ::write(wake_fd.get(), &v, sizeof(v));
    if(n == -1){
        int ec = errno;
        if(ec == EAGAIN) return;
        handle_error("epoll_wakeup/request_wakeup failed", error_code::from_errno(ec));
    }
}

void epoll_wakeup::consume_wakeup() const{
    uint64_t v = 0;
    while(true){
        ssize_t n = ::read(wake_fd.get(), &v, sizeof(v));
        if(n == -1){
            int ec = errno;
            if(ec == EAGAIN) return;
            handle_error("epoll_wakeup/consume_wakeup failed", error_code::from_errno(ec));
            return;
        }
        if(n == 0) return;
    }
}
