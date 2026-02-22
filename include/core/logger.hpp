#pragma once

#include <cassert>
#include <expected>
#include <string_view>

struct error_code;

namespace logger{
    enum class log_level : int{
        debug = 0,
        info = 1,
        warn = 2,
        error = 3
    };

    void set_log_level(log_level level);
    log_level get_log_level();

    void log_debug(std::string_view msg);
    void log_debug(std::string_view location, std::string_view function, std::string_view msg);
    void log_info(std::string_view msg);
    void log_info(std::string_view location, std::string_view function, std::string_view msg);
    void log_warn(const error_code& ec);
    void log_warn(std::string_view msg, std::string_view function, const error_code& ec);
    void log_error(const error_code& ec);
    void log_error(std::string_view msg, std::string_view function, const error_code& ec);

    template<class T>
    void log_warn(const std::expected<T, error_code>& ec_exp){
        assert(!ec_exp);
        log_warn(ec_exp.error());
    }

    template<class T>
    void log_warn(std::string_view msg, std::string_view function, const std::expected<T, error_code>& ec_exp){
        assert(!ec_exp);
        log_warn(msg, function, ec_exp.error());
    }

    template<class T>
    void log_error(const std::expected<T, error_code>& ec_exp){
        assert(!ec_exp);
        log_error(ec_exp.error());
    }

    template<class T>
    void log_error(std::string_view msg, std::string_view function, const std::expected<T, error_code>& ec_exp){
        assert(!ec_exp);
        log_error(msg, function, ec_exp.error());
    }
}
