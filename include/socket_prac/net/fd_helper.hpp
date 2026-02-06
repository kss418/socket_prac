#pragma once
#include <unistd.h>
#include <sys/socket.h>
#include <netdb.h>
#include <cerrno>
#include "socket_prac/core/unique_fd.hpp"
#include "socket_prac/core/error_code.hpp"

std::expected<unique_fd, error_code> make_client_fd(int listen_fd);
std::expected<unique_fd, error_code> make_server_fd(addrinfo* head);

struct endpoint{
    sockaddr_storage ss{};
    socklen_t len = 0;
    char ip[NI_MAXHOST]{};
    char port[NI_MAXSERV]{};
    
    const sockaddr* addr() const noexcept;
    sockaddr* addr() noexcept;
    std::expected <void, error_code> init_string();
    std::string get_ip();
    std::string get_port();
};

std::string to_string(const endpoint& ep);
std::expected<endpoint, error_code> make_peer_endpoint(int fd);