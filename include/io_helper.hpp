#pragma once
#include "../include/error_code.hpp"
#include <string_view>
#include <string>

struct socket_info{
    std::string buf;
    size_t offset = 0;
    uint32_t interest = 0;

    bool buf_compact();
    bool buf_clear();

    void append(std::string_view sv);
    void append(const char* p, std::size_t n);
};

std::expected <size_t, error_code> flush_send(int fd, socket_info& si);