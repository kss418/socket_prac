#include "client/chat_io_worker.hpp"
#include "protocol/line_parser.hpp"
#include <array>
#include <cerrno>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

chat_io_worker::chat_io_worker(
    socket_info& si, unique_fd& server_fd, chat_executor& executor
) : si(si), server_fd(server_fd), executor(executor){}

std::expected<void, error_code> chat_io_worker::run(std::stop_token stop_token){
    std::stop_callback on_stop(stop_token, [this](){
        ::shutdown(server_fd.get(), SHUT_RDWR);
    });

    std::array<pollfd, 2> fds{{
        { .fd = STDIN_FILENO, .events = POLLIN, .revents = 0 },
        { .fd = server_fd.get(), .events = POLLIN | POLLHUP | POLLERR, .revents = 0 }
    }};

    while(!stop_token.stop_requested()){
        int size = ::poll(fds.data(), fds.size(), -1);
        if(size < 0){
            int ec = errno;
            if(ec == EINTR) continue;
            return std::unexpected(error_code::from_errno(ec));
        }

        if(fds[0].revents & (POLLIN | POLLHUP)){
            auto send_exp = send_stdin();
            if(!send_exp) return std::unexpected(send_exp.error());
            if(*send_exp) break;
        }

        if(fds[1].revents & POLLERR){
            int so_error = 0;
            socklen_t len = sizeof(so_error);
            if(::getsockopt(server_fd.get(), SOL_SOCKET, SO_ERROR, &so_error, &len) == -1){
                return std::unexpected(error_code::from_errno(errno));
            }

            if(so_error == 0) so_error = EIO;
            return std::unexpected(error_code::from_errno(so_error));
        }

        if(fds[1].revents & (POLLIN | POLLHUP)){
            auto recv_exp = recv_socket();
            if(!recv_exp) return std::unexpected(recv_exp.error());

            while(auto line = line_parser::parse_line(si.recv.raw())){
                auto dec_exp = command_codec::decode(*line);
                if(!dec_exp){
                    handle_error("chat_io_worker/decode failed", dec_exp);
                    continue;
                }

                executor.request_execute(std::move(*dec_exp));
            }

            if(*recv_exp) break;
        }

        fds[0].revents = 0;
        fds[1].revents = 0;
    }

    return {};
}

std::expected<bool, error_code> chat_io_worker::recv_socket(){
    auto dr_exp = drain_recv(server_fd.get(), si);
    if(!dr_exp) return std::unexpected(dr_exp.error());
    return dr_exp->closed;
}

std::expected<bool, error_code> chat_io_worker::send_stdin(){
    char tmp[1024];
    ssize_t recv_byte = ::read(STDIN_FILENO, tmp, sizeof(tmp));
    if(recv_byte == 0) return true;

    if(recv_byte < 0){
        int ec = errno;
        if(ec == EINTR) return false;
        return std::unexpected(error_code::from_errno(ec));
    }

    stdin_buf.append(tmp, static_cast<std::size_t>(recv_byte));
    while(auto line = line_parser::parse_line(stdin_buf)){
        if(line->empty()) continue;

        si.send.append(command_codec::cmd_say{*line});
        auto fs_exp = flush_send(server_fd.get(), si);
        if(!fs_exp) return std::unexpected(fs_exp.error());
    }

    return false;
}
