#include "../storage/local_store.hpp"
#include <sqlite3.h>
#include <algorithm>
#include <filesystem>
#include <memory>
#include <random>
#include <iostream>
#include <stdexcept>

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void test_accounts_persist(const std::filesystem::path& database) {
    {
        LocalStore store(database);
        require(store.authenticate("alice", "abc", true), "sign up failed");
        require(!store.authenticate("alice", "different", true), "duplicate sign up accepted");
    }
    LocalStore store(database);
    require(store.authenticate("alice", "abc", false), "account did not survive restart");
    require(!store.authenticate("alice", "wrong", false), "wrong password accepted");
    require(!store.authenticate("missing", "abc", false), "missing account accepted");
    require(!store.authenticate("", "abc", true), "empty username accepted");
    require(!store.authenticate("bob", "", true), "empty password accepted");
}

SavedGame completed_game() {
    SavedGame game;
    game.white = "alice";
    game.black = "Guest";
    game.date = "test-date";
    Board board;
    std::mt19937 random(3180);
    for (int action = 0; action < 4096 && !board.has_game_ended(); ++action) {
        auto legal = board.legal_moves();
        require(!legal.empty(), "fixture ran out of legal moves");
        const auto king_capture = std::find_if(legal.begin(), legal.end(), [&](const Move& move) {
            return std::abs(board.get_piece(move.new_x, move.new_y)) == 6;
        });
        const Move selected = king_capture != legal.end() ? *king_capture : legal[random() % legal.size()];
        require(board.make_move(selected), "fixture move was rejected");
        game.mainline.push_back(board.get_last_move());
    }
    require(board.has_game_ended(), "fixture did not finish");
    game.result = board.get_game_status() > 0 ? "1-0" : "0-1";
    return game;
}

void test_game_mainline_and_side_persist(const std::filesystem::path& database) {
    const SavedGame game = completed_game();
    std::int64_t id = 0;
    {
        LocalStore store(database);
        id = store.save_game("alice", 1, game);
        require(id > 0, "game was not saved");
    }
    LocalStore reopened(database);
    const auto saved = reopened.games("alice");
    require(saved.size() == 1, "game did not survive restart");
    require(saved[0].game_id == id && saved[0].player_side == 1 &&
                saved[0].white == game.white && saved[0].black == game.black &&
                saved[0].date == game.date && saved[0].result == game.result &&
                saved[0].mainline == game.mainline, "saved game fields or moves changed");
    require(reopened.games("other").empty(), "another player's games leaked");
    Board replay;
    for (const Move& move : saved[0].mainline)
        require(replay.make_move(move), "saved mainline cannot be replayed");
    require(replay.has_game_ended() &&
                saved[0].result == (replay.get_game_status() > 0 ? "1-0" : "0-1"),
            "saved mainline did not reproduce the game result");
}

std::string query(const std::filesystem::path& database, const char* sql) {
    sqlite3* raw = nullptr;
    const int opened = sqlite3_open(database.string().c_str(), &raw);
    std::unique_ptr<sqlite3, decltype(&sqlite3_close)> db(raw, sqlite3_close);
    require(opened == SQLITE_OK, "cannot inspect test database");
    sqlite3_stmt* raw_statement = nullptr;
    require(sqlite3_prepare_v2(db.get(), sql, -1, &raw_statement, nullptr) == SQLITE_OK,
            "cannot prepare test query");
    std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)> statement(raw_statement, sqlite3_finalize);
    require(sqlite3_step(statement.get()) == SQLITE_ROW, "test query returned no row");
    return reinterpret_cast<const char*>(sqlite3_column_text(statement.get(), 0));
}

void test_plain_sha256_and_bound_usernames(const std::filesystem::path& database) {
    require(query(database, "SELECT password_sha256 FROM accounts WHERE username = 'alice';") ==
                "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
            "stored digest is not plain SHA-256");
    LocalStore store(database);
    const std::string quoted = "o'brien";
    require(store.authenticate(quoted, "quoted-password", true) &&
                store.authenticate(quoted, "quoted-password", false), "bound username changed");
    require(!store.authenticate("' OR 1=1 --", "abc", false), "username was interpreted as SQL");
}

void test_failed_association_rolls_back_game(const std::filesystem::path& database) {
    LocalStore store(database);
    const auto count = query(database, "SELECT count(*) FROM games;");
    for (const auto& request : {std::pair<std::string, int>{"missing", 0}, {"alice", -1}, {"alice", 2}}) {
        bool rejected = false;
        try { store.save_game(request.first, request.second, completed_game()); }
        catch (const std::exception&) { rejected = true; }
        require(rejected, "invalid player association was accepted");
        require(query(database, "SELECT count(*) FROM games;") == count,
                "failed player association left an orphan game");
    }
    const auto id = store.save_game("alice", 0, completed_game());
    require(id > 0 && store.games("alice").front().player_side == 0,
            "transaction did not recover or white side changed");
}

void test_special_moves_round_trip(const std::filesystem::path& database) {
    LocalStore store(database);
    SavedGame game;
    game.white = "alice";
    game.black = "Guest";
    game.result = "1-0";
    game.date = "special-move-serialization";
    for (const auto special : {SpecialMove::CastleKingside, SpecialMove::CastleQueenside,
                              SpecialMove::PromoteQueen, SpecialMove::PromoteRook,
                              SpecialMove::PromoteBishop, SpecialMove::PromoteKnight})
        game.mainline.push_back(Move{0, 0, 1, 1, special});
    const auto id = store.save_game("alice", 0, game);
    const auto saved = store.games("alice");
    require(saved.front().game_id == id && saved.front().mainline == game.mainline,
            "special move codes changed during serialization");
}

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    const std::filesystem::path directory(argv[1]);
    try {
        std::filesystem::remove_all(directory);
        test_accounts_persist(directory / "chess.db");
        test_game_mainline_and_side_persist(directory / "chess.db");
        test_plain_sha256_and_bound_usernames(directory / "chess.db");
        test_failed_association_rolls_back_game(directory / "chess.db");
        test_special_moves_round_trip(directory / "chess.db");
        std::filesystem::remove_all(directory);
        std::cout << "Local store tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        std::filesystem::remove_all(directory);
        return 1;
    }
}
