#include "database/db_service.hpp"
#include "database/db_connector.hpp"
#include <pqxx/pqxx>
#include <cerrno>
#include <string>
#include <vector>

db_service::db_service(db_connector& connector) noexcept : connector(connector) {}

std::expected<void, error_code> db_service::ping() noexcept{
    std::lock_guard<std::mutex> lock(mtx);

    try{
        pqxx::read_transaction tx(connector.connection());
        tx.exec("SELECT 1");
        tx.commit();
        return {};
    } catch(const std::exception& ex){
        return std::unexpected(db_connector::map_exception(ex));
    }
}

std::expected<std::optional<std::string>, error_code> db_service::login(
    std::string_view id, std::string_view pw
) noexcept{
    std::lock_guard<std::mutex> lock(mtx);

    try{
        pqxx::read_transaction tx(connector.connection());
        auto rows = tx.exec(
            "SELECT nickname FROM auth.users WHERE id = $1 AND pw = $2 LIMIT 1",
            pqxx::params{id, pw}
        );
        tx.commit();
        if(rows.empty()) return std::optional<std::string>{};
        return std::optional<std::string>{rows.front().front().c_str()};
    } catch(const std::exception& ex){
        return std::unexpected(db_connector::map_exception(ex));
    }
}

std::expected<bool, error_code> db_service::signup(
    std::string_view id, std::string_view pw
) noexcept{
    std::lock_guard<std::mutex> lock(mtx);

    try{
        pqxx::work tx(connector.connection());
        auto rows = tx.exec(
            "INSERT INTO auth.users (id, pw, nickname) VALUES ($1, $2, $3) "
            "ON CONFLICT (id) DO NOTHING RETURNING id",
            pqxx::params{id, pw, "guest"}
        );
        tx.commit();
        return !rows.empty();
    } catch(const std::exception& ex){
        return std::unexpected(db_connector::map_exception(ex));
    }
}

std::expected<bool, error_code> db_service::change_nickname(
    std::string_view id, std::string_view nickname
) noexcept{
    std::lock_guard<std::mutex> lock(mtx);

    try{
        pqxx::work tx(connector.connection());
        auto rows = tx.exec(
            "UPDATE auth.users SET nickname = $2 WHERE id = $1 RETURNING id",
            pqxx::params{id, nickname}
        );
        tx.commit();
        return !rows.empty();
    } catch(const std::exception& ex){
        return std::unexpected(db_connector::map_exception(ex));
    }
}

std::expected<bool, error_code> db_service::add_friend(
    std::string_view user_id, std::string_view friend_id
) noexcept{
    std::lock_guard<std::mutex> lock(mtx);

    try{
        pqxx::work tx(connector.connection());
        auto rows = tx.exec(
            "INSERT INTO social.friendships (user_a_id, user_b_id) "
            "VALUES (LEAST($1, $2), GREATEST($1, $2)) "
            "ON CONFLICT (user_a_id, user_b_id) DO NOTHING "
            "RETURNING user_a_id",
            pqxx::params{user_id, friend_id}
        );
        tx.commit();
        return !rows.empty();
    } catch(const std::exception& ex){
        return std::unexpected(db_connector::map_exception(ex));
    }
}

std::expected<bool, error_code> db_service::remove_friend(
    std::string_view user_id, std::string_view friend_id
) noexcept{
    std::lock_guard<std::mutex> lock(mtx);

    try{
        pqxx::work tx(connector.connection());
        auto rows = tx.exec(
            "DELETE FROM social.friendships "
            "WHERE user_a_id = LEAST($1, $2) AND user_b_id = GREATEST($1, $2) "
            "RETURNING user_a_id",
            pqxx::params{user_id, friend_id}
        );
        tx.commit();
        return !rows.empty();
    } catch(const std::exception& ex){
        return std::unexpected(db_connector::map_exception(ex));
    }
}

std::expected<std::vector<std::string>, error_code> db_service::list_friends(
    std::string_view user_id
) noexcept{
    std::lock_guard<std::mutex> lock(mtx);

    try{
        pqxx::read_transaction tx(connector.connection());
        auto rows = tx.exec(
            "SELECT CASE "
            "  WHEN user_a_id = $1 THEN user_b_id "
            "  ELSE user_a_id "
            "END AS friend_id "
            "FROM social.friendships "
            "WHERE user_a_id = $1 OR user_b_id = $1 "
            "ORDER BY friend_id ASC",
            pqxx::params{user_id}
        );
        tx.commit();

        std::vector<std::string> out;
        out.reserve(rows.size());
        for(const auto& row : rows){
            out.emplace_back(row[0].c_str());
        }
        return out;
    } catch(const std::exception& ex){
        return std::unexpected(db_connector::map_exception(ex));
    }
}

