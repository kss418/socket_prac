#include "database/db_service.hpp"
#include "database/db_connector.hpp"
#include <pqxx/pqxx>
#include <string>

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
