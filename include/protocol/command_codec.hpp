#pragma once
#include "core/error_code.hpp"
#include <expected>
#include <vector>
#include <string>
#include <string_view>
#include <variant>

namespace command_codec{
    struct cmd_say{
        std::string room_id;
        std::string text;
    };
    struct cmd_nick{ std::string nick; };
    struct cmd_response{ std::string text; };
    struct cmd_login{ std::string id, pw; };
    struct cmd_register{ std::string id, pw; };
    struct cmd_friend_request{ std::string to_user_id; };
    struct cmd_friend_accept{ std::string from_user_id; };
    struct cmd_friend_reject{ std::string from_user_id; };
    struct cmd_friend_remove{ std::string friend_user_id; };
    struct cmd_list_friend{};
    struct cmd_list_friend_request{};
    struct cmd_create_room{ std::string room_name; };
    struct cmd_delete_room{ std::string room_id; };
    struct cmd_invite_room{
        std::string room_id;
        std::string friend_user_id;
    };
    struct cmd_leave_room{ std::string room_id; };
    struct cmd_list_room{};
    struct cmd_history{
        std::string room_id;
        std::string limit;
    };

    enum class decode_error : int{
        empty_line = 1,
        invalid_command,
        unexpected_argument
    };

    struct decode_info{
        std::string_view cmd;
        std::vector<std::string_view> args;
    };
    using command = std::variant<
        cmd_say,
        cmd_nick,
        cmd_response,
        cmd_login,
        cmd_register,
        cmd_friend_request,
        cmd_friend_accept,
        cmd_friend_reject,
        cmd_friend_remove,
        cmd_list_friend,
        cmd_list_friend_request,
        cmd_create_room,
        cmd_delete_room,
        cmd_invite_room,
        cmd_leave_room,
        cmd_list_room,
        cmd_history
    >;

    decode_info decode_line(std::string_view line);
    std::string_view erase_delimeter(std::string_view line);

    std::string encode(const command& cmd);
    std::expected <command, error_code> decode(std::string_view line);
    std::string decode_strerror(int code);
}
