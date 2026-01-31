#include <string>
#include <cstring>
#include <netdb.h>

enum class error_domain { errno_domain, gai_domain };
struct error_code{
    error_domain domain{};
    int code{0};
    static error_code from_errno(int ec);
    static error_code from_gai(int ec);
};

std::string to_string(const error_code& ec);