#include "../include/session.hpp"

std::expected <void, int> echo_session(int client_fd){
    std::array <char, BUF_SIZE> buf{};
    while(true){
        ssize_t recv_byte = ::recv(client_fd, buf.data(), buf.size(), 0);
        if(recv_byte == 0) return {};
        if(recv_byte == -1){
            int ec = errno;
            if(errno == EINTR) continue;
            return std::unexpected(ec);
        }

        ssize_t send_byte = 0;
        while(send_byte < recv_byte){
            ssize_t now = ::send(client_fd, buf.data() + send_byte, recv_byte - send_byte, MSG_NOSIGNAL);
            if(now == -1){
                int ec = errno;
                if(ec == EINTR) continue;
                return std::unexpected(ec);
            }

            if(now == 0) return std::unexpected(EPIPE);
            send_byte += now;
        }
    }
}