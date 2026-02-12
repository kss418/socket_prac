#include "client/chat_client.hpp"
#include "client/chat_executor.hpp"
#include "client/chat_io_worker.hpp"
#include "net/addr.hpp"
#include <condition_variable>
#include <mutex>
#include <optional>
#include <thread>

std::expected <void, error_code> chat_client::connect(const char* ip, const char* port){
    auto addr_exp = get_addr_client(ip, port);
    if(!addr_exp){
        handle_error("get_addr_client failed", addr_exp);
        return std::unexpected(addr_exp.error());
    }

    auto server_fd_exp = make_server_fd(addr_exp->get());
    if(!server_fd_exp){
        handle_error("make_server_fd failed", server_fd_exp);
        return std::unexpected(server_fd_exp.error());
    }

    server_fd = std::move(*server_fd_exp);
    si = {};
    return {};
}

std::expected <void, error_code> chat_client::run(){
    chat_executor executor;
    chat_io_worker io_worker(si, server_fd, executor);

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
            handle_error("chat_io_worker/run failed", io_exp);
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
