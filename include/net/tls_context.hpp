#pragma once
#include "core/error_code.hpp"
#include <expected>
#include <memory>
#include <openssl/ssl.h>
#include <string_view>

class tls_context{
    struct ctx_deleter{
        void operator()(SSL_CTX* p) const noexcept;
    };

    std::unique_ptr<SSL_CTX, ctx_deleter> ctx;
    bool server = false;

    static std::expected <void, error_code> init_tls();
    static std::expected <void, error_code> set_common_options(SSL_CTX* ctx);
    tls_context(std::unique_ptr<SSL_CTX, ctx_deleter> ctx, bool server) noexcept;
public:
    tls_context(const tls_context&) = delete;
    tls_context& operator=(const tls_context&) = delete;

    tls_context(tls_context&& other) noexcept = default;
    tls_context& operator=(tls_context&& other) noexcept = default;

    static std::expected <tls_context, error_code> create_server(
        std::string_view cert_chain_path,
        std::string_view private_key_path
    );
    static std::expected <tls_context, error_code> create_client(std::string_view ca_file_path = {});

    SSL_CTX* get() const noexcept;
    bool is_server() const noexcept;
};
