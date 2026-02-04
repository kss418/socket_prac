#include "../include/acceptor.hpp"
#include <sys/epoll.h>

acceptor::acceptor(unique_fd fd) : listen_fd(std::move(fd)){};

void acceptor::handle_accept(){
    auto client_fd_exp = make_client_fd();
    if(!client_fd_exp){
        handle_error("acceptor/handle_accept/make_client_fd failed", client_fd_exp);
        return;
    }

    auto rcfd_exp = ep_registry.register_client(std::move(*client_fd_exp), EPOLLIN | EPOLLRDHUP);
    if(!rcfd_exp){
        handle_error("run/handle_accept/register_client_fd failed", rcfd_exp);
        return;
    } 

    auto ep = (ep_registry.find(*rcfd_exp)->second).ep;
    std::cout << to_string(ep) << " is connected" << "\n";
}

std::expected <unique_fd, error_code> acceptor::make_client_fd(){
    while(true){
        int client_fd = ::accept(listen_fd.get(), nullptr, nullptr);
        if(client_fd != -1) return unique_fd(client_fd);

        int ec = errno;
        if(ec == EINTR) continue;
        return std::unexpected(error_code::from_errno(ec));
    }
}

std::expected <void, error_code> acceptor::run(){
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