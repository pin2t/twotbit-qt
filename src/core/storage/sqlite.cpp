#include "storage/sqlite.h"

#include <sqlite3.h>

#include <cstdio>

namespace tb {

namespace {

/// The file: URI of a path, as an immutable database is opened by.
std::string fileURI(const std::string& path) {
    std::string uri = "file:";
    for (unsigned char c : path) {
        if (c == '\\') {
            uri += '/';
        } else if (std::isalnum(c) || c == '/' || c == ':' || c == '.' || c == '-' || c == '_' || c == '~' || c >= 0x80) {
            uri += char(c);
        } else {
            char buf[4];
            std::snprintf(buf, sizeof buf, "%%%02X", c);
            uri += buf;
        }
    }
    return uri + "?immutable=1";
}

} // namespace

int64_t SQLiteDatabase::Row::integer(int column) const { return sqlite3_column_int64(stmt_, column); }

std::string SQLiteDatabase::Row::text(int column) const {
    const unsigned char* raw = sqlite3_column_text(stmt_, column);
    if (!raw) return "";
    return std::string(reinterpret_cast<const char*>(raw), size_t(sqlite3_column_bytes(stmt_, column)));
}

std::optional<Bytes> SQLiteDatabase::Row::blob(int column) const {
    if (sqlite3_column_type(stmt_, column) == SQLITE_NULL) return std::nullopt;
    const void* raw = sqlite3_column_blob(stmt_, column);
    int count = sqlite3_column_bytes(stmt_, column);
    if (!raw || count <= 0) return Bytes();
    const uint8_t* p = static_cast<const uint8_t*>(raw);
    return Bytes(p, p + count);
}

Hash256 SQLiteDatabase::Row::hash(int column) const {
    auto data = blob(column);
    if (!data) return Hash256();
    return Hash256::fromBytes(*data).value_or(Hash256());
}

bool SQLiteDatabase::Row::isNull(int column) const { return sqlite3_column_type(stmt_, column) == SQLITE_NULL; }

SQLiteDatabase::SQLiteDatabase(const std::string& path, bool immutable) {
    int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_NOMUTEX;
    std::string name = path;
    if (immutable) {
        flags = SQLITE_OPEN_READONLY | SQLITE_OPEN_URI | SQLITE_OPEN_NOMUTEX;
        name = fileURI(path);
    }
    int rc = sqlite3_open_v2(name.c_str(), &handle_, flags, nullptr);
    if (rc != SQLITE_OK) {
        std::string message = handle_ ? sqlite3_errmsg(handle_) : "cannot open database";
        sqlite3_close_v2(handle_);
        handle_ = nullptr;
        throw SQLiteError(rc, message);
    }
    sqlite3_busy_timeout(handle_, 5000);
}

SQLiteDatabase::~SQLiteDatabase() { close(); }

void SQLiteDatabase::close() {
    for (auto& [sql, stmt] : statements_) sqlite3_finalize(stmt);
    statements_.clear();
    if (handle_) sqlite3_close_v2(handle_);
    handle_ = nullptr;
}

SQLiteError SQLiteDatabase::error(int rc) const { return SQLiteError(rc, handle_ ? sqlite3_errmsg(handle_) : "database closed"); }

void SQLiteDatabase::execute(const std::string& sql) {
    if (!handle_) throw error(SQLITE_MISUSE);
    int rc = sqlite3_exec(handle_, sql.c_str(), nullptr, nullptr, nullptr);
    if (rc != SQLITE_OK) throw error(rc);
}

sqlite3_stmt* SQLiteDatabase::prepare(const std::string& sql) {
    if (!handle_) throw error(SQLITE_MISUSE);
    auto it = statements_.find(sql);
    if (it != statements_.end()) return it->second;
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(handle_, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK || !stmt) throw error(rc);
    statements_[sql] = stmt;
    return stmt;
}

void SQLiteDatabase::run(const std::string& sql, const std::vector<SQLValue>& values, const RowHandler& row) {
    sqlite3_stmt* stmt = prepare(sql);
    struct Reset {
        sqlite3_stmt* stmt;
        ~Reset() {
            sqlite3_reset(stmt);
            sqlite3_clear_bindings(stmt);
        }
    } reset{stmt};
    for (size_t i = 0; i < values.size(); ++i) {
        int index = int(i + 1);
        int rc = std::visit(
            [&](const auto& v) -> int {
                using T = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<T, int64_t>) {
                    return sqlite3_bind_int64(stmt, index, v);
                } else if constexpr (std::is_same_v<T, std::string>) {
                    return sqlite3_bind_text(stmt, index, v.data(), int(v.size()), SQLITE_TRANSIENT);
                } else if constexpr (std::is_same_v<T, Bytes>) {
                    static const uint8_t empty = 0;
                    return sqlite3_bind_blob(stmt, index, v.empty() ? &empty : v.data(), int(v.size()), SQLITE_TRANSIENT);
                } else {
                    return sqlite3_bind_null(stmt, index);
                }
            },
            values[i]);
        if (rc != SQLITE_OK) throw error(rc);
    }
    while (true) {
        int rc = sqlite3_step(stmt);
        if (rc == SQLITE_DONE) return;
        if (rc != SQLITE_ROW) throw error(rc);
        if (row) row(Row(stmt));
    }
}

std::optional<int64_t> SQLiteDatabase::scalar(const std::string& sql, const std::vector<SQLValue>& values) {
    std::optional<int64_t> result;
    run(sql, values, [&](const Row& r) { result = r.isNull(0) ? std::nullopt : std::optional<int64_t>(r.integer(0)); });
    return result;
}

int SQLiteDatabase::changes() const { return handle_ ? sqlite3_changes(handle_) : 0; }

} // namespace tb
