#include "../include/unique_fd.hpp"
#include <unistd.h>

unique_fd::unique_fd(unique_fd&& other) noexcept{
    this->fd = other.fd;
    other.fd = -1;
}

unique_fd& unique_fd::operator=(unique_fd&& other){
    if(this != &other){
        reset();
        fd = other.fd;
        other.fd = -1;
    }
    return *this;
}

unique_fd::~unique_fd() noexcept{ reset(); }

void unique_fd::reset(int new_fd = -1) noexcept{
    if(fd != -1) ::close(fd);
    fd = new_fd;
}

int unique_fd::get() const{ return fd; }
explicit unique_fd::operator bool() const{ return fd != -1; }
explicit unique_fd::unique_fd(int fd) : fd(fd){}