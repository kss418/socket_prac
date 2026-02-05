#pragma once
#include "../include/error_code.hpp"
#include "../include/epoll_server.hpp"

namespace server_factory{
    std::expected <epoll_server, error_code> create_server(const char* port);
};