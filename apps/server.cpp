#include <iostream>
#include <cstring>
#include <unordered_map>
#include <sys/epoll.h>
#include <array>
#include "../include/unique_fd.hpp"
#include "../include/addr.hpp"
#include "../include/fd_helper.hpp"
#include "../include/session.hpp"
#include "../include/error_code.hpp"
#include "../include/io_helper.hpp"
#include "../include/ep_helper.hpp"

int main(){
    auto addr_exp = get_addr_server("8080");
    if(!addr_exp){
        std::cerr << "get_addr_server failed: " << to_string(addr_exp.error()) << "\n";
        return 1;
    }

    auto listen_fd_exp = make_listen_fd(addr_exp->get());
    if(!listen_fd_exp){
        std::cerr << "make_listen_fd failed: " << to_string(listen_fd_exp.error()) << "\n";
        return 1;
    }

    auto epfd = unique_fd{::epoll_create1(EPOLL_CLOEXEC)};
    if(!epfd){
        int ec = errno;
        std::cerr << "epoll_create1 failed: " << std::strerror(ec) << "\n";
        return 1;
    }

    std::unordered_map <int, socket_info> socket_infos;
    int listen_fd = listen_fd_exp->get();
    auto rlfd_exp = register_listen_fd(epfd.get(), listen_fd);
    if(!rlfd_exp){
        std::cerr << "register_fd failed: " << to_string(rlfd_exp.error()) << "\n";
        return 1;
    }

    std::array <epoll_event, 128> events;
    while(true){
        int event_sz = ::epoll_wait(epfd.get(), events.data(), events.size(), -1);
        if(event_sz == -1){
            int ec = errno;
            if(errno == EINTR) continue;
            std::cerr << "event loop failed: " << std::strerror(ec) << "\n";
            return 1;
        }

        for(int i = 0;i < event_sz;++i){
            int fd = events[i].data.fd;
            uint32_t event = events[i].events;
            
            if(event & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)){
                unregister_fd(epfd.get(), socket_infos, fd);
                continue;
            }

            if(fd == listen_fd){
                auto client_fd_exp = make_client_fd(listen_fd);
                if(!client_fd_exp){
                    std::cerr << "make_client_fd failed: " << to_string(client_fd_exp.error()) << "\n";
                    continue;
                }

                auto rcfd = register_client_fd(epfd.get(), socket_infos, std::move(*client_fd_exp), EPOLLIN);
                if(!rcfd){
                    std::cerr << "register_client_fd failed: " << to_string(rcfd.error()) << "\n";
                    continue;
                }

                continue;
            }

            auto it = socket_infos.find(fd);
            if(it == socket_infos.end()) continue;
            auto& si = it->second;

            if(event & EPOLLIN){

            }

            if(event & EPOLLOUT){
                auto fs_exp = flush_send(fd, si);
                if(!fs_exp){
                    std::cerr << "flush_send failed: " << to_string(fs_exp.error()) << "\n";
                    continue; 
                }

         
            }
        }
    }

    return 0;
}