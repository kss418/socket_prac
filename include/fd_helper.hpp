#pragma once
#include <expected>
#include <unistd.h>
#include <sys/socket.h>
#include <netdb.h>
#include <cerrno>
#include "../include/unique_fd.hpp"
#include "../include/error_code.hpp"

std::expected<unique_fd, error_code> make_listen_fd(addrinfo* head);
std::expected<unique_fd, error_code> make_client_fd(int listen_fd);
std::expected<unique_fd, error_code> make_server_fd(addrinfo* head);

struct endpoint{
    sockaddr_storage ss{};
    socklen_t len = 0;
    
    const sockaddr* addr() const noexcept;
    sockaddr* addr() noexcept;
    std::expected <std::string, error_code> get_string() const;
};

std::expected<endpoint, error_code> make_peer_endpoint(int fd);