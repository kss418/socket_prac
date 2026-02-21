#include "server/epoll_server.hpp"
#include "core/config_loader.hpp"
#include "database/db_connector.hpp"
#include "database/db_service.hpp"
#include "net/tls_context.hpp"
#include <cstdlib>
#include <filesystem>

int main(){
    std::filesystem::path root_path = std::filesystem::path(PROJECT_ROOT_DIR);
    std::filesystem::path config_path = root_path / "config/server.conf";

    auto cfg_exp = config_loader::load_key_value_file(config_path.string());
    if(!cfg_exp){
        handle_error("config load failed", cfg_exp);
        return 1;
    }

    const config_loader::config_map& cfg = *cfg_exp;
    auto req_exp = config_loader::check_server_require(cfg);
    if(!req_exp){
        handle_error("missing required config key", req_exp);
        return 1;
    }

    std::string db_host = config_loader::get_or(cfg, "db.host", "127.0.0.1");
    std::string db_port = config_loader::get_or(cfg, "db.port", "5432");
    std::string db_name = config_loader::get_or(cfg, "db.name", "");
    std::string db_user = config_loader::get_or(cfg, "db.user", "");
    std::string db_password_env = config_loader::get_or(cfg, "db.password_env", "DB_PASSWORD");
    const char* db_password_raw = std::getenv(db_password_env.c_str());
    std::string db_password = db_password_raw == nullptr ? "" : db_password_raw;

    std::string server_port = config_loader::get_or(cfg, "server.port", "8080");
    std::string tls_cert_raw = config_loader::get_or(cfg, "tls.cert", "");
    std::string tls_key_raw = config_loader::get_or(cfg, "tls.key", "");

    std::string tls_cert_path = root_path / tls_cert_raw;
    std::string tls_key_path = root_path / tls_key_raw;

    auto db_exp = db_connector::create(
        db_host, db_port, db_name, db_user, db_password
    );
    if(!db_exp){
        handle_error("db connect failed", db_exp);
        return 1;
    }

    db_service db(*db_exp);

    auto tls_ctx_exp = tls_context::create_server(tls_cert_path, tls_key_path);
    if(!tls_ctx_exp){
        handle_error("tls context create failed", tls_ctx_exp);
        return 1;
    }

    auto server_exp = epoll_server::create(server_port.c_str(), db, std::move(*tls_ctx_exp));
    if(!server_exp) return 1;

    auto run_exp = server_exp->run();
    if(!run_exp){
        handle_error("server run failed", run_exp);
        return 1;
    }

    return 0;
}
