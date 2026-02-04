#include "../include/epoll_server.hpp"
#include "../include/addr.hpp"
#include "../include/epoll_utility.hpp"

epoll_server::epoll_server(const char* port) : port(port){}

std::expected <void, error_code> epoll_server::init(){
    auto addr_exp = get_addr_server(port);
    if(!addr_exp){
        handle_error("init/get_addr_server failed", addr_exp);
        return std::unexpected(addr_exp.error());
    }

    auto listen_fd_exp = epoll_listener::make_listener(addr_exp->get());
    if(!listen_fd_exp){
        handle_error("init/make_listener failed", listen_fd_exp);
        return std::unexpected(listen_fd_exp.error());
    }

    unique_fd epfd = unique_fd{::epoll_create1(EPOLL_CLOEXEC)};
    if(!epfd){
        int ec = errno;
        handle_error("init/epoll_create1 failed", error_code::from_errno(ec));
        return std::unexpected(error_code::from_errno(ec));
    }

    ep_registry = {std::move(epfd)};
    listener = std::move(*listen_fd_exp);

    return {};
}

std::expected <void, error_code> epoll_server::run(){
    event_loop loop(ep_registry);
    auto run_exp = loop.run(
        [this](int fd, socket_info& si, uint32_t event){ handle_recv(fd, si, event); },
        [this](int fd, socket_info& si){ handle_send(fd, si); },
        [this](int fd){ ep_registry.unregister(fd); }
    );

    if(!run_exp){
        handle_error("run/event loop error", run_exp);
        return std::unexpected(run_exp.error());
    }
    return {};
}

void epoll_server::handle_accept(int listen_fd){
    auto client_fd_exp = make_client_fd(listen_fd);
    if(!client_fd_exp){
        handle_error("run/handle_accept/make_client_fd failed", client_fd_exp);
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

void epoll_server::handle_send(int fd, socket_info& si){
    auto fs_exp = flush_send(fd, si);
    if(!fs_exp){
        handle_error("run/handle_send/flush_send failed", fs_exp);
        ep_registry.unregister(fd);
        return; 
    }
                
    if(si.offset < si.send_buf.size()) return;
    si.interest &= ~EPOLLOUT;
                
    auto mod_ep_exp = epoll_utility::update_interest(ep_registry.get_epfd(), fd, si, si.interest);
    if(!mod_ep_exp) ep_registry.unregister(fd);
}

void epoll_server::handle_recv(int fd, socket_info& si, uint32_t event){
    auto dr_exp = drain_recv(fd, si);
    if(!dr_exp){
        handle_error(to_string(si.ep) + " run/handle_recv/drain_recv failed", dr_exp);
        ep_registry.unregister(fd);
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

    auto mod_ep_exp = epoll_utility::update_interest(ep_registry.get_epfd(), fd, si, si.interest);
    if(!mod_ep_exp) ep_registry.unregister(fd);
}

void epoll_server::handle_close(int fd, socket_info& si){
    std::cout << to_string(si.ep) << " is disconnected" << "\n";
    ep_registry.unregister(fd);
}