#include "core/error_code.hpp"
#include "core/config_loader.hpp"
#include "database/db_connector.hpp"
#include "protocol/command_codec.hpp"

error_code error_code::from_errno(int ec){ return {error_domain::errno_domain, ec}; }

error_code error_code::from_gai(int ec){ return {error_domain::gai_domain, ec}; }

error_code error_code::from_decode(int ec){ return {error_domain::decode_domain, ec}; }
error_code error_code::from_decode(command_codec::decode_error ec){
    return from_decode(static_cast<int>(ec));
}

error_code error_code::from_db(int ec){ return {error_domain::db_domain, ec}; }

error_code error_code::from_config(int ec){ return {error_domain::config_domain, ec}; }
error_code error_code::from_config(config_loader::config_error ec){
    return from_config(static_cast<int>(ec));
}

std::string to_string(const error_code& ec){
    if(ec.domain == error_domain::errno_domain) return std::strerror(ec.code);
    else if(ec.domain == error_domain::gai_domain) return ::gai_strerror(ec.code);
    else if(ec.domain == error_domain::decode_domain) return command_codec::decode_strerror(ec.code);
    else if(ec.domain == error_domain::db_domain) return db_connector::db_strerror(ec.code);
    else if(ec.domain == error_domain::config_domain) return config_loader::config_strerror(ec.code);
    return "unknown error";
}

std::ostream& operator<<(std::ostream& os, const error_code& ec){
    return os << to_string(ec);
}

void handle_error(const std::string& msg, const error_code& ec){
    std::cerr << msg << " " << ec << "\n";
}
