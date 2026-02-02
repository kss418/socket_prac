#pragma once
#include <string>
#include <cstring>
#include <netdb.h>
#include <expected>
#include <iostream>

enum class error_domain { errno_domain, gai_domain };
struct error_code{
    error_domain domain{};
    int code{0};
    static error_code from_errno(int ec);
    static error_code from_gai(int ec);
};

std::string to_string(const error_code& ec);
std::ostream& operator<<(std::ostream& os, const error_code& ec);
void handle_error(const std::string& msg, const error_code& ec);

template<class T>
void handle_error(const std::string& msg, const std::expected<T, error_code>& ec_exp){
    assert(!ec_exp);
    handle_error(msg, ec_exp.error());
}