std::expected<bool, error_code> db_service::request_friend(
    std::string_view from_user_id, std::string_view to_user_id
) noexcept{
    std::lock_guard<std::mutex> lock(mtx);

    try{
        pqxx::work tx(connector.connection());
        auto rows = tx.exec(
            "INSERT INTO social.friend_requests (from_user_id, to_user_id, status) "
            "SELECT $1, $2, 'pending' "
            "WHERE NOT EXISTS ("
            "  SELECT 1 FROM social.friendships "
            "  WHERE user_a_id = LEAST($1, $2) AND user_b_id = GREATEST($1, $2)"
            ") "
            "ON CONFLICT (from_user_id, to_user_id) DO UPDATE "
            "SET status = 'pending', updated_at = now() "
            "WHERE social.friend_requests.status IN ('rejected', 'canceled') "
            "RETURNING from_user_id",
            pqxx::params{from_user_id, to_user_id}
        );
        tx.commit();
        return !rows.empty();
    } catch(const std::exception& ex){
        return std::unexpected(db_connector::map_exception(ex));
    }
}

std::expected<bool, error_code> db_service::accept_friend_request(
    std::string_view from_user_id, std::string_view to_user_id
) noexcept{
    std::lock_guard<std::mutex> lock(mtx);

    try{
        pqxx::work tx(connector.connection());
        auto update_rows = tx.exec(
            "UPDATE social.friend_requests "
            "SET status = 'accepted', updated_at = now() "
            "WHERE from_user_id = $1 AND to_user_id = $2 AND status = 'pending' "
            "RETURNING from_user_id",
            pqxx::params{from_user_id, to_user_id}
        );
        if(update_rows.empty()){
            tx.commit();
            return false;
        }

        tx.exec(
            "INSERT INTO social.friendships (user_a_id, user_b_id) "
            "VALUES (LEAST($1, $2), GREATEST($1, $2)) "
            "ON CONFLICT (user_a_id, user_b_id) DO NOTHING",
            pqxx::params{from_user_id, to_user_id}
        );

        tx.commit();
        return true;
    } catch(const std::exception& ex){
        return std::unexpected(db_connector::map_exception(ex));
    }
}

std::expected<bool, error_code> db_service::reject_friend_request(
    std::string_view from_user_id, std::string_view to_user_id
) noexcept{
    std::lock_guard<std::mutex> lock(mtx);

    try{
        pqxx::work tx(connector.connection());
        auto rows = tx.exec(
            "UPDATE social.friend_requests "
            "SET status = 'rejected', updated_at = now() "
            "WHERE from_user_id = $1 AND to_user_id = $2 AND status = 'pending' "
            "RETURNING from_user_id",
            pqxx::params{from_user_id, to_user_id}
        );
        tx.commit();
        return !rows.empty();
    } catch(const std::exception& ex){
        return std::unexpected(db_connector::map_exception(ex));
    }
}

std::expected<std::vector<std::string>, error_code> db_service::list_friend_requests(
    std::string_view to_user_id
) noexcept{
    std::lock_guard<std::mutex> lock(mtx);

    try{
        pqxx::read_transaction tx(connector.connection());
        auto rows = tx.exec(
            "SELECT from_user_id "
            "FROM social.friend_requests "
            "WHERE to_user_id = $1 AND status = 'pending' "
            "ORDER BY created_at ASC",
            pqxx::params{to_user_id}
        );
        tx.commit();

        std::vector<std::string> out;
        out.reserve(rows.size());
        for(const auto& row : rows){
            out.emplace_back(row[0].c_str());
        }
        return out;
    } catch(const std::exception& ex){
        return std::unexpected(db_connector::map_exception(ex));
    }
}

std::expected<std::int64_t, error_code> db_service::create_room(
    std::string_view owner_user_id, std::string_view room_name
) noexcept{
    std::lock_guard<std::mutex> lock(mtx);

    try{
        pqxx::work tx(connector.connection());
        auto room_rows = tx.exec(
            "INSERT INTO chat.rooms (name, owner_user_id) "
            "VALUES ($1, $2) "
            "RETURNING id",
            pqxx::params{room_name, owner_user_id}
        );
        if(room_rows.empty()){
            tx.commit();
            return std::unexpected(error_code::from_errno(EIO));
        }

        std::int64_t room_id = room_rows[0][0].as<std::int64_t>();

        tx.exec(
            "INSERT INTO chat.room_members (room_id, user_id, role) "
            "VALUES ($1, $2, 'owner') "
            "ON CONFLICT (room_id, user_id) DO NOTHING",
            pqxx::params{room_id, owner_user_id}
        );

        tx.commit();
        return room_id;
    } catch(const std::exception& ex){
        return std::unexpected(db_connector::map_exception(ex));
    }
}

std::expected<bool, error_code> db_service::delete_room(
    std::string_view owner_user_id, std::int64_t room_id
) noexcept{
    std::lock_guard<std::mutex> lock(mtx);

    try{
        pqxx::work tx(connector.connection());
        auto rows = tx.exec(
            "DELETE FROM chat.rooms "
            "WHERE id = $1 AND owner_user_id = $2 "
            "RETURNING id",
            pqxx::params{room_id, owner_user_id}
        );
        tx.commit();
        return !rows.empty();
    } catch(const std::exception& ex){
        return std::unexpected(db_connector::map_exception(ex));
    }
}

