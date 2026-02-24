#pragma once
#include "core/error_code.hpp"
#include <expected>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <cstdint>
#include <vector>

class db_connector;

class db_service{
public:
    struct message_info{
        std::int64_t id{};
        std::string sender_user_id;
        std::string body;
        std::string created_at;
    };

    struct room_info{
        std::int64_t id{};
        std::string name;
        std::string owner_user_id;
        std::int64_t member_count{};
    };
    enum class invite_room_result{
        invited = 0,
        already_member,
        not_friend,
        room_not_found_or_no_permission
    };
    enum class leave_room_result{
        left = 0,
        not_member_or_room_not_found,
        owner_cannot_leave
    };

private:
    db_connector& connector;
    std::mutex mtx;

public:
    explicit db_service(db_connector& connector) noexcept;

    db_service(const db_service&) = delete;
    db_service& operator=(const db_service&) = delete;
    db_service(db_service&&) = delete;
    db_service& operator=(db_service&&) = delete;

    std::expected<void, error_code> ping() noexcept;
    std::expected<std::optional<std::string>, error_code> login(
        std::string_view id, std::string_view pw
    ) noexcept;
    std::expected<bool, error_code> signup(std::string_view id, std::string_view pw) noexcept;
    std::expected<bool, error_code> change_nickname(
        std::string_view id, std::string_view nickname
    ) noexcept;
    std::expected<bool, error_code> add_friend(
        std::string_view user_id, std::string_view friend_id
    ) noexcept;
    std::expected<bool, error_code> remove_friend(
        std::string_view user_id, std::string_view friend_id
    ) noexcept;
    std::expected<std::vector<std::string>, error_code> list_friends(
        std::string_view user_id
    ) noexcept;
    std::expected<bool, error_code> request_friend(
        std::string_view from_user_id, std::string_view to_user_id
    ) noexcept;
    std::expected<bool, error_code> accept_friend_request(
        std::string_view from_user_id, std::string_view to_user_id
    ) noexcept;
    std::expected<bool, error_code> reject_friend_request(
        std::string_view from_user_id, std::string_view to_user_id
    ) noexcept;
    std::expected<std::vector<std::string>, error_code> list_friend_requests(
        std::string_view to_user_id
    ) noexcept;
    std::expected<std::int64_t, error_code> create_room(
        std::string_view owner_user_id, std::string_view room_name
    ) noexcept;
    std::expected<bool, error_code> delete_room(
        std::string_view owner_user_id, std::int64_t room_id
    ) noexcept;
    std::expected<invite_room_result, error_code> invite_room(
        std::string_view inviter_user_id,
        std::int64_t room_id,
        std::string_view friend_user_id
    ) noexcept;
    std::expected<std::optional<std::int64_t>, error_code> create_room_message(
        std::int64_t room_id,
        std::string_view sender_user_id,
        std::string_view body
    ) noexcept;
    std::expected<leave_room_result, error_code> leave_room(
        std::string_view user_id,
        std::int64_t room_id
    ) noexcept;
    std::expected<std::vector<room_info>, error_code> list_rooms(
        std::string_view user_id
    ) noexcept;
    std::expected<std::optional<std::vector<message_info>>, error_code> list_room_messages(
        std::string_view user_id,
        std::int64_t room_id,
        std::int32_t limit
    ) noexcept;
};
