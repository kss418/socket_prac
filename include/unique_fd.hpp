#pragma once

struct unique_fd{
private:
    int fd = -1;
public:
    unique_fd() noexcept = default;
    explicit unique_fd(int fd) noexcept;
    
    unique_fd(const unique_fd&) = delete;
    unique_fd& operator=(const unique_fd&) = delete;

    unique_fd(unique_fd&& other) noexcept;
    unique_fd& operator=(unique_fd&& other) noexcept;

    ~unique_fd() noexcept;
    void reset(int) noexcept;
    int get() const noexcept;
    explicit operator bool() const noexcept;
};