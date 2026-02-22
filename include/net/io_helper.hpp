#pragma once
#include "core/error_code.hpp"
#include "core/unique_fd.hpp"
#include "net/fd_helper.hpp"
#include "net/tls_session.hpp"
#include "protocol/command_codec.hpp"
#include <string_view>
#include <string>

constexpr int BUF_SIZE = 4096;

class offset_buffer{
protected:
    std::string buf;
    std::size_t offset = 0;
public:
    bool clear_if_done();
    bool compact_if_needed();
    void reset_offset();

    bool has_pending() const;
    const char* current_data() const;
    std::size_t remaining() const;
    void advance(std::size_t n);
    std::size_t get_offset() const;
    void set_offset(std::size_t new_offset);

    std::string& raw();
    const std::string& raw() const;
};

class send_buffer : public offset_buffer{
public:
    bool append(std::string_view sv);
    bool append(const char* p, std::size_t n);
    bool append(const command_codec::command& cmd);
};

class recv_buffer : public offset_buffer{
public:
    void append(const char* p, std::size_t n);
    std::string take_all();
};

struct socket_info{
    recv_buffer recv;
    send_buffer send;
    tls_session tls;
    bool is_closed = false;
    uint32_t interest = 0;
    unique_fd ufd;
    endpoint ep;
    std::string nickname = "guest";
};

struct recv_info{
    std::size_t byte = 0;
    bool closed = 0;
};

std::expected <std::size_t, error_code> flush_send(socket_info& si);
std::expected <recv_info, error_code> drain_recv(socket_info& si);
