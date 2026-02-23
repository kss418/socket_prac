#include "server/epoll_server.hpp"
#include "core/config_loader.hpp"
#include "core/logger.hpp"
#include "core/path_util.hpp"
#include "database/db_connector.hpp"
#include "database/db_service.hpp"
#include "net/tls_context.hpp"
#include <cerrno>
#include <csignal>
#include <ctime>
#include <filesystem>
#include <pthread.h>
#include <string>
#include <thread>

const char* signal_to_string(int sig){
    if(sig == SIGINT) return "SIGINT";
    if(sig == SIGTERM) return "SIGTERM";
    return "UNKNOWN";
}

int main(int argc, char** argv){
    (void)argc;
#if defined(SIGPIPE)
    std::signal(SIGPIPE, SIG_IGN);
#endif

    sigset_t shutdown_set{};
    ::sigemptyset(&shutdown_set);
    ::sigaddset(&shutdown_set, SIGINT);
    ::sigaddset(&shutdown_set, SIGTERM);
    int mask_rc = ::pthread_sigmask(SIG_BLOCK, &shutdown_set, nullptr);
    if(mask_rc != 0){
        logger::log_error("pthread_sigmask failed", __func__, error_code::from_errno(mask_rc));
        return 1;
    }

    std::filesystem::path root_path =
        path_util::resolve_root_with_required_files(argv, {"config/server.conf", ".env"});
    std::filesystem::path config_path = root_path / "config/server.conf";
    std::filesystem::path env_path = root_path / ".env";
    logger::log_info("runtime root path: " + root_path.string());

    auto cfg_exp = config_loader::load_key_value_file(config_path.string());
    if(!cfg_exp){
        logger::log_error("config load failed", __func__, cfg_exp);
        return 1;
    }
    logger::log_info("config load success");

    auto env_exp = config_loader::load_key_value_file(env_path.string());
    if(!env_exp){
        logger::log_error(".env load failed", __func__, env_exp);
        return 1;
    }
    logger::log_info(".env load success");

    const config_loader::config_map& cfg = *cfg_exp;
    const config_loader::config_map& env = *env_exp;
    auto req_exp = config_loader::check_server_require(cfg, env);
    if(!req_exp){
        logger::log_error("missing required config key", __func__, req_exp);
        return 1;
    }
    logger::log_info("config/env required keys validated");

    std::string db_host = config_loader::get_or(cfg, "db.host", "127.0.0.1");
    std::string db_port = config_loader::get_or(cfg, "db.port", "5432");
    std::string db_name = config_loader::get_or(cfg, "db.name", "");
    std::string db_user =
        config_loader::trim_wrapping_quotes(config_loader::get_or(env, "db.user", ""));
    std::string db_password =
        config_loader::trim_wrapping_quotes(config_loader::get_or(env, "db.password", ""));

    std::string server_port = config_loader::get_or(cfg, "server.port", "8080");
    std::string tls_cert_raw = config_loader::get_or(cfg, "tls.cert", "");
    std::string tls_key_raw = config_loader::get_or(cfg, "tls.key", "");

    std::string tls_cert_path = path_util::resolve_from_root(root_path, tls_cert_raw).string();
    std::string tls_key_path = path_util::resolve_from_root(root_path, tls_key_raw).string();

    auto db_exp = db_connector::create(
        db_host, db_port, db_name, db_user, db_password
    );
    if(!db_exp){
        logger::log_error("db connect failed", __func__, db_exp);
        return 1;
    }
    logger::log_info("db connect success / cert = " + tls_cert_raw + " / key = " + tls_key_raw);

    db_service db(*db_exp);
    auto tls_ctx_exp = tls_context::create_server(tls_cert_path, tls_key_path);
    if(!tls_ctx_exp){
        logger::log_error("tls context create failed", __func__, tls_ctx_exp);
        return 1;
    }
    logger::log_info("tls context create success");

    auto server_exp = epoll_server::create(server_port.c_str(), db, std::move(*tls_ctx_exp));
    if(!server_exp) return 1;
    logger::log_info("server create success");

    logger::log_info("server run start port:" + server_port);
    std::stop_source stop_source;
    std::jthread signal_waiter([&stop_source, &shutdown_set](std::stop_token st){
        while(!st.stop_requested()){
            timespec timeout{};
            timeout.tv_sec = 0;
            timeout.tv_nsec = 200 * 1000 * 1000;

            int sig = ::sigtimedwait(&shutdown_set, nullptr, &timeout);
            if(sig == SIGINT || sig == SIGTERM){
                logger::log_info(std::string("shutdown signal received: ") + signal_to_string(sig));
                stop_source.request_stop();
                return;
            }

            if(sig == -1){
                int ec = errno;
                if(ec == EAGAIN || ec == EINTR) continue;
                logger::log_warn("sigtimedwait failed", __func__, error_code::from_errno(ec));
                return;
            }
        }
    });

    auto run_exp = server_exp->run(stop_source.get_token());
    signal_waiter.request_stop();
    if(!run_exp){
        logger::log_error("server run failed", __func__, run_exp);
        return 1;
    }

    return 0;
}
