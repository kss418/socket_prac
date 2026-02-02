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
        handle_error("get_addr_server failed", addr_exp);
        return 1;
    }

    auto listen_fd_exp = make_listen_fd(addr_exp->get());
    if(!listen_fd_exp){
        handle_error("make_listen_fd failed", listen_fd_exp);
        return 1;
    }

    auto epfd = unique_fd{::epoll_create1(EPOLL_CLOEXEC)};
    if(!epfd){
        int ec = errno;
        handle_error("epoll_create1 failed", error_code::from_errno(ec));
        return 1;
    }

    std::unordered_map <int, socket_info> socket_infos;
    auto listen_fd = std::move(*listen_fd_exp);
    auto rlfd_exp = register_listen_fd(epfd.get(), listen_fd.get());
    if(!rlfd_exp){
        handle_error("register_fd failed", rlfd_exp);
        return 1;
    }

    std::array <epoll_event, 128> events;
    while(true){
        int event_sz = ::epoll_wait(epfd.get(), events.data(), events.size(), -1);
        if(event_sz == -1){
            int ec = errno;
            if(errno == EINTR) continue;
            handle_error("event loop failed", error_code::from_errno(ec));
            return 1;
        }

        for(int i = 0;i < event_sz;++i){
            int fd = events[i].data.fd;
            uint32_t event = events[i].events;
            
            if(event & (EPOLLERR | EPOLLHUP)){ // error
                if(fd == listen_fd.get()){
                    std::cerr << "listen fd error" << "\n";
                    return 1; 
                }

                unregister_fd(epfd.get(), socket_infos, fd);
                continue;
            }

            if(fd == listen_fd.get()){ // accept socket
                auto client_fd_exp = make_client_fd(listen_fd.get());
                if(!client_fd_exp){
                    handle_error("make_client_fd failed", client_fd_exp);
                    continue;
                }

                auto rcfd_exp = register_client_fd(epfd.get(), socket_infos, std::move(*client_fd_exp), EPOLLIN | EPOLLRDHUP);
                if(!rcfd_exp){
                    handle_error("register_client_fd failed", rcfd_exp);
                    continue;
                } 

                auto ep = socket_infos[*rcfd_exp].ep;
                std::cout << to_string(ep) << " is connected" << "\n";
                continue;
            }

            auto it = socket_infos.find(fd);
            if(it == socket_infos.end()) continue;
            auto& si = it->second;

            if(event & (EPOLLIN | EPOLLRDHUP)){ // recv data
                auto dr_exp = drain_recv(fd, si);
                if(!dr_exp){
                    handle_error("drain_recv failed", dr_exp);
                    unregister_fd(epfd.get(), socket_infos, fd);
                    continue; 
                }

                auto recv_info = *dr_exp;
                std::cout << to_string(si.ep) << " sends " << recv_info.byte 
                    << " byte" << (recv_info.byte == 1 ? "\n" : "s\n");

                si.append(si.recv_buf);
                si.recv_buf.clear();

                if(recv_info.closed || event & EPOLLRDHUP){ // peer closed
                    std::cout << to_string(si.ep) << " is disconnected" << "\n";
                    unregister_fd(epfd.get(), socket_infos, fd);
                    continue;
                }

                if(si.interest & EPOLLOUT) continue;
                si.interest |= EPOLLOUT;

                auto mod_ep_exp = mod_ep(epfd.get(), fd, si.interest);
                if(!mod_ep_exp){
                    handle_error("mod_ep failed", mod_ep_exp);
                    unregister_fd(epfd.get(), socket_infos, fd);
                }
            }

            if(event & EPOLLOUT){ // send data
                auto fs_exp = flush_send(fd, si);
                if(!fs_exp){
                    handle_error("flush_send failed", fs_exp);
                    unregister_fd(epfd.get(), socket_infos, fd);
                    continue; 
                }
                
                if(si.offset < si.send_buf.size()) continue;
                si.interest &= ~EPOLLOUT;
                
                auto mod_ep_exp = mod_ep(epfd.get(), fd, si.interest);
                if(!mod_ep_exp){
                    handle_error("mod_ep failed", mod_ep_exp);
                    unregister_fd(epfd.get(), socket_infos, fd);
                }
            }
        }
    }

    return 0;
}