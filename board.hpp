#pragma once

#include<vector>
#include<unordered_map>
#include<cstdint>
#include<random>
#include<tuple>

using Hash = std::uint64_t;

inline const int INF = 1e9, NEGINF = -1e9;

class Board{
    friend struct BoardTestAccess;

    std::vector<std::vector<int>> current_board{8, std::vector<int> (8, 0)};
    std::vector<std::tuple<int, int, int, int, int, int, int>> rollback; //old old new new piece turn move_left
    std::unordered_map<int, int> piece_value;
    std::unordered_map<int, Hash> piece_hash, turn_hash, move_left_hash;
    std::unordered_map<int, std::vector<std::pair<int, int>>> move_pattern;
    std::unordered_map<int, std::vector<std::pair<int, int>>> pawn_move_pattern;
    std::unordered_map<int, std::vector<std::pair<int, int>>> pawn_capture_pattern;
    int turn;
    int move_left;
    int game_status;
    Hash board_hash;
    std::mt19937_64 rng{std::random_device{}()};
    std::size_t search_nodes = 0;
    std::size_t closed_window_nodes = 0;

public:
    Board();
    ~Board() = default;

    auto begin() -> std::vector<std::vector<int>>::iterator;
    auto end() -> std::vector<std::vector<int>>::iterator;
    auto cbegin() const -> std::vector<std::vector<int>>::const_iterator;
    auto cend() const -> std::vector<std::vector<int>>::const_iterator;

    bool inboard(int x, int y);
    bool check_game_ended();
    void get_turn();
    bool valid_move(int old_x, int old_y, int new_x, int new_y);
    void make_move(int old_x, int old_y, int new_x, int new_y);
    void make_capture(int old_x, int old_y, int new_x, int new_y);
    int board_eval();
    void rollback_move();
    int negamax(int move_remaining, int depth, int alpha = NEGINF, int beta = INF);
};

