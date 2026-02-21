#include "core/config_loader.hpp"
#include <cctype>
#include <cerrno>
#include <fstream>

std::string config_loader::trim(std::string_view sv){
    std::size_t begin = 0;
    while(begin < sv.size() && std::isspace(static_cast<unsigned char>(sv[begin])) != 0){
        ++begin;
    }

    std::size_t end = sv.size();
    while(end > begin && std::isspace(static_cast<unsigned char>(sv[end - 1])) != 0){
        --end;
    }

    return std::string(sv.substr(begin, end - begin));
}

bool config_loader::is_comment_or_blank(std::string_view line){
    for(char ch : line){
        if(std::isspace(static_cast<unsigned char>(ch)) != 0) continue;
        return ch == '#';
    }
    return true;
}

std::expected <config_loader::config_map, error_code> config_loader::load_key_value_file(std::string_view path){
    std::ifstream in{std::string(path)};
    if(!in.is_open()) return std::unexpected(error_code::from_errno(ENOENT));

    config_map cfg;
    std::string line;
    while(std::getline(in, line)){
        if(is_comment_or_blank(line)) continue;

        std::size_t eq = line.find('=');
        if(eq == std::string::npos){
            return std::unexpected(error_code::from_errno(EPROTO));
        }

        std::string key = trim(std::string_view(line).substr(0, eq));
        std::string value = trim(std::string_view(line).substr(eq + 1));
        if(key.empty()){
            return std::unexpected(error_code::from_errno(EINVAL));
        }

        auto [it, inserted] = cfg.emplace(std::move(key), std::move(value));
        if(!inserted){
            return std::unexpected(error_code::from_errno(EEXIST));
        }
    }

    if(!in.eof() && in.fail()){
        return std::unexpected(error_code::from_errno(EIO));
    }

    return cfg;
}

std::expected <std::string, error_code> config_loader::require(const config_map& cfg, std::string_view key){
    auto it = cfg.find(std::string(key));
    if(it == cfg.end()) return std::unexpected(error_code::from_errno(ENOENT));
    return it->second;
}

std::string config_loader::get_or(
    const config_map& cfg, std::string_view key, std::string_view fallback
){
    auto it = cfg.find(std::string(key));
    if(it == cfg.end()) return std::string(fallback);
    return it->second;
}

std::expected <void, error_code> config_loader::check_server_require(const config_loader::config_map& cfg){
    if(std::expected<std::string, error_code> req = config_loader::require(cfg, "db.host"); !req){
        return std::unexpected(req.error());
    }
    if(std::expected<std::string, error_code> req = config_loader::require(cfg, "db.port"); !req){
        return std::unexpected(req.error());
    }
    if(std::expected<std::string, error_code> req = config_loader::require(cfg, "db.name"); !req){
        return std::unexpected(req.error());
    }
    if(std::expected<std::string, error_code> req = config_loader::require(cfg, "db.user"); !req){
        return std::unexpected(req.error());
    }
    if(std::expected<std::string, error_code> req = config_loader::require(cfg, "tls.cert"); !req){
        return std::unexpected(req.error());
    }
    if(std::expected<std::string, error_code> req = config_loader::require(cfg, "tls.key"); !req){
        return std::unexpected(req.error());
    }
    if(std::expected<std::string, error_code> req = config_loader::require(cfg, "db.password_env"); !req){
        return std::unexpected(req.error());
    }

    return {};
}
