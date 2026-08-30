#include"board.hpp"

#include<iostream>
#include<algorithm>
#include<cmath>

Board::Board(){
    this -> turn = 0;
    this -> move_left = 1;
    this -> game_status = 0;
    this -> board_hash = 0;

    std::uniform_int_distribution<Hash> dist(0, UINT64_MAX);
    for(int i = -6; i <= 6; ++i){
        for(int j = 0; j < 64; ++j){
            piece_hash[i * 64 + j] = dist(rng);
        }
    }
    turn_hash[0] = dist(rng); turn_hash[1] = dist(rng);
    move_left_hash[1] = dist(rng); move_left_hash[2] = dist(rng);

    for(auto& h: current_board[1]) h = 1;
    for(auto& h: current_board[6]) h = -1;
    current_board[0][0] = 4; current_board[0][1] = 2; current_board[0][2] = 3; current_board[0][3] = 6; current_board[0][4] = 5;
    current_board[0][5] = 3; current_board[0][6] = 2; current_board[0][7] = 4;

    for(int i = 0; i < 8; ++i){
        current_board[7][i] = -1 * current_board[0][i];
    }

    for(auto &i: current_board){
        for(auto& j: i){
            board_hash ^= piece_hash[j * 64 + (&j - &i[0]) + 
                (&i - &current_board[0]) * 8];
        }
    }
    board_hash ^= turn_hash[turn];
    board_hash ^= move_left_hash[move_left];

    piece_value[0] = 0; piece_value[1] = 1; piece_value[2] = 3; piece_value[3] = 3; piece_value[4] = 5; piece_value[5] = 9; piece_value[6] = 99;
    piece_value[-1] = -1; piece_value[-2] = -3; piece_value[-3] = -3; piece_value[-4] = -5; piece_value[-5] = -9; piece_value[-6] = -99;

    pawn_move_pattern[1].push_back({1, 0});
    pawn_capture_pattern[1].push_back({1, 1}); pawn_capture_pattern[1].push_back({1, -1});
    
    pawn_move_pattern[-1].push_back({-1, 0});
    pawn_capture_pattern[-1].push_back({-1, 1}); pawn_capture_pattern[-1].push_back({-1, -1});
    
    move_pattern[2].push_back({2, 1}); move_pattern[2].push_back({2, -1}); move_pattern[2].push_back({-2, 1}); move_pattern[2].push_back({-2, -1});
    move_pattern[2].push_back({1, 2}); move_pattern[2].push_back({1, -2}); move_pattern[2].push_back({-1, 2}); move_pattern[2].push_back({-1, -2}); 

    move_pattern[3].push_back({1, 1}); move_pattern[3].push_back({2, 2}); move_pattern[3].push_back({3, 3}); move_pattern[3].push_back({4, 4}); move_pattern[3].push_back({5, 5}); move_pattern[3].push_back({6, 6}); move_pattern[3].push_back({7, 7});
    move_pattern[3].push_back({-1, -1}); move_pattern[3].push_back({-2, -2}); move_pattern[3].push_back({-3, -3}); move_pattern[3].push_back({-4, -4}); move_pattern[3].push_back({-5, -5}); move_pattern[3].push_back({-6, -6}); move_pattern[3].push_back({-7, -7});
    move_pattern[3].push_back({1, -1}); move_pattern[3].push_back({2, -2}); move_pattern[3].push_back({3, -3}); move_pattern[3].push_back({4, -4}); move_pattern[3].push_back({5, -5}); move_pattern[3].push_back({6, -6}); move_pattern[3].push_back({7, -7});
    move_pattern[3].push_back({-1, 1}); move_pattern[3].push_back({-2, 2}); move_pattern[3].push_back({-3, 3}); move_pattern[3].push_back({-4, 4}); move_pattern[3].push_back({-5, 5}); move_pattern[3].push_back({-6, 6}); move_pattern[3].push_back({-7, 7});

    move_pattern[4].push_back({1, 0}); move_pattern[4].push_back({2, 0}); move_pattern[4].push_back({3, 0}); move_pattern[4].push_back({4, 0}); move_pattern[4].push_back({5, 0}); move_pattern[4].push_back({6, 0}); move_pattern[4].push_back({7, 0});
    move_pattern[4].push_back({-1, 0}); move_pattern[4].push_back({-2, 0}); move_pattern[4].push_back({-3, 0}); move_pattern[4].push_back({-4, 0}); move_pattern[4].push_back({-5, 0}); move_pattern[4].push_back({-6, 0}); move_pattern[4].push_back({-7, 0});
    move_pattern[4].push_back({0, 1}); move_pattern[4].push_back({0, 2}); move_pattern[4].push_back({0, 3}); move_pattern[4].push_back({0, 4}); move_pattern[4].push_back({0, 5}); move_pattern[4].push_back({0, 6}); move_pattern[4].push_back({0, 7});
    move_pattern[4].push_back({0, -1}); move_pattern[4].push_back({0, -2}); move_pattern[4].push_back({0, -3}); move_pattern[4].push_back({0, -4}); move_pattern[4].push_back({0, -5}); move_pattern[4].push_back({0, -6}); move_pattern[4].push_back({0, -7});   

    move_pattern[5].push_back({1, 0}); move_pattern[5].push_back({2, 0}); move_pattern[5].push_back({3, 0}); move_pattern[5].push_back({4, 0}); move_pattern[5].push_back({5, 0}); move_pattern[5].push_back({6, 0}); move_pattern[5].push_back({7, 0});
    move_pattern[5].push_back({-1, 0}); move_pattern[5].push_back({-2, 0}); move_pattern[5].push_back({-3, 0}); move_pattern[5].push_back({-4, 0}); move_pattern[5].push_back({-5, 0}); move_pattern[5].push_back({-6, 0}); move_pattern[5].push_back({-7, 0});
    move_pattern[5].push_back({0, 1}); move_pattern[5].push_back({0, 2}); move_pattern[5].push_back({0, 3}); move_pattern[5].push_back({0, 4}); move_pattern[5].push_back({0, 5}); move_pattern[5].push_back({0, 6}); move_pattern[5].push_back({0, 7});
    move_pattern[5].push_back({0, -1}); move_pattern[5].push_back({0, -2}); move_pattern[5].push_back({0, -3}); move_pattern[5].push_back({0, -4}); move_pattern[5].push_back({0, -5}); move_pattern[5].push_back({0, -6}); move_pattern[5].push_back({0, -7});
    move_pattern[5].push_back({1, 1}); move_pattern[5].push_back({2, 2}); move_pattern[5].push_back({3, 3}); move_pattern[5].push_back({4, 4}); move_pattern[5].push_back({5, 5}); move_pattern[5].push_back({6, 6}); move_pattern[5].push_back({7, 7});
    move_pattern[5].push_back({-1, -1}); move_pattern[5].push_back({-2, -2}); move_pattern[5].push_back({-3, -3}); move_pattern[5].push_back({-4, -4}); move_pattern[5].push_back({-5, -5}); move_pattern[5].push_back({-6, -6}); move_pattern[5].push_back({-7, -7});
    move_pattern[5].push_back({1, -1}); move_pattern[5].push_back({2, -2}); move_pattern[5].push_back({3, -3}); move_pattern[5].push_back({4, -4}); move_pattern[5].push_back({5, -5}); move_pattern[5].push_back({6, -6}); move_pattern[5].push_back({7, -7});
    move_pattern[5].push_back({-1, 1}); move_pattern[5].push_back({-2, 2}); move_pattern[5].push_back({-3, 3}); move_pattern[5].push_back({-4, 4}); move_pattern[5].push_back({-5, 5}); move_pattern[5].push_back({-6, 6}); move_pattern[5].push_back({-7, 7});

    move_pattern[6].push_back({1, 0}); move_pattern[6].push_back({-1, 0}); move_pattern[6].push_back({0, 1}); move_pattern[6].push_back({0, -1});
    move_pattern[6].push_back({1, 1}); move_pattern[6].push_back({1, -1}); move_pattern[6].push_back({-1, 1}); move_pattern[6].push_back({-1, -1});
    
    move_pattern[-2].push_back({2, 1}); move_pattern[-2].push_back({2, -1}); move_pattern[-2].push_back({-2, 1}); move_pattern[-2].push_back({-2, -1});
    move_pattern[-2].push_back({1, 2}); move_pattern[-2].push_back({1, -2}); move_pattern[-2].push_back({-1, 2}); move_pattern[-2].push_back({-1, -2}); 

    move_pattern[-3].push_back({1, 1}); move_pattern[-3].push_back({2, 2}); move_pattern[-3].push_back({3, 3}); move_pattern[-3].push_back({4, 4}); move_pattern[-3].push_back({5, 5}); move_pattern[-3].push_back({6, 6}); move_pattern[-3].push_back({7, 7});
    move_pattern[-3].push_back({-1, -1}); move_pattern[-3].push_back({-2, -2}); move_pattern[-3].push_back({-3, -3}); move_pattern[-3].push_back({-4, -4}); move_pattern[-3].push_back({-5, -5}); move_pattern[-3].push_back({-6, -6}); move_pattern[-3].push_back({-7, -7});
    move_pattern[-3].push_back({1, -1}); move_pattern[-3].push_back({2, -2}); move_pattern[-3].push_back({3, -3}); move_pattern[-3].push_back({4, -4}); move_pattern[-3].push_back({5, -5}); move_pattern[-3].push_back({6, -6}); move_pattern[-3].push_back({7, -7});
    move_pattern[-3].push_back({-1, 1}); move_pattern[-3].push_back({-2, 2}); move_pattern[-3].push_back({-3, 3}); move_pattern[-3].push_back({-4, 4}); move_pattern[-3].push_back({-5, 5}); move_pattern[-3].push_back({-6, 6}); move_pattern[-3].push_back({-7, 7});

    move_pattern[-4].push_back({1, 0}); move_pattern[-4].push_back({2, 0}); move_pattern[-4].push_back({3, 0}); move_pattern[-4].push_back({4, 0}); move_pattern[-4].push_back({5, 0}); move_pattern[-4].push_back({6, 0}); move_pattern[-4].push_back({7, 0});
    move_pattern[-4].push_back({-1, 0}); move_pattern[-4].push_back({-2, 0}); move_pattern[-4].push_back({-3, 0}); move_pattern[-4].push_back({-4, 0}); move_pattern[-4].push_back({-5, 0}); move_pattern[-4].push_back({-6, 0}); move_pattern[-4].push_back({-7, 0});
    move_pattern[-4].push_back({0, 1}); move_pattern[-4].push_back({0, 2}); move_pattern[-4].push_back({0, 3}); move_pattern[-4].push_back({0, 4}); move_pattern[-4].push_back({0, 5}); move_pattern[-4].push_back({0, 6}); move_pattern[-4].push_back({0, 7});
    move_pattern[-4].push_back({0, -1}); move_pattern[-4].push_back({0, -2}); move_pattern[-4].push_back({0, -3}); move_pattern[-4].push_back({0, -4}); move_pattern[-4].push_back({0, -5}); move_pattern[-4].push_back({0, -6}); move_pattern[-4].push_back({0, -7});   

    move_pattern[-5].push_back({1, 0}); move_pattern[-5].push_back({2, 0}); move_pattern[-5].push_back({3, 0}); move_pattern[-5].push_back({4, 0}); move_pattern[-5].push_back({5, 0}); move_pattern[-5].push_back({6, 0}); move_pattern[-5].push_back({7, 0});
    move_pattern[-5].push_back({-1, 0}); move_pattern[-5].push_back({-2, 0}); move_pattern[-5].push_back({-3, 0}); move_pattern[-5].push_back({-4, 0}); move_pattern[-5].push_back({-5, 0}); move_pattern[-5].push_back({-6, 0}); move_pattern[-5].push_back({-7, 0});
    move_pattern[-5].push_back({0, 1}); move_pattern[-5].push_back({0, 2}); move_pattern[-5].push_back({0, 3}); move_pattern[-5].push_back({0, 4}); move_pattern[-5].push_back({0, 5}); move_pattern[-5].push_back({0, 6}); move_pattern[-5].push_back({0, 7});
    move_pattern[-5].push_back({0, -1}); move_pattern[-5].push_back({0, -2}); move_pattern[-5].push_back({0, -3}); move_pattern[-5].push_back({0, -4}); move_pattern[-5].push_back({0, -5}); move_pattern[-5].push_back({0, -6}); move_pattern[-5].push_back({0, -7});
    move_pattern[-5].push_back({1, 1}); move_pattern[-5].push_back({2, 2}); move_pattern[-5].push_back({3, 3}); move_pattern[-5].push_back({4, 4}); move_pattern[-5].push_back({5, 5}); move_pattern[-5].push_back({6, 6}); move_pattern[-5].push_back({7, 7});
    move_pattern[-5].push_back({-1, -1}); move_pattern[-5].push_back({-2, -2}); move_pattern[-5].push_back({-3, -3}); move_pattern[-5].push_back({-4, -4}); move_pattern[-5].push_back({-5, -5}); move_pattern[-5].push_back({-6, -6}); move_pattern[-5].push_back({-7, -7});
    move_pattern[-5].push_back({1, -1}); move_pattern[-5].push_back({2, -2}); move_pattern[-5].push_back({3, -3}); move_pattern[-5].push_back({4, -4}); move_pattern[-5].push_back({5, -5}); move_pattern[-5].push_back({6, -6}); move_pattern[-5].push_back({7, -7});
    move_pattern[-5].push_back({-1, 1}); move_pattern[-5].push_back({-2, 2}); move_pattern[-5].push_back({-3, 3}); move_pattern[-5].push_back({-4, 4}); move_pattern[-5].push_back({-5, 5}); move_pattern[-5].push_back({-6, 6}); move_pattern[-5].push_back({-7, 7});

    move_pattern[-6].push_back({1, 0}); move_pattern[-6].push_back({-1, 0}); move_pattern[-6].push_back({0, 1}); move_pattern[-6].push_back({0, -1});
    move_pattern[-6].push_back({1, 1}); move_pattern[-6].push_back({1, -1}); move_pattern[-6].push_back({-1, 1}); move_pattern[-6].push_back({-1, -1});
}


