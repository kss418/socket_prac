#pragma once
#include "../include/error_code.hpp"

struct socket_info{
    std::string buf;
    size_t offset = 0;
    uint32_t interest;
}

std::expected <void, error_code> flush_send(int fd);