#include "../include/fd_helper.hpp"

std::expected<unique_fd, error_code> make_listen_fd(addrinfo* head){
    int ec = 0;
    for(addrinfo* p = head; p; p = p->ai_next){
        unique_fd fd(::socket(p->ai_family, p->ai_socktype, p->ai_protocol));
        if(!fd){ ec = errno; continue; }

        int use = 1;
        if(::setsockopt(fd.get(), SOL_SOCKET, SO_REUSEADDR, &use, sizeof(use)) == -1){
            ec = errno;
            continue;
        }

        if(::bind(fd.get(), p->ai_addr, p->ai_addrlen) == 0){
            if(::listen(fd.get(), SOMAXCONN) == 0) return fd;
            ec = errno;
            return std::unexpected(error_code::from_errno(ec));
        }

        ec = errno;
    }

    if(ec == 0) ec = EINVAL;
    return std::unexpected(error_code::from_errno(ec));
}

std::expected <unique_fd, error_code> make_client_fd(int listen_fd){
    while(true){
        int client_fd = ::accept(listen_fd, nullptr, nullptr);
        if(client_fd != -1) return unique_fd(client_fd);

        int ec = errno;
        if(ec == EINTR) continue;
        return std::unexpected(error_code::from_errno(ec));
    }
}

std::expected<unique_fd, error_code> make_server_fd(addrinfo* head){
    int ec = 0;
    for(addrinfo* p = head; p; p = p->ai_next){
        unique_fd fd(::socket(p->ai_family, p->ai_socktype, p->ai_protocol));
        if(!fd){ ec = errno; continue; }

        if(::connect(fd.get(), p->ai_addr, p->ai_addrlen) == 0) return fd;
        ec = errno;
    }

    if(ec == 0) ec = EINVAL;
    return std::unexpected(error_code::from_errno(ec));
}

const sockaddr* endpoint::addr() const noexcept{ return reinterpret_cast<const sockaddr*>(&ss); }
sockaddr* endpoint::addr() noexcept{ return reinterpret_cast<sockaddr*>(&ss); }

std::expected <void, error_code> endpoint::init_string(){
    int ec = ::getnameinfo(this->addr(), this->len, ip, sizeof(ip), port, sizeof(port), NI_NUMERICHOST | NI_NUMERICSERV);
    if(ec != 0) return std::unexpected(error_code::from_gai(ec));
    return {};
}

std::string endpoint::get_ip(){
    return std::string(ip);
}

std::string endpoint::get_port(){
    return std::string(port);
}

std::string to_string(const endpoint& ep){
    return std::string(ep.ip) + ":" + std::string(ep.port);
}

std::expected<endpoint, error_code> make_peer_endpoint(int fd){
    endpoint ep;
    while(true){
        ep.len = sizeof(ep.ss);
        if(::getpeername(fd, ep.addr(), &ep.len) == 0) return ep;

        int ec = errno;
        if(ec == EINTR) continue;
        return std::unexpected(error_code::from_errno(ec));
    }
}