#pragma once
#include <string>
#include <cstring>
#include <netdb.h>
#include <expected>
#include <iostream>

namespace command_codec{
    enum class decode_error : int;
}

namespace config_loader{
    enum class config_error : int;
}

namespace tls{
    enum class tls_error : int;
}

class db_connector;

enum class error_domain { errno_domain, gai_domain, decode_domain, db_domain, config_domain, tls_domain };
struct error_code{
    error_domain domain{};
    int code{0};

    static error_code from_errno(int ec);

    static error_code from_gai(int ec);

    static error_code from_decode(int ec);
    static error_code from_decode(command_codec::decode_error ec);

    static error_code from_db(int ec);
    static error_code from_config(int ec);
    static error_code from_config(config_loader::config_error ec);
    static error_code from_tls(int ec);
    static error_code from_tls(tls::tls_error ec);
};

std::string to_string(const error_code& ec);
std::ostream& operator<<(std::ostream& os, const error_code& ec);