auto Board::begin() -> std::vector<std::vector<int>>::iterator{
    return current_board.begin();
}

auto Board::end() -> std::vector<std::vector<int>>::iterator{
    return current_board.end();
}

auto Board::cbegin() const -> std::vector<std::vector<int>>::const_iterator{
    return current_board.begin();
}

auto Board::cend() const -> std::vector<std::vector<int>>::const_iterator{
    return current_board.end();
}

bool Board::inboard(int x, int y) { return (x >= 0 && y >= 0 && x < 8 && y < 8); }

bool Board::check_game_ended(){
    if(game_status){
        std::cout << "Game ended\n";
        if(game_status > 0) std::cout << "White won\n";
        else std::cout << "Black won\n";
        return true;
    }
    return false;
}

void Board::get_turn(){
    if(!turn){
        std::cout << "White turn\n";
    }
    else std::cout << "Black turn\n";
    std::cout << move_left << ' ' << "move(s) left\n";
}

bool Board::valid_move(int old_x, int old_y, int new_x, int new_y){
    if(!inboard(new_x, new_y) || !inboard(old_x, old_y)) return false;
    
    int piece = current_board[old_x][old_y];
    if(!piece) return false; 
    if(turn && piece > 0) return false;
    if(!turn && piece < 0) return false;

    int destination_piece = current_board[new_x][new_y];
    
    if(destination_piece != 0 && (piece > 0) == (destination_piece > 0)) return false;
    
    int dx = new_x - old_x;
    int dy = new_y - old_y;
    
    if(piece == 3 || piece == -3 || piece == 4 || piece == -4 || piece == 5 || piece == -5){
    int step_x = (dx > 0) - (dx < 0);
    int step_y = (dy > 0) - (dy < 0);

        for(int i = 1; i < std::max(std::abs(dx), std::abs(dy)); ++i){

            int check_x = old_x + i * step_x;
            int check_y = old_y + i * step_y;

            if(!inboard(check_x, check_y)) return false;

            if(current_board[check_x][check_y] != 0) return false;
        }
    }

    if(piece == 1 || piece == -1) {
        if(destination_piece == 0) {
            auto& moves = pawn_move_pattern[piece];
            for(auto& move : moves) {
                if(move.first == dx && move.second == dy) {
                    return true;
                }
            }
            return false;
        }
        else {
            auto& captures = pawn_capture_pattern[piece];
            for(auto& capture : captures) {
                if(capture.first == dx && capture.second == dy) {
                    return true;
                }
            }
            return false;
        }
    }
    
    if(move_pattern.find(piece) == move_pattern.end()) return false;
    
    auto& patterns = move_pattern[piece];
    for(auto& pattern : patterns) {
        if(pattern.first == dx && pattern.second == dy) {
            return true;
        }
    }
    
    return false;
}

