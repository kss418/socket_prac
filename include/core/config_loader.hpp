#pragma once
#include "core/error_code.hpp"
#include <expected>
#include <string>
#include <string_view>
#include <unordered_map>

namespace config_loader{
    using config_map = std::unordered_map<std::string, std::string>;

    enum class config_error : int{
        file_not_found = 1,
        malformed_line,
        empty_key,
        duplicate_key,
        read_failed,
        missing_required_key
    };

    std::string config_strerror(int code);
    std::string trim(std::string_view sv);
    std::string trim_wrapping_quotes(std::string_view sv);
    bool is_comment_or_blank(std::string_view line);
    std::expected <config_map, error_code> load_key_value_file(std::string_view path);
    std::expected <std::string, error_code> require(const config_map& cfg, std::string_view key);
    std::string get_or(const config_map& cfg, std::string_view key, std::string_view fallback);
    
    std::expected <void, error_code> check_server_require(const config_map& cfg, const config_map& env);
} 
