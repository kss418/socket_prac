#include "protocol/command_codec.hpp"
#include <type_traits>

std::string_view command_codec::erase_delimeter(std::string_view line){
    if(!line.empty() && line.back() == '\n') line.remove_suffix(1);
    return line;
}

std::string command_codec::decode_strerror(int code){
    if(code == static_cast<int>(decode_error::empty_line)) return "empty_line";
    else if(code == static_cast<int>(decode_error::invalid_command)) return "invalid_command";
    else if(code == static_cast<int>(decode_error::unexpected_argument)) return "unexpected_argument";
    return "unknown_decode_error";
}

command_codec::decode_info command_codec::decode_line(std::string_view line){
    decode_info info{};
    std::size_t start = 0;

    while(start < line.size()){
        std::size_t pos = line.find('\r', start);
        if(pos == std::string_view::npos){
            std::string_view tail = line.substr(start);
            if(!tail.empty()){
                if(info.cmd.empty()) info.cmd = tail;
                else info.args.push_back(tail);
            }
            break;
        }

        std::string_view token = line.substr(start, pos - start);
        if(!token.empty()){
            if(info.cmd.empty()) info.cmd = token;
            else info.args.push_back(token);
        }

        start = pos + 1;
        if(start < line.size() && line[start] == '\n') ++start;
    }
    
    return info;
}

std::string command_codec::encode(const command& cmd){
    return std::visit([](const auto& c) -> std::string {
        using T = std::decay_t<decltype(c)>;
        if constexpr (std::is_same_v<T, cmd_nick>) return "nick\r" + c.nick + "\n";
        if constexpr (std::is_same_v<T, cmd_say>) return "say\r" + c.room_id + "\r" + c.text + "\n";
        if constexpr (std::is_same_v<T, cmd_response>) return "response\r" + c.text + "\n";
        if constexpr (std::is_same_v<T, cmd_login>) return "login\r" + c.id + "\r" + c.pw + "\n";
        if constexpr (std::is_same_v<T, cmd_register>) return "register\r" + c.id + "\r" + c.pw + "\n";
        if constexpr (std::is_same_v<T, cmd_friend_request>) return "friend_request\r" + c.to_user_id + "\n";
        if constexpr (std::is_same_v<T, cmd_friend_accept>) return "friend_accept\r" + c.from_user_id + "\n";
        if constexpr (std::is_same_v<T, cmd_friend_reject>) return "friend_reject\r" + c.from_user_id + "\n";
        if constexpr (std::is_same_v<T, cmd_friend_remove>) return "friend_remove\r" + c.friend_user_id + "\n";
        if constexpr (std::is_same_v<T, cmd_list_friend>) return "list_friend\n";
        if constexpr (std::is_same_v<T, cmd_list_friend_request>) return "list_friend_request\n";
        if constexpr (std::is_same_v<T, cmd_create_room>) return "create_room\r" + c.room_name + "\n";
        if constexpr (std::is_same_v<T, cmd_delete_room>) return "delete_room\r" + c.room_id + "\n";
        if constexpr (std::is_same_v<T, cmd_invite_room>) return "invite_room\r" + c.room_id + "\r" + c.friend_user_id + "\n";
        if constexpr (std::is_same_v<T, cmd_leave_room>) return "leave_room\r" + c.room_id + "\n";
        if constexpr (std::is_same_v<T, cmd_list_room>) return "list_room\n";
        if constexpr (std::is_same_v<T, cmd_history>) return "history\r" + c.room_id + "\r" + c.limit + "\n";
    }, cmd);    
}