void Board::make_move(int old_x, int old_y, int new_x, int new_y){
    if(check_game_ended()) return;
    if(!valid_move(old_x, old_y, new_x, new_y)){
        std::cout << "Invalid move\n";
        return;
    }

    if(current_board[new_x][new_y]) { make_capture(old_x, old_y, new_x, new_y); return; }

    int piece = current_board[old_x][old_y];
    board_hash ^= piece_hash[piece * 64 + old_x * 8 + old_y];
    board_hash ^= piece_hash[0 * 64 + old_x * 8 + old_y];
    board_hash ^= turn_hash[turn];
    board_hash ^= move_left_hash[move_left];

    rollback.push_back(std::make_tuple(old_x, old_y, new_x, new_y, 
        current_board[new_x][new_y], this -> turn, this -> move_left));

    current_board[new_x][new_y] = piece;
    current_board[old_x][old_y] = 0;
    --move_left;
    if(!move_left) turn = 1 - turn, move_left = 2;

    board_hash ^= piece_hash[piece * 64 + new_x * 8 + new_y];
    board_hash ^= piece_hash[0 * 64 + new_x * 8 + new_y];
    board_hash ^= turn_hash[turn];
    board_hash ^= move_left_hash[move_left];
}

void Board::make_capture(int old_x, int old_y, int new_x, int new_y){
    if(check_game_ended()) return;

    if(move_left < 2) {
        std::cout << "Insufficient move left";
        return;
    }

    int piece = current_board[old_x][old_y], captured_piece = current_board[new_x][new_y];

    board_hash ^= piece_hash[piece * 64 + old_x * 8 + old_y];
    board_hash ^= piece_hash[captured_piece * 64 + new_x * 8 + new_y];
    board_hash ^= piece_hash[0 * 64 + old_x * 8 + old_y];
    board_hash ^= turn_hash[turn];
    board_hash ^= move_left_hash[move_left];

    rollback.push_back(std::make_tuple(old_x, old_y, new_x, new_y, 
        captured_piece, this -> turn, this -> move_left));

    if(std::abs(captured_piece) == 6) game_status = -1 * captured_piece / 6;

    current_board[new_x][new_y] = piece;
    current_board[old_x][old_y] = 0;
    turn = 1 - turn;

    board_hash ^= piece_hash[piece * 64 + new_x * 8 + new_y];
    board_hash ^= turn_hash[turn];
    board_hash ^= move_left_hash[move_left];
}

