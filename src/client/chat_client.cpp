#include "client/chat_client.hpp"
#include "client/chat_executor.hpp"
#include "client/chat_io_worker.hpp"
#include "core/logger.hpp"
#include "net/addr.hpp"
#include <condition_variable>
#include <mutex>
#include <optional>
#include <thread>

chat_client::chat_client(socket_info si, unique_fd server_fd, tls_context tls_ctx) :
    si(std::move(si)),
    server_fd(std::move(server_fd)),
    tls_ctx(std::move(tls_ctx)),
    logged_in(false){}

std::expected <chat_client, error_code> chat_client::create(
    const char* ip, const char* port, std::string_view ca_file_path
){
    auto addr_exp = get_addr_client(ip, port);
    if(!addr_exp){
        logger::log_error("get_addr_client failed", "chat_client::create()", addr_exp);
        return std::unexpected(addr_exp.error());
    }

    auto server_fd_exp = make_server_fd(addr_exp->get());
    if(!server_fd_exp){
        logger::log_error("make_server_fd failed", "chat_client::create()", server_fd_exp);
        return std::unexpected(server_fd_exp.error());
    }

    auto tls_ctx_exp = tls_context::create_client(ca_file_path);
    if(!tls_ctx_exp){
        logger::log_error("tls_context create failed", "chat_client::create()", tls_ctx_exp);
        return std::unexpected(tls_ctx_exp.error());
    }

    tls_context tls_ctx = std::move(*tls_ctx_exp);

    auto tls_exp = tls_session::create_client(tls_ctx, server_fd_exp->get(), ip);
    if(!tls_exp){
        logger::log_error("tls_session create failed", "chat_client::create()", tls_exp);
        return std::unexpected(tls_exp.error());
    }

    socket_info si{};
    si.tls = std::move(*tls_exp);

    return std::expected<chat_client, error_code>(
        std::in_place, std::move(si), std::move(*server_fd_exp), std::move(tls_ctx)
    );
}

std::expected <void, error_code> chat_client::run(){
    chat_executor executor(logged_in);
    chat_io_worker io_worker(si, server_fd, executor, logged_in);

    std::mutex state_mtx;
    std::condition_variable state_cv;
    std::optional<error_code> error_opt;
    bool stop_requested = false;

    auto signal_stop = [&](std::optional<error_code> ec = std::nullopt){
        {
            std::lock_guard<std::mutex> lock(state_mtx);
            if(ec && !error_opt) error_opt = *ec;
            stop_requested = true;
        }
        state_cv.notify_one();
    };

    std::jthread execute_thread([&executor, &signal_stop](std::stop_token st){
        executor.run(st);
        signal_stop();
    });

    std::jthread io_thread([&io_worker, &signal_stop](std::stop_token st){
        auto io_exp = io_worker.run(st);
        if(!io_exp){
            logger::log_error("io_thread error", "chat_client::run()", io_exp);
            signal_stop(io_exp.error());
        }
        else signal_stop();
    });

    {
        std::unique_lock<std::mutex> lock(state_mtx);
        state_cv.wait(lock, [&](){ return stop_requested; });
    }

    execute_thread.request_stop();
    io_thread.request_stop();
    execute_thread.join();
    io_thread.join();

    if(error_opt) return std::unexpected(*error_opt);
    return {};
}
