#include "server/epoll_acceptor.hpp"
#include <sys/epoll.h>
#include <cerrno>

epoll_acceptor::epoll_acceptor(epoll_listener& listener, epoll_registry& registry) : 
    listener(listener), registry(registry){};

void epoll_acceptor::handle_accept(){
    auto client_fd_exp = make_client_fd();
    if(!client_fd_exp){
        handle_error("acceptor/handle_accept/make_client_fd failed", client_fd_exp);
        return;
    }

    registry.request_register(std::move(*client_fd_exp), EPOLLIN | EPOLLRDHUP);
}

std::expected <unique_fd, error_code> epoll_acceptor::make_client_fd(){
    while(true){
        int client_fd = ::accept(listener.get_fd(), nullptr, nullptr);
        if(client_fd != -1) return unique_fd(client_fd);

        int ec = errno;
        if(ec == EINTR) continue;
        return std::unexpected(error_code::from_errno(ec));
    }
}

std::expected <void, error_code> epoll_acceptor::run(const std::stop_token& stop_token){
    std::stop_callback on_stop(stop_token, [this](){ listener.request_wakeup(); });

    while(!stop_token.stop_requested()){
        int event_sz = ::epoll_wait(listener.get_epfd(), events.data(), events.size(), -1);
        if(event_sz == -1){
            int ec = errno;
            if(errno == EINTR) continue;
            return std::unexpected(error_code::from_errno(ec));
        }

        for(int i = 0;i < event_sz;++i){
            int fd = events[i].data.fd;
            uint32_t event = events[i].events;

            if(fd == listener.get_wake_fd()){
                listener.consume_wakeup();
                continue;
            }

            if(event & (EPOLLERR | EPOLLHUP)){
                int ec = 0;
                socklen_t len = sizeof(ec);
                if(getsockopt(fd, SOL_SOCKET, SO_ERROR, &ec, &len) == -1) ec = errno;
                return std::unexpected(error_code::from_errno(ec));
            }

            if(fd != listener.get_fd()) continue;
            handle_accept();
        }
    }

    return {};
}