int Board::board_eval(){
    int val = 0;
    for(auto& i: current_board){
        for(auto& j: i){
            val += piece_value[j];
        }
    }
    return val * (turn ? -1 : 1);
}

void Board::rollback_move(){
    if(rollback.empty()) return;
    auto [old_x, old_y, new_x, new_y, captured_piece, old_turn, old_move_left] = rollback.back();
    rollback.pop_back();

    board_hash ^= piece_hash[current_board[old_x][old_y] * 64 + old_x * 8 + old_y];
    board_hash ^= piece_hash[current_board[new_x][new_y] * 64 + new_x * 8 + new_y];
    board_hash ^= turn_hash[turn];
    board_hash ^= move_left_hash[move_left];

    current_board[old_x][old_y] = current_board[new_x][new_y];
    current_board[new_x][new_y] = captured_piece;
    if(std::abs(captured_piece) == 6) game_status = 0;
    this -> turn = old_turn;
    this -> move_left = old_move_left;

    board_hash ^= piece_hash[current_board[old_x][old_y] * 64 + old_x * 8 + old_y];
    board_hash ^= piece_hash[current_board[new_x][new_y] * 64 + new_x * 8 + new_y];
    board_hash ^= turn_hash[turn];
    board_hash ^= move_left_hash[move_left];
}

