#pragma once
#include "../include/error_code.hpp"
#include "../include/unique_fd.hpp"

class acceptor{
    void acceptor::handle_accept();
    std::expected <unique_fd, error_code> acceptor::make_client_fd();
public:
    acceptor(const acceptor&) = delete;
    acceptor& operator=(const acceptor&) = delete;

    acceptor(acceptor&&) noexcept = default;
    acceptor& operator=(acceptor&&) noexcept = default;

    acceptor(unique_fd fd);
    std::expected <void, error_code> run();
};