std::expected<db_service::invite_room_result, error_code> db_service::invite_room(
    std::string_view inviter_user_id,
    std::int64_t room_id,
    std::string_view friend_user_id
) noexcept{
    std::lock_guard<std::mutex> lock(mtx);

    try{
        pqxx::work tx(connector.connection());

        auto inviter_member_rows = tx.exec(
            "SELECT 1 "
            "FROM chat.room_members "
            "WHERE room_id = $1 AND user_id = $2 "
            "LIMIT 1",
            pqxx::params{room_id, inviter_user_id}
        );
        if(inviter_member_rows.empty()){
            tx.commit();
            return invite_room_result::room_not_found_or_no_permission;
        }

        auto friendship_rows = tx.exec(
            "SELECT 1 "
            "FROM social.friendships "
            "WHERE user_a_id = LEAST($1, $2) AND user_b_id = GREATEST($1, $2) "
            "LIMIT 1",
            pqxx::params{inviter_user_id, friend_user_id}
        );
        if(friendship_rows.empty()){
            tx.commit();
            return invite_room_result::not_friend;
        }

        auto insert_rows = tx.exec(
            "INSERT INTO chat.room_members (room_id, user_id, role) "
            "VALUES ($1, $2, 'member') "
            "ON CONFLICT (room_id, user_id) DO NOTHING "
            "RETURNING user_id",
            pqxx::params{room_id, friend_user_id}
        );

        tx.commit();
        if(insert_rows.empty()) return invite_room_result::already_member;
        return invite_room_result::invited;
    } catch(const std::exception& ex){
        return std::unexpected(db_connector::map_exception(ex));
    }
}

std::expected<std::optional<std::int64_t>, error_code> db_service::create_room_message(
    std::int64_t room_id,
    std::string_view sender_user_id,
    std::string_view body
) noexcept{
    std::lock_guard<std::mutex> lock(mtx);

    try{
        pqxx::work tx(connector.connection());
        auto rows = tx.exec(
            "INSERT INTO chat.messages (room_id, sender_user_id, body) "
            "SELECT $1, $2, $3 "
            "WHERE EXISTS ("
            "  SELECT 1 FROM chat.room_members "
            "  WHERE room_id = $1 AND user_id = $2"
            ") "
            "RETURNING id",
            pqxx::params{room_id, sender_user_id, body}
        );
        tx.commit();
        if(rows.empty()) return std::optional<std::int64_t>{};
        return std::optional<std::int64_t>{rows[0][0].as<std::int64_t>()};
    } catch(const std::exception& ex){
        return std::unexpected(db_connector::map_exception(ex));
    }
}

std::expected<db_service::leave_room_result, error_code> db_service::leave_room(
    std::string_view user_id,
    std::int64_t room_id
) noexcept{
    std::lock_guard<std::mutex> lock(mtx);

    try{
        pqxx::work tx(connector.connection());

        auto owner_rows = tx.exec(
            "SELECT owner_user_id "
            "FROM chat.rooms "
            "WHERE id = $1 "
            "LIMIT 1",
            pqxx::params{room_id}
        );
        if(owner_rows.empty()){
            tx.commit();
            return leave_room_result::not_member_or_room_not_found;
        }

        const std::string owner_user_id = owner_rows[0][0].c_str();
        if(owner_user_id == user_id){
            tx.commit();
            return leave_room_result::owner_cannot_leave;
        }

        auto leave_rows = tx.exec(
            "DELETE FROM chat.room_members "
            "WHERE room_id = $1 AND user_id = $2 "
            "RETURNING room_id",
            pqxx::params{room_id, user_id}
        );

        tx.commit();
        if(leave_rows.empty()) return leave_room_result::not_member_or_room_not_found;
        return leave_room_result::left;
    } catch(const std::exception& ex){
        return std::unexpected(db_connector::map_exception(ex));
    }
}

std::expected<std::vector<db_service::room_info>, error_code> db_service::list_rooms(
    std::string_view user_id
) noexcept{
    std::lock_guard<std::mutex> lock(mtx);

    try{
        pqxx::read_transaction tx(connector.connection());
        auto rows = tx.exec(
            "SELECT r.id, r.name, r.owner_user_id, COUNT(all_m.user_id)::BIGINT AS member_count "
            "FROM chat.rooms r "
            "JOIN chat.room_members scope_m "
            "  ON scope_m.room_id = r.id AND scope_m.user_id = $1 "
            "LEFT JOIN chat.room_members all_m ON all_m.room_id = r.id "
            "GROUP BY r.id, r.name, r.owner_user_id "
            "ORDER BY r.id ASC",
            pqxx::params{user_id}
        );
        tx.commit();

        std::vector<room_info> out;
        out.reserve(rows.size());
        for(const auto& row : rows){
            room_info info{};
            info.id = row[0].as<std::int64_t>();
            info.name = row[1].c_str();
            info.owner_user_id = row[2].c_str();
            info.member_count = row[3].as<std::int64_t>();
            out.push_back(std::move(info));
        }
        return out;
    } catch(const std::exception& ex){
        return std::unexpected(db_connector::map_exception(ex));
    }
}