int Board::negamax(int move_remaining, int depth, int alpha, int beta){
    ++search_nodes;
    if(alpha >= beta) ++closed_window_nodes;
    if(move_remaining == 2 && depth >= 30) return board_eval();
    if(check_game_ended()) return board_eval();

    int best_score = NEGINF;

    for(auto& x: current_board){
        for(auto& y: x){
            auto& get_move_pattern = (std::abs(y) == 1) ? pawn_move_pattern[y] : move_pattern[y];
            for(auto& move: get_move_pattern){
                int new_x = (&x - &current_board[0]) + move.first;
                int new_y = (&y - &x[0]) + move.second;
                if(valid_move(&x - &current_board[0], &y - &x[0], new_x, new_y)){
                    if(current_board[new_x][new_y] != 0) continue;
                    make_move(&x - &current_board[0], &y - &x[0], new_x, new_y);
                    auto score = negamax(move_remaining == 1 ? 2 : 1, depth + (move_remaining == 1 ? 1 : 0), 
                        move_remaining == 2 ? alpha : -beta, move_remaining == 2 ? beta : -alpha) * (move_remaining == 1 ? -1 : 1);
                    best_score = std::max(best_score, score);
                    alpha = std::max(alpha, score);
                    rollback_move();
                    if(alpha >= beta) return best_score;
                }
            }
            if(move_remaining == 1) continue;
            auto& get_move_pattern_capture = (std::abs(y) == 1) ? pawn_capture_pattern[y] : move_pattern[y];
            for(auto& move: get_move_pattern_capture){
                int new_x = (&x - &current_board[0]) + move.first;
                int new_y = (&y - &x[0]) + move.second;
                if(valid_move(&x - &current_board[0], &y - &x[0], new_x, new_y)){
                    if(current_board[new_x][new_y] == 0) continue;
                    make_capture(&x - &current_board[0], &y - &x[0], new_x, new_y);
                    auto score = -negamax(2, depth + 1, -beta, -alpha);
                    best_score = std::max(best_score, score);
                    alpha = std::max(alpha, score);
                    rollback_move();
                    if(alpha >= beta) return best_score;
                }
            }
        }
    }

    return best_score;
}
