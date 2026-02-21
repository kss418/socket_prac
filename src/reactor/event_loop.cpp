#include "reactor/event_loop.hpp"
#include <cerrno>
#include <sys/socket.h>

event_loop::event_loop(epoll_registry& registry) : registry(registry){}

std::expected<void, error_code> event_loop::run(
    const std::stop_token& stop_token,
    const std::function<bool(socket_info&, uint32_t)>& on_recv,
    const std::function<void(socket_info&)>& on_send,
    const std::function<bool(socket_info&)>& on_execute,
    const std::function<void(int, uint32_t)>& on_client_error
){
    std::stop_callback on_stop(stop_token, [this](){ registry.request_wakeup(); });

    while(!stop_token.stop_requested()){
        int event_sz = ::epoll_wait(registry.get_epfd(), events.data(), events.size(), -1);
        if(event_sz == -1){
            int ec = errno;
            if(errno == EINTR) continue;
            return std::unexpected(error_code::from_errno(ec));
        }
        registry.work();
        if(stop_token.stop_requested()) break;

        for(int i = 0;i < event_sz;++i){
            int fd = events[i].data.fd;
            uint32_t event = events[i].events;

            if(is_error_event(event)){
                on_client_error(fd, event);
                continue;
            }

            auto it = registry.find(fd);
            if(it == registry.end()) continue;
            auto& si = it->second;

            bool keep_alive = true;
            if(is_read_event(event)) keep_alive = on_recv(si, event);
            if(!keep_alive) continue;

            if(is_write_event(event)) on_send(si);
            if(is_read_event(event)){
                while(on_execute(si));
            }
        }
    }

    return {};
}

bool event_loop::is_read_event(uint32_t event){ return (event & (EPOLLIN | EPOLLRDHUP | EPOLLHUP)) != 0; }
bool event_loop::is_write_event(uint32_t event){ return (event & EPOLLOUT) != 0; }
bool event_loop::is_error_event(uint32_t event){ return (event & EPOLLERR) != 0; }
