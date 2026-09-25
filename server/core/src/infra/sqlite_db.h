#pragma once
#include <sqlite3.h>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace guard {

class DbError : public std::runtime_error {
public:
    explicit DbError(const std::string& m) : std::runtime_error(m) {}
};

// Thrown for UNIQUE / CHECK / FOREIGN KEY violations (maps to HTTP 409/400).
class DbConstraintError : public DbError {
public:
    explicit DbConstraintError(const std::string& m) : DbError(m) {}
};

// ---------------------------------------------------------------------------
// One SQLite connection (WAL, foreign keys on) shared by the whole process.
// Every repository call takes lock(); multi-statement work uses transaction().
// ---------------------------------------------------------------------------
class Db {
public:
    explicit Db(const std::string& path);
    ~Db();
    Db(const Db&) = delete;
    Db& operator=(const Db&) = delete;

    // Applies NNN_*.sql files from `dir` that are newer than the current
    // schema version. Backs the file up to "<path>.bak-v<old>" first when an
    // existing database is about to change.
    void migrate(const std::string& dir);
    int schemaVersion();

    void exec(const std::string& sql);
    int64_t lastInsertId();
    int changes();

    std::unique_lock<std::recursive_mutex> lock() { return std::unique_lock<std::recursive_mutex>(mtx_); }
    void transaction(const std::function<void()>& fn);

    sqlite3* handle() { return db_; }

private:
    void backupTo(const std::string& target);

    sqlite3* db_ = nullptr;
    std::string path_;
    std::recursive_mutex mtx_;
};

// Prepared statement with 1-based bind() and 0-based column getters.
class Stmt {
public:
    Stmt(Db& db, const std::string& sql);
    ~Stmt();
    Stmt(const Stmt&) = delete;
    Stmt& operator=(const Stmt&) = delete;

    Stmt& bind(int idx, int64_t v);
    Stmt& bind(int idx, int v) { return bind(idx, static_cast<int64_t>(v)); }
    Stmt& bind(int idx, double v);
    Stmt& bind(int idx, const std::string& v);
    Stmt& bind(int idx, const char* v) { return bind(idx, std::string(v)); }
    Stmt& bind(int idx, const std::optional<int64_t>& v);
    Stmt& bindNull(int idx);
    Stmt& bindBlob(int idx, const std::vector<float>& v);

    bool step();          // true while a row is available
    void run();           // step to completion (INSERT/UPDATE/DELETE)

    bool isNull(int col);
    int64_t i64(int col);
    double f64(int col);
    std::string text(int col);
    std::optional<int64_t> optI64(int col);
    std::vector<float> floats(int col);

private:
    sqlite3* db_;
    sqlite3_stmt* st_ = nullptr;
};

} // namespace guard
