#include "../include/epoll_server.hpp"
#include "../include/addr.hpp"
#include "../include/epoll_utility.hpp"

epoll_server::epoll_server(
    epoll_registry registry, epoll_listener listener, epoll_acceptor acceptor
) : registry(std::move(registry)), listener(std::move(listener)), acceptor(std::move(acceptor)){}

std::expected <void, error_code> epoll_server::run(){
    event_loop loop(registry);
    auto run_exp = loop.run(
        [this](int fd, socket_info& si, uint32_t event){ handle_recv(fd, si, event); },
        [this](int fd, socket_info& si){ handle_send(fd, si); },
        [this](int fd){ registry.unregister(fd); }
    );

    if(!run_exp){
        handle_error("run/event loop error", run_exp);
        return std::unexpected(run_exp.error());
    }
    return {};
}

void epoll_server::handle_send(int fd, socket_info& si){
    auto fs_exp = flush_send(fd, si);
    if(!fs_exp){
        handle_error("run/handle_send/flush_send failed", fs_exp);
        registry.unregister(fd);
        return; 
    }
                
    if(si.offset < si.send_buf.size()) return;
    si.interest &= ~EPOLLOUT;
                
    auto mod_ep_exp = epoll_utility::update_interest(registry.get_epfd(), fd, si, si.interest);
    if(!mod_ep_exp) registry.unregister(fd);
}

void epoll_server::handle_recv(int fd, socket_info& si, uint32_t event){
    auto dr_exp = drain_recv(fd, si);
    if(!dr_exp){
        handle_error(to_string(si.ep) + " run/handle_recv/drain_recv failed", dr_exp);
        registry.unregister(fd);
        return; 
    }

    auto recv_info = *dr_exp;
    std::cout << to_string(si.ep) << " sends " << recv_info.byte 
        << " byte" << (recv_info.byte == 1 ? "\n" : "s\n");

    si.append(si.recv_buf);
    si.recv_buf.clear();

    if(recv_info.closed || event & EPOLLRDHUP){ // peer closed
        handle_close(fd, si);
        return;
    }

    if(si.interest & EPOLLOUT) return;
    si.interest |= EPOLLOUT;

    auto mod_ep_exp = epoll_utility::update_interest(registry.get_epfd(), fd, si, si.interest);
    if(!mod_ep_exp) registry.unregister(fd);
}

void epoll_server::handle_close(int fd, socket_info& si){
    std::cout << to_string(si.ep) << " is disconnected" << "\n";
    registry.unregister(fd);
}