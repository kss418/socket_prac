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

std::expected<bool, error_code> db_service::login(
    std::string_view id, std::string_view pw
) noexcept{
    std::lock_guard<std::mutex> lock(mtx);

    try{
        pqxx::read_transaction tx(connector.connection());
        auto rows = tx.exec(
            "SELECT 1 FROM auth.users WHERE id = $1 AND pw = $2 LIMIT 1",
            pqxx::params{id, pw}
        );
        tx.commit();
        return !rows.empty();
    } catch(const std::exception& ex){
        return std::unexpected(db_connector::map_exception(ex));
    }
}