std::expected <command_codec::command, error_code> command_codec::decode(std::string_view line){
    line = erase_delimeter(line);
    if(line.empty()) return std::unexpected(error_code::from_decode(decode_error::empty_line));

    decode_info info = decode_line(line);
    if(info.cmd.empty()){
        return std::unexpected(error_code::from_decode(decode_error::empty_line));
    }

    if(info.cmd == "say"){
        if(info.args.size() != 2){
            return std::unexpected(error_code::from_decode(decode_error::unexpected_argument));
        }
        return cmd_say{std::string(info.args[0]), std::string(info.args[1])};
    }

    if(info.cmd == "nick"){
        if(info.args.size() != 1){
            return std::unexpected(error_code::from_decode(decode_error::unexpected_argument));
        }
        return cmd_nick{std::string(info.args[0])};
    }

    if(info.cmd == "response"){
        if(info.args.size() != 1){
            return std::unexpected(error_code::from_decode(decode_error::unexpected_argument));
        }
        return cmd_response{std::string(info.args[0])};
    }

    if(info.cmd == "login"){
        if(info.args.size() != 2){
            return std::unexpected(error_code::from_decode(decode_error::unexpected_argument));
        }
        return cmd_login{std::string(info.args[0]), std::string(info.args[1])};
    }

    if(info.cmd == "register"){
        if(info.args.size() != 2){
            return std::unexpected(error_code::from_decode(decode_error::unexpected_argument));
        }
        return cmd_register{std::string(info.args[0]), std::string(info.args[1])};
    }

    if(info.cmd == "friend_request"){
        if(info.args.size() != 1){
            return std::unexpected(error_code::from_decode(decode_error::unexpected_argument));
        }
        return cmd_friend_request{std::string(info.args[0])};
    }

    if(info.cmd == "friend_accept"){
        if(info.args.size() != 1){
            return std::unexpected(error_code::from_decode(decode_error::unexpected_argument));
        }
        return cmd_friend_accept{std::string(info.args[0])};
    }

    if(info.cmd == "friend_reject"){
        if(info.args.size() != 1){
            return std::unexpected(error_code::from_decode(decode_error::unexpected_argument));
        }
        return cmd_friend_reject{std::string(info.args[0])};
    }

    if(info.cmd == "friend_remove"){
        if(info.args.size() != 1){
            return std::unexpected(error_code::from_decode(decode_error::unexpected_argument));
        }
        return cmd_friend_remove{std::string(info.args[0])};
    }

    if(info.cmd == "list_friend"){
        if(!info.args.empty()){
            return std::unexpected(error_code::from_decode(decode_error::unexpected_argument));
        }
        return cmd_list_friend{};
    }

    if(info.cmd == "list_friend_request"){
        if(!info.args.empty()){
            return std::unexpected(error_code::from_decode(decode_error::unexpected_argument));
        }
        return cmd_list_friend_request{};
    }

    if(info.cmd == "create_room"){
        if(info.args.size() != 1){
            return std::unexpected(error_code::from_decode(decode_error::unexpected_argument));
        }
        return cmd_create_room{std::string(info.args[0])};
    }

    if(info.cmd == "delete_room"){
        if(info.args.size() != 1){
            return std::unexpected(error_code::from_decode(decode_error::unexpected_argument));
        }
        return cmd_delete_room{std::string(info.args[0])};
    }

    if(info.cmd == "invite_room"){
        if(info.args.size() != 2){
            return std::unexpected(error_code::from_decode(decode_error::unexpected_argument));
        }
        return cmd_invite_room{std::string(info.args[0]), std::string(info.args[1])};
    }

    if(info.cmd == "leave_room"){
        if(info.args.size() != 1){
            return std::unexpected(error_code::from_decode(decode_error::unexpected_argument));
        }
        return cmd_leave_room{std::string(info.args[0])};
    }

    if(info.cmd == "list_room"){
        if(!info.args.empty()){
            return std::unexpected(error_code::from_decode(decode_error::unexpected_argument));
        }
        return cmd_list_room{};
    }

    if(info.cmd == "history"){
        if(info.args.size() != 2){
            return std::unexpected(error_code::from_decode(decode_error::unexpected_argument));
        }
        return cmd_history{std::string(info.args[0]), std::string(info.args[1])};
    }

    return std::unexpected(error_code::from_decode(decode_error::invalid_command));
}
