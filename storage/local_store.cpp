#include "local_store.hpp"

#include <sqlite3.h>
#include <array>
#include <iomanip>
#include <memory>
#include <sstream>
#include <stdexcept>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>
#else
#include <openssl/sha.h>
#endif

namespace {
using Statement = std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)>;

void check(sqlite3* db, int result) {
    if (result != SQLITE_OK && result != SQLITE_DONE && result != SQLITE_ROW)
        throw std::runtime_error(sqlite3_errmsg(db));
}

void execute(sqlite3* db, const char* sql) {
    check(db, sqlite3_exec(db, sql, nullptr, nullptr, nullptr));
}

Statement prepare(sqlite3* db, const char* sql) {
    sqlite3_stmt* statement = nullptr;
    check(db, sqlite3_prepare_v2(db, sql, -1, &statement, nullptr));
    return Statement(statement, sqlite3_finalize);
}

void bind(sqlite3* db, sqlite3_stmt* statement, int index, const std::string& value) {
    check(db, sqlite3_bind_text(statement, index, value.data(),
                              static_cast<int>(value.size()), SQLITE_TRANSIENT));
}

std::string text(sqlite3_stmt* statement, int column) {
    const auto* value = sqlite3_column_text(statement, column);
    return value ? reinterpret_cast<const char*>(value) : "";
}

std::string sha256(const std::string& password) {
    std::array<unsigned char, 32> digest{};
#ifdef _WIN32
    struct Provider {
        BCRYPT_ALG_HANDLE handle = nullptr;
        ~Provider() { if (handle) BCryptCloseAlgorithmProvider(handle, 0); }
    } provider;
    if (BCryptOpenAlgorithmProvider(&provider.handle, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0 ||
        BCryptHash(provider.handle, nullptr, 0,
                   reinterpret_cast<PUCHAR>(const_cast<char*>(password.data())),
                   static_cast<ULONG>(password.size()), digest.data(),
                   static_cast<ULONG>(digest.size())) < 0)
        throw std::runtime_error("Could not calculate password digest.");
#else
    if (!SHA256(reinterpret_cast<const unsigned char*>(password.data()), password.size(), digest.data()))
        throw std::runtime_error("Could not calculate password digest.");
#endif
    std::ostringstream output;
    output << std::hex << std::setfill('0');
    for (const auto byte : digest) output << std::setw(2) << int(byte);
    return output.str();
}
}

LocalStore::LocalStore(const std::filesystem::path& database) {
    if (!database.parent_path().empty()) std::filesystem::create_directories(database.parent_path());
    const auto filename = database.u8string();
    const int opened = sqlite3_open(reinterpret_cast<const char*>(filename.c_str()), &db);
    try {
        check(db, opened);
        execute(db, "PRAGMA foreign_keys = ON;");
        execute(db,
            "CREATE TABLE IF NOT EXISTS accounts ("
            "username TEXT PRIMARY KEY NOT NULL, password_sha256 TEXT NOT NULL);"
            "CREATE TABLE IF NOT EXISTS games ("
            "game_id INTEGER PRIMARY KEY, date TEXT NOT NULL, result TEXT NOT NULL,"
            "white TEXT NOT NULL, black TEXT NOT NULL, mainline_history TEXT NOT NULL);"
            "CREATE TABLE IF NOT EXISTS player_games ("
            "username TEXT NOT NULL REFERENCES accounts(username),"
            "game_id INTEGER NOT NULL REFERENCES games(game_id),"
            "side INTEGER NOT NULL CHECK(side IN (0, 1)), PRIMARY KEY(username, game_id));");
    } catch (...) {
        sqlite3_close(db);
        db = nullptr;
        throw;
    }
}

LocalStore::~LocalStore() { sqlite3_close(db); }

