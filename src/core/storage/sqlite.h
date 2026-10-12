#pragma once

#include <cstddef>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "util/bytes.h"

struct sqlite3;
struct sqlite3_stmt;

namespace tb {

class SQLiteError : public Error {
public:
    SQLiteError(int code, const std::string& message) : Error(message + " (SQLite error " + std::to_string(code) + ")"), code(code), message(message) {}

    int code;
    std::string message;
};

/// SQLValue is one value bound to a statement parameter.
using SQLValue = std::variant<int64_t, std::string, Bytes, std::nullptr_t>;

/// SQLiteDatabase is one connection to an SQLite database file. It is not
/// thread safe itself: the Store holds it under a lock and serializes every
/// use of it, which is what makes it safe to share.
class SQLiteDatabase {
public:
    /// Row reads the columns of the current result row.
    class Row {
    public:
        explicit Row(sqlite3_stmt* stmt) : stmt_(stmt) {}
        int64_t integer(int column) const;
        std::string text(int column) const;
        /// The blob, or nothing for NULL.
        std::optional<Bytes> blob(int column) const;
        Hash256 hash(int column) const;
        bool isNull(int column) const;

    private:
        sqlite3_stmt* stmt_;
    };

    using RowHandler = std::function<void(const Row&)>;

    /// Opens the database file. An immutable database is only read, and
    /// SQLite touches nothing of the file then, not even its locks.
    explicit SQLiteDatabase(const std::string& path, bool immutable = false);
    ~SQLiteDatabase();
    SQLiteDatabase(const SQLiteDatabase&) = delete;
    SQLiteDatabase& operator=(const SQLiteDatabase&) = delete;

    void close();

    /// Runs SQL statements that return no rows.
    void execute(const std::string& sql);

    /// Runs one statement with the values bound and calls row for every
    /// result row. The prepared statement is cached for the next call.
    void run(const std::string& sql, const std::vector<SQLValue>& values = {}, const RowHandler& row = nullptr);

    /// The single integer the query returns, or nothing without a row or for
    /// NULL.
    std::optional<int64_t> scalar(const std::string& sql, const std::vector<SQLValue>& values = {});

    /// The number of rows the last statement changed.
    int changes() const;

    /// Runs the body in a transaction, committed when it returns and rolled
    /// back when it throws.
    template <class F>
    auto transaction(F body) -> decltype(body()) {
        execute("BEGIN IMMEDIATE");
        try {
            if constexpr (std::is_void_v<decltype(body())>) {
                body();
                execute("COMMIT");
            } else {
                auto result = body();
                execute("COMMIT");
                return result;
            }
        } catch (...) {
            try {
                execute("ROLLBACK");
            } catch (...) {
            }
            throw;
        }
    }

private:
    sqlite3_stmt* prepare(const std::string& sql);
    SQLiteError error(int rc) const;

    sqlite3* handle_ = nullptr;
    std::map<std::string, sqlite3_stmt*> statements_;
};

} // namespace tb
