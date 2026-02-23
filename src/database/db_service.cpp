#include "database/db_service.hpp"
#include "database/db_connector.hpp"
#include <pqxx/pqxx>
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
