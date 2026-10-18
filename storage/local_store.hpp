#pragma once

#include "../board.hpp"
#include <filesystem>
#include <string>
#include <vector>

struct sqlite3;

struct SavedGame {
    std::int64_t game_id = 0;
    int player_side = -1;
    std::string white;
    std::string black;
    std::string result;
    std::string date;
    std::vector<Move> mainline;
};

class LocalStore {
public:
    explicit LocalStore(const std::filesystem::path& database);
    ~LocalStore();
    LocalStore(const LocalStore&) = delete;
    LocalStore& operator=(const LocalStore&) = delete;

    bool authenticate(const std::string& username, const std::string& password, bool sign_up);
    std::int64_t save_game(const std::string& username, int player_side, const SavedGame& game);
    std::vector<SavedGame> games(const std::string& username) const;

private:
    sqlite3* db = nullptr;
};
