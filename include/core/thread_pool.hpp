#pragma once
#include "protocol/command_codec.hpp"
#include "net/io_helper.hpp"
#include "reactor/epoll_registry.hpp"
#include <thread>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <queue>

class thread_pool{
    std::vector<std::jthread> workers;
    std::mutex mtx;
    std::condition_variable cv;
    bool run = true;

    struct task{
        command_codec::command cmd;
        epoll_registry& reg;
        int fd;
    };

    std::queue <task> tasks;
    void worker_loop(std::stop_token st);
    void execute(const task& t);
public:
    explicit thread_pool(std::size_t sz = std::thread::hardware_concurrency());
    ~thread_pool();

    thread_pool(const thread_pool&) = delete;
    thread_pool& operator=(const thread_pool&) = delete;

    void stop();
    bool enqueue(command_codec::command cmd, epoll_registry& reg, int fd);
};