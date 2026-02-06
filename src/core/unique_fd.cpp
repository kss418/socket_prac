#include "core/unique_fd.hpp"
#include <unistd.h>

unique_fd::unique_fd(unique_fd&& other) noexcept{
    this->fd = other.fd;
    other.fd = -1;
}

unique_fd& unique_fd::operator=(unique_fd&& other) noexcept{
    if(this != &other){
        reset();
        fd = other.fd;
        other.fd = -1;
    }
    return *this;
}

unique_fd::~unique_fd() noexcept{ reset(); }

void unique_fd::reset(int new_fd) noexcept{
    if(fd != -1) ::close(fd);
    fd = new_fd;
}

int unique_fd::get() const noexcept{ return fd; }
unique_fd::operator bool() const noexcept{ return fd != -1; }
unique_fd::unique_fd(int fd) noexcept : fd(fd){}