#include "infra/sqlite_db.h"
#include "common/log.h"
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace guard {

namespace {

[[noreturn]] void fail(sqlite3* db, int rc, const std::string& ctx) {
    std::string msg = ctx + ": " + (db ? sqlite3_errmsg(db) : sqlite3_errstr(rc));
    if ((rc & 0xff) == SQLITE_CONSTRAINT) throw DbConstraintError(msg);
    throw DbError(msg);
}

} // namespace

// ---------------------------------------------------------------- Db

Db::Db(const std::string& path) : path_(path) {
    if (int rc = sqlite3_open_v2(path.c_str(), &db_,
                                 SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr);
        rc != SQLITE_OK)
        fail(db_, rc, "open " + path);
    sqlite3_busy_timeout(db_, 5000);
    exec("PRAGMA journal_mode=WAL; PRAGMA foreign_keys=ON; PRAGMA synchronous=NORMAL;");
}

Db::~Db() { sqlite3_close_v2(db_); }

void Db::exec(const std::string& sql) {
    auto lk = lock();
    char* err = nullptr;
    if (sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &err) != SQLITE_OK) {
        std::string msg = err ? err : "unknown error";
        sqlite3_free(err);
        throw DbError("exec failed: " + msg);
    }
}

int64_t Db::lastInsertId() { return sqlite3_last_insert_rowid(db_); }
int Db::changes() { return sqlite3_changes(db_); }

void Db::transaction(const std::function<void()>& fn) {
    auto lk = lock();
    exec("BEGIN IMMEDIATE");
    try {
        fn();
        exec("COMMIT");
    } catch (...) {
        exec("ROLLBACK");
        throw;
    }
}

int Db::schemaVersion() {
    auto lk = lock();
    exec("CREATE TABLE IF NOT EXISTS schema_migrations ("
         "version INTEGER PRIMARY KEY, name TEXT NOT NULL, applied_at TEXT NOT NULL DEFAULT (datetime('now')))");
    Stmt st(*this, "SELECT COALESCE(MAX(version), 0) FROM schema_migrations");
    st.step();
    return static_cast<int>(st.i64(0));
}

void Db::backupTo(const std::string& target) {
    sqlite3* dst = nullptr;
    if (sqlite3_open(target.c_str(), &dst) != SQLITE_OK) fail(dst, SQLITE_ERROR, "open backup " + target);
    sqlite3_backup* b = sqlite3_backup_init(dst, "main", db_, "main");
    if (b) {
        sqlite3_backup_step(b, -1);
        sqlite3_backup_finish(b);
    }
    int rc = sqlite3_errcode(dst);
    sqlite3_close(dst);
    if (rc != SQLITE_OK) throw DbError("backup to " + target + " failed");
}

void Db::migrate(const std::string& dir) {
    auto lk = lock();
    const int current = schemaVersion();

    std::vector<std::pair<int, fs::path>> files;
    if (!fs::is_directory(dir)) throw DbError("migrations directory not found: " + dir);
    for (const auto& e : fs::directory_iterator(dir)) {
        const std::string name = e.path().filename().string();
        if (e.path().extension() != ".sql" || name.size() < 4 || !std::isdigit(name[0])) continue;
        files.emplace_back(std::stoi(name.substr(0, name.find('_'))), e.path());
    }
    std::sort(files.begin(), files.end());

    bool backedUp = false;
    for (const auto& [version, file] : files) {
        if (version <= current) continue;
        if (current > 0 && !backedUp) {
            const std::string bak = path_ + ".bak-v" + std::to_string(current);
            backupTo(bak);
            LOG(Info) << "Database backed up to " << bak;
            backedUp = true;
        }
        std::ifstream in(file);
        std::stringstream sql;
        sql << in.rdbuf();
        transaction([&] {
            exec(sql.str());
            Stmt st(*this, "INSERT INTO schema_migrations(version, name) VALUES(?, ?)");
            st.bind(1, version).bind(2, file.filename().string()).run();
        });
        LOG(Info) << "Applied migration " << file.filename().string();
    }
}

// ---------------------------------------------------------------- Stmt

Stmt::Stmt(Db& db, const std::string& sql) : db_(db.handle()) {
    if (int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &st_, nullptr); rc != SQLITE_OK)
        fail(db_, rc, "prepare");
}

Stmt::~Stmt() { sqlite3_finalize(st_); }

Stmt& Stmt::bind(int idx, int64_t v) { sqlite3_bind_int64(st_, idx, v); return *this; }
Stmt& Stmt::bind(int idx, double v) { sqlite3_bind_double(st_, idx, v); return *this; }
Stmt& Stmt::bind(int idx, const std::string& v) {
    sqlite3_bind_text(st_, idx, v.data(), static_cast<int>(v.size()), SQLITE_TRANSIENT);
    return *this;
}
Stmt& Stmt::bind(int idx, const std::optional<int64_t>& v) { return v ? bind(idx, *v) : bindNull(idx); }
Stmt& Stmt::bindNull(int idx) { sqlite3_bind_null(st_, idx); return *this; }
Stmt& Stmt::bindBlob(int idx, const std::vector<float>& v) {
    sqlite3_bind_blob(st_, idx, v.data(), static_cast<int>(v.size() * sizeof(float)), SQLITE_TRANSIENT);
    return *this;
}

bool Stmt::step() {
    int rc = sqlite3_step(st_);
    if (rc == SQLITE_ROW) return true;
    if (rc == SQLITE_DONE) return false;
    fail(db_, rc, "step");
}

void Stmt::run() { while (step()) {} }

bool Stmt::isNull(int col) { return sqlite3_column_type(st_, col) == SQLITE_NULL; }
int64_t Stmt::i64(int col) { return sqlite3_column_int64(st_, col); }
double Stmt::f64(int col) { return sqlite3_column_double(st_, col); }
std::string Stmt::text(int col) {
    const unsigned char* t = sqlite3_column_text(st_, col);
    return t ? std::string(reinterpret_cast<const char*>(t), sqlite3_column_bytes(st_, col)) : std::string();
}
std::optional<int64_t> Stmt::optI64(int col) {
    return isNull(col) ? std::nullopt : std::optional<int64_t>(i64(col));
}
std::vector<float> Stmt::floats(int col) {
    const void* p = sqlite3_column_blob(st_, col);
    const int bytes = sqlite3_column_bytes(st_, col);
    std::vector<float> v(bytes / sizeof(float));
    if (p && bytes) std::memcpy(v.data(), p, v.size() * sizeof(float));
    return v;
}

} // namespace guard
