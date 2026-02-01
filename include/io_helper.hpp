#pragma once
#include "../include/error_code.hpp"
#include "../include/unique_fd.hpp"
#include <unordered_map>
#include <string_view>
#include <string>

constexpr int BUF_SIZE = 4096;

struct socket_info{
    std::string buf;
    std::size_t offset = 0;
    uint32_t interest = 0;
    unique_fd ufd;
    endpoint ep;

    bool buf_compact();
    bool buf_clear();

    void append(std::string_view sv);
    void append(const char* p, std::size_t n);
};

struct recv_info{
    std::size_t byte = 0;
    bool closed = 0;
};

std::expected <std::size_t, error_code> flush_send(int fd, socket_info& si);
std::expected <recv_info, error_code> drain_recv(int fd, std::string& buf);
void flush_recv(std::string& recv_buf);

std::expected <void, error_code> register_listen_fd(int epfd, int ufd);

std::expected <void, error_code> register_client_fd(
    int epfd, std::unordered_map<int, socket_info>& infos, unique_fd ufd, uint32_t events
);

std::expected <void, error_code> unregister_fd(
    int epfd, std::unordered_map<int, socket_info>& infos, int fd
);