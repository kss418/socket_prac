#include "../include/epoll_acceptor.hpp"
#include <sys/epoll.h>

epoll_acceptor::epoll_acceptor(epoll_listener& listener, epoll_registry& registry) : 
    listener(listener), registry(registry){};

void epoll_acceptor::handle_accept(){
    auto client_fd_exp = make_client_fd();
    if(!client_fd_exp){
        handle_error("acceptor/handle_accept/make_client_fd failed", client_fd_exp);
        return;
    }

    auto rcfd_exp = registry.register_client(std::move(*client_fd_exp), EPOLLIN | EPOLLRDHUP);
    if(!rcfd_exp){
        handle_error("run/handle_accept/register_client_fd failed", rcfd_exp);
        return;
    } 

    auto ep = (registry.find(*rcfd_exp)->second).ep;
    std::cout << to_string(ep) << " is connected" << "\n";
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

std::expected <void, error_code> epoll_acceptor::run(){
    while(true){
        int event_sz = ::epoll_wait(listener.get_epfd(), events.data(), events.size(), -1);
        if(event_sz == -1){
            int ec = errno;
            if(errno == EINTR) continue;
            return std::unexpected(error_code::from_errno(ec));
        }

        for(int i = 0;i < event_sz;++i){
            int fd = events[i].data.fd;
            uint32_t event = events[i].events;

            if(event & (EPOLLERR | EPOLLHUP)){
                int ec = 0;
                socklen_t len = sizeof(ec);
                if(getsockopt(fd, SOL_SOCKET, SO_ERROR, &ec, &len) == -1) ec = errno;
                return std::unexpected(error_code::from_errno(ec));
            }

            handle_accept();
        }
    }
}