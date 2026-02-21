#pragma once
#include "core/error_code.hpp"
#include "net/tls_context.hpp"
#include <expected>
#include <memory>
#include <openssl/ssl.h>
#include <string_view>

struct tls_io_result{
    std::size_t byte = 0;
    bool closed = false;
    bool want_read = false;
    bool want_write = false;
};

class tls_session{
    struct ssl_deleter{
        void operator()(SSL* p) const noexcept;
    };

    std::unique_ptr<SSL, ssl_deleter> ssl;
    bool handshake_done = false;
    bool want_read = false;
    bool want_write = false;
    bool peer_closed = false;

    explicit tls_session(std::unique_ptr<SSL, ssl_deleter> ssl) noexcept;
    std::expected <tls_io_result, error_code> from_ssl_error(int ssl_error, std::size_t byte);
    void apply_state(const tls_io_result& io) noexcept;
public:
    tls_session() noexcept = default;
    tls_session(const tls_session&) = delete;
    tls_session& operator=(const tls_session&) = delete;

    tls_session(tls_session&& other) noexcept = default;
    tls_session& operator=(tls_session&& other) noexcept = default;

    static std::expected <tls_session, error_code> create_server(tls_context& ctx, int fd);
    static std::expected <tls_session, error_code> create_client(
        tls_context& ctx, int fd, std::string_view server_name
    );

    std::expected <tls_io_result, error_code> handshake();
    std::expected <tls_io_result, error_code> read(char* dst, std::size_t cap);
    std::expected <tls_io_result, error_code> write(const char* src, std::size_t len);
    std::expected <void, error_code> shutdown();
    std::expected <void, error_code> verify_peer() const;

    bool is_handshake_done() const noexcept;
    bool needs_read() const noexcept;
    bool needs_write() const noexcept;
    bool is_closed() const noexcept;
    SSL* get() const noexcept;
};
