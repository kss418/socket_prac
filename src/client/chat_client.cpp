#include "client/chat_client.hpp"
#include "net/addr.hpp"
#include "protocol/line_parser.hpp"

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
    while(true){
        std::string s;
        if(!std::getline(std::cin, s)) return {};
        if(s.empty()) continue;

        si.send.append(command_codec::encode(command_codec::cmd_say{s}));
        auto flush_send_exp = flush_send(server_fd.get(), si);
        if(!flush_send_exp){
            handle_error("flush_send failed", flush_send_exp);
            continue;
        }

        auto recv_ret_exp = drain_recv(server_fd.get(), si);
        if(!recv_ret_exp){
            handle_error("drain_recv failed", recv_ret_exp);
            return std::unexpected(recv_ret_exp.error());
        }

        while(true){
            auto line = line_parser::parse_line(si.recv.raw());
            if(!line) break;

            auto dec_exp = command_codec::decode(*line);
            if(!dec_exp){
                handle_error("decode failed", dec_exp);
                continue;
            }

            auto cmd = std::move(*dec_exp);
            execute(cmd);
        }

        if(recv_ret_exp->closed) return {};
    }

    return {};
}

void chat_client::execute(const command_codec::command& cmd){
    std::visit([&](const auto& c){
        using T = std::decay_t<decltype(c)>;
        if constexpr (std::is_same_v<T, command_codec::cmd_say>){
            std::cout << c.text << "\n";
        }

        if constexpr (std::is_same_v<T, command_codec::cmd_nick>){

        }
    }, cmd);
}