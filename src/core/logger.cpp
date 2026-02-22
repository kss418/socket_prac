#include "core/logger.hpp"
#include "core/error_code.hpp"

#include <atomic>
#include <iostream>

namespace logger{
    static std::atomic<int> g_log_level{static_cast<int>(log_level::info)};

    static bool should_log(log_level level){
        return static_cast<int>(level) >= g_log_level.load(std::memory_order_relaxed);
    }

    static void log_line(std::string_view level_name, std::string_view msg){
        std::clog << "[" << level_name << "] " << msg << "\n";
    }

    static void log_line(std::string_view level_name, const error_code& ec){
        std::clog << "[" << level_name << "] " << ::to_string(ec) << "\n";
    }

    static void log_line(std::string_view level_name, std::string_view location, std::string_view function, std::string_view msg){
        std::clog << "[" << level_name << "] "
                  << "[" << location << "::" << function << "] "
                  << msg << "\n";
    }

    static void log_line(std::string_view level_name, std::string_view msg, std::string_view function, const error_code& ec){
        std::clog << "[" << level_name << "] "
                  << "[" << function << "] "
                  << msg
                  << " "
                  << ::to_string(ec)
                  << "\n";
    }

    void set_log_level(log_level level){
        g_log_level.store(static_cast<int>(level), std::memory_order_relaxed);
    }

    log_level get_log_level(){
        return static_cast<log_level>(g_log_level.load(std::memory_order_relaxed));
    }

    void log_debug(std::string_view msg){
        if(should_log(log_level::debug)) log_line("DEBUG", msg);
    }
    void log_debug(std::string_view location, std::string_view function, std::string_view msg){
        if(should_log(log_level::debug)) log_line("DEBUG", location, function, msg);
    }

    void log_info(std::string_view msg){
        if(should_log(log_level::info)) log_line("INFO", msg);
    }
    void log_info(std::string_view location, std::string_view function, std::string_view msg){
        if(should_log(log_level::info)) log_line("INFO", location, function, msg);
    }

    void log_warn(const error_code& ec){
        if(should_log(log_level::warn)) log_line("WARN", ec);
    }
    
    void log_warn(std::string_view msg, std::string_view function, const error_code& ec){
        if(should_log(log_level::warn)) log_line("WARN", msg, function, ec);
    }

    void log_error(const error_code& ec){
        if(should_log(log_level::error)) log_line("ERROR", ec);
    }

    void log_error(std::string_view msg, std::string_view function, const error_code& ec){
        if(should_log(log_level::error)) log_line("ERROR", msg, function, ec);
    }
}
