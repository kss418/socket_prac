#include "../include/epoll_server.hpp"
#include "../include/addr.hpp"
#include "../include/epoll_utility.hpp"
#include <thread>

std::expected <epoll_server, error_code> epoll_server::create(const char* port){
    auto addr_exp = get_addr_server(port);
    if(!addr_exp){
        handle_error("create/get_addr_server failed", addr_exp);
        return std::unexpected(addr_exp.error());
    }

    auto listen_fd_exp = epoll_listener::create(addr_exp->get());
    if(!listen_fd_exp){
        handle_error("create/epoll_listener::create failed", listen_fd_exp);
        return std::unexpected(listen_fd_exp.error());
    }

    auto wakeup_exp = epoll_wakeup::create();
    if(!wakeup_exp){
        handle_error("create/epoll_wakeup::create failed", wakeup_exp);
        return std::unexpected(wakeup_exp.error());
    }

    epoll_registry registry(std::move(*wakeup_exp));
    epoll_listener listener = std::move(*listen_fd_exp);
    return epoll_server(std::move(registry), std::move(listener));
}

epoll_server::epoll_server(
    epoll_registry registry, epoll_listener listener
) : registry(std::move(registry)), listener(std::move(listener)){}

std::expected <void, error_code> epoll_server::run(){
    std::jthread accept_thread([this](std::stop_token st){
        epoll_acceptor acceptor(listener, registry);
        auto accept_exp = acceptor.run(st);
        if(!accept_exp) handle_error("acceptor thread error", accept_exp);
    });

    event_loop loop(registry);
    auto run_exp = loop.run(
        [this](int fd, socket_info& si, uint32_t event){ handle_recv(fd, si, event); },
        [this](int fd, socket_info& si){ handle_send(fd, si); },
        [this](int fd){ registry.request_unregister(fd); }
    );

    if(!run_exp){
        handle_error("run/event loop error", run_exp);
        accept_thread.request_stop();
        return std::unexpected(run_exp.error());
    }

    accept_thread.request_stop();
    return {};
}

void epoll_server::handle_send(int fd, socket_info& si){
    auto fs_exp = flush_send(fd, si);
    if(!fs_exp){
        handle_error("run/handle_send/flush_send failed", fs_exp);
        registry.request_unregister(fd);
        return; 
    }
                
    if(si.offset < si.send_buf.size()) return;
    si.interest &= ~EPOLLOUT;
                
    auto mod_ep_exp = epoll_utility::update_interest(registry.get_epfd(), fd, si, si.interest);
    if(!mod_ep_exp) registry.request_unregister(fd);
}

void epoll_server::handle_recv(int fd, socket_info& si, uint32_t event){
    auto dr_exp = drain_recv(fd, si);
    if(!dr_exp){
        handle_error(to_string(si.ep) + " run/handle_recv/drain_recv failed", dr_exp);
        registry.request_unregister(fd);
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
    if(!mod_ep_exp) registry.request_unregister(fd);
}

void epoll_server::handle_close(int fd, socket_info& si){
    std::cout << to_string(si.ep) << " is disconnected" << "\n";
    registry.request_unregister(fd);
}