bool LocalStore::authenticate(const std::string& username, const std::string& password, bool sign_up) {
    if (username.empty() || password.empty() || username.size() > 40 || password.size() > 40) return false;
    const auto digest = sha256(password);
    if (sign_up) {
        auto statement = prepare(db,
            "INSERT INTO accounts(username, password_sha256) VALUES(?, ?) "
            "ON CONFLICT(username) DO NOTHING;");
        bind(db, statement.get(), 1, username);
        bind(db, statement.get(), 2, digest);
        check(db, sqlite3_step(statement.get()));
        return sqlite3_changes(db) == 1;
    }
    auto statement = prepare(db, "SELECT password_sha256 FROM accounts WHERE username = ?;");
    bind(db, statement.get(), 1, username);
    const int result = sqlite3_step(statement.get());
    check(db, result);
    return result == SQLITE_ROW && text(statement.get(), 0) == digest;
}

std::int64_t LocalStore::save_game(const std::string& username, int player_side, const SavedGame& game) {
    if (game.mainline.empty()) throw std::invalid_argument("Cannot save an empty game.");
    std::ostringstream history;
    for (const Move& move : game.mainline) {
        history << move.old_x << ' ' << move.old_y << ' ' << move.new_x << ' ' << move.new_y
                << ' ' << static_cast<int>(move.special_move) << '\n';
    }
    execute(db, "BEGIN;");
    try {
        auto game_statement = prepare(db,
            "INSERT INTO games(date, result, white, black, mainline_history) VALUES(?, ?, ?, ?, ?);");
        bind(db, game_statement.get(), 1, game.date);
        bind(db, game_statement.get(), 2, game.result);
        bind(db, game_statement.get(), 3, game.white);
        bind(db, game_statement.get(), 4, game.black);
        bind(db, game_statement.get(), 5, history.str());
        check(db, sqlite3_step(game_statement.get()));
        const auto id = sqlite3_last_insert_rowid(db);
        auto player_statement = prepare(db,
            "INSERT INTO player_games(username, game_id, side) VALUES(?, ?, ?);");
        bind(db, player_statement.get(), 1, username);
        check(db, sqlite3_bind_int64(player_statement.get(), 2, id));
        check(db, sqlite3_bind_int(player_statement.get(), 3, player_side));
        check(db, sqlite3_step(player_statement.get()));
        execute(db, "COMMIT;");
        return id;
    } catch (...) {
        sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
        throw;
    }
}

std::vector<SavedGame> LocalStore::games(const std::string& username) const {
    auto statement = prepare(db,
        "SELECT g.game_id, p.side, g.white, g.black, g.result, g.date, g.mainline_history "
        "FROM player_games p JOIN games g ON g.game_id = p.game_id "
        "WHERE p.username = ? ORDER BY g.game_id DESC;");
    bind(db, statement.get(), 1, username);
    std::vector<SavedGame> saved;
    int result = SQLITE_OK;
    while ((result = sqlite3_step(statement.get())) == SQLITE_ROW) {
        SavedGame game;
        game.game_id = sqlite3_column_int64(statement.get(), 0);
        game.player_side = sqlite3_column_int(statement.get(), 1);
        game.white = text(statement.get(), 2);
        game.black = text(statement.get(), 3);
        game.result = text(statement.get(), 4);
        game.date = text(statement.get(), 5);
        std::istringstream history(text(statement.get(), 6));
        while (!(history >> std::ws).eof()) {
            Move move;
            int special = 0;
            if (!(history >> move.old_x >> move.old_y >> move.new_x >> move.new_y >> special) ||
                move.old_x < 0 || move.old_x >= 8 || move.old_y < 0 || move.old_y >= 8 ||
                move.new_x < 0 || move.new_x >= 8 || move.new_y < 0 || move.new_y >= 8 ||
                special < 0 || special > static_cast<int>(SpecialMove::PromoteKnight))
                throw std::runtime_error("Invalid saved move history.");
            move.special_move = static_cast<SpecialMove>(special);
            game.mainline.push_back(move);
        }
        saved.push_back(std::move(game));
    }
    check(db, result);
    return saved;
}
