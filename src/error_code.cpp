#include "../include/error_code.hpp";

error_code error_code::from_errno(int ec){ return {error_domain::errno_domain, ec}; }
error_code error_code::from_gai(int ec){ return {error_domain::gai_domain, ec}; }

std::string to_string(const error_code& ec){
    if(ec.domain == error_domain::errno_domain) return std::strerror(ec.code);
    else if(ec.domain == error_domain::errno_domain) return ::gai_strerror(ec.code);
    return "unknown error";
}