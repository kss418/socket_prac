#include "../include/event_loop.hpp"
#include <cerrno>
#include <sys/socket.h>

event_loop::event_loop(epoll_registry& registry) : registry(registry){}

std::expected<void, error_code> event_loop::run(
    int listen_fd,
    const std::function<void()>& on_accept,
    const std::function<void(int, socket_info&, uint32_t)>& on_recv,
    const std::function<void(int, socket_info&)>& on_send,
    const std::function<void(int)>& on_client_error
){
    while(true){
        int event_sz = ::epoll_wait(registry.get_epfd(), events.data(), events.size(), -1);
        if(event_sz == -1){
            int ec = errno;
            if(errno == EINTR) continue;
            return std::unexpected(error_code::from_errno(ec));
        }

        for(int i = 0;i < event_sz;++i){
            int fd = events[i].data.fd;
            uint32_t event = events[i].events;

            if(event & (EPOLLERR | EPOLLHUP)){
                if(fd == listen_fd){
                    int ec = 0;
                    socklen_t len = sizeof(ec);
                    if(getsockopt(fd, SOL_SOCKET, SO_ERROR, &ec, &len) == -1) ec = errno;
                    return std::unexpected(error_code::from_errno(ec));
                }

                on_client_error(fd);
                continue;
            }

            if(fd == listen_fd){
                on_accept();
                continue;
            }

            auto it = registry.find(fd);
            if(it == registry.end()) continue;
            auto& si = it->second;
            
            if(event & (EPOLLIN | EPOLLRDHUP)) on_recv(fd, si, event);
            if(event & EPOLLOUT) on_send(fd, si);
        }
    }
}