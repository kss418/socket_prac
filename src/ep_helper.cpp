#include "../include/ep_helper.hpp"
#include <sys/epoll.h>
#include <netdb.h>
#include <fcntl.h>

int set_nonblocking(int fd){
    int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags == -1) return -1;
    return ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

int ep_add(int epfd, int fd, uint32_t events){
    epoll_event ev{};
    ev.events = events;
    ev.data.fd = fd;
    return ::epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev);
}

int ep_mod(int epfd, int fd, uint32_t events){
    epoll_event ev{};
    ev.events = events;
    ev.data.fd = fd;
    return ::epoll_ctl(epfd, EPOLL_CTL_MOD, fd, &ev);
}

int ep_del(int epfd, int fd){
    return ::epoll_ctl(epfd, EPOLL_CTL_DEL, fd, nullptr);
}