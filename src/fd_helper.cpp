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
