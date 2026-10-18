#include "evaluator.hpp"

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

EngineConfig EngineConfig::standard() {
    EngineConfig config;
    config.pawn_sq_table = {
         0,  0,  0,  0,  0,  0,  0,  0,
         5, 10, 10,-20,-20, 10, 10,  5,
         5, -5,-10,  0,  0,-10, -5,  5,
         0,  0,  0, 20, 20,  0,  0,  0,
         5,  5, 10, 25, 25, 10,  5,  5,
        10, 10, 20, 30, 30, 20, 10, 10,
        50, 50, 50, 50, 50, 50, 50, 50,
         0,  0,  0,  0,  0,  0,  0,  0
    };
    config.knight_sq_table = {
       -50,-40,-30,-30,-30,-30,-40,-50,
       -40,-20,  0,  5,  5,  0,-20,-40,
       -30,  5, 10, 15, 15, 10,  5,-30,
       -30,  0, 15, 20, 20, 15,  0,-30,
       -30,  5, 15, 20, 20, 15,  5,-30,
       -30,  0, 10, 15, 15, 10,  0,-30,
       -40,-20,  0,  0,  0,  0,-20,-40,
       -50,-40,-30,-30,-30,-30,-40,-50
    };
    config.king_sq_table = {
        20, 30, 10,  0,  0, 10, 30, 20,
        20, 20,  0,  0,  0,  0, 20, 20,
       -10,-20,-20,-20,-20,-20,-20,-10,
       -20,-30,-30,-40,-40,-30,-30,-20,
       -30,-40,-40,-50,-50,-40,-40,-30,
       -30,-40,-40,-50,-50,-40,-40,-30,
       -30,-40,-40,-50,-50,-40,-40,-30,
       -30,-40,-40,-50,-50,-40,-40,-30
    };
    config.piece_value = {0, 1, 3, 3, 5, 9, 99999};
    config.passed_pawn_bonus = {0, 5, 15, 30, 60, 100, 160, 0};
    return config;
}

StandardEvaluator::StandardEvaluator(EngineConfig evaluator_config)
    : config(std::move(evaluator_config)) {}

const EngineConfig& StandardEvaluator::get_config() const { return config; }

int StandardEvaluator::evaluate(const Board& board) const {
    if (board.has_game_ended()) {
        int winner_score = board.get_game_status() > 0 ? 1e9 : -1e9;
        return winner_score * (board.get_current_turn() ? -1 : 1);
    }

    int raw_score = 0;
    int pawn_count_white[8] = {0};
    int pawn_count_black[8] = {0};
    int white_king_x = -1, white_king_y = -1;
    int black_king_x = -1, black_king_y = -1;
    std::vector<std::pair<int, int>> white_rooks;
    std::vector<std::pair<int, int>> black_rooks;

    auto is_on_board = [](int row, int column) {
        return row >= 0 && row < 8 && column >= 0 && column < 8;
    };

    for (int row = 0; row < 8; ++row) {
        for (int column = 0; column < 8; ++column) {
            int piece = board.get_piece(row, column);
            if (piece == 0) continue;

            int absolute_piece = std::abs(piece);
            int positional_score = 0;
            int square_index = piece > 0
                ? row * 8 + column
                : (7 - row) * 8 + column;

            switch (absolute_piece) {
                case 1: {
                    positional_score = config.pawn_sq_table[square_index];
                    if (piece > 0) ++pawn_count_white[column];
                    else ++pawn_count_black[column];

                    bool passed = true;
                    int forward = piece > 0 ? 1 : -1;
                    for (int check_row = row + forward;
                         check_row >= 0 && check_row < 8;
                         check_row += forward) {
                        for (int check_column = column - 1;
                             check_column <= column + 1; ++check_column) {
                            if (!is_on_board(check_row, check_column)) continue;
                            int enemy_pawn = piece > 0 ? -1 : 1;
                            if (board.get_piece(check_row, check_column) == enemy_pawn) {
                                passed = false;
                                break;
                            }
                        }
                        if (!passed) break;
                    }
                    if (passed) {
                        int rank = piece > 0 ? row : 7 - row;
                        raw_score += (piece > 0 ? 1 : -1) * config.passed_pawn_bonus[rank];
                    }
                    break;
                }
                case 2: {
                    positional_score = config.knight_sq_table[square_index];
                    const int knight_moves[8][2] = {
                        {-2,-1}, {-2,1}, {-1,-2}, {-1,2},
                        {1,-2}, {1,2}, {2,-1}, {2,1}
                    };
                    int mobility = 0;
                    for (const auto& move : knight_moves) {
                        int next_row = row + move[0];
                        int next_column = column + move[1];
                        if (!is_on_board(next_row, next_column)) continue;
                        int destination = board.get_piece(next_row, next_column);
                        if (destination == 0 || (destination > 0) != (piece > 0)) ++mobility;
                    }
                    raw_score += (piece > 0 ? 1 : -1) * mobility * config.knight_mobility_weight;
                    break;
                }
                case 3: {
                    const int bishop_directions[4][2] = {
                        {-1,-1}, {-1,1}, {1,-1}, {1,1}
                    };
                    int mobility = 0;
                    for (const auto& direction : bishop_directions) {
                        int next_row = row + direction[0];
                        int next_column = column + direction[1];
                        while (is_on_board(next_row, next_column)) {
                            int destination = board.get_piece(next_row, next_column);
                            if (destination == 0) ++mobility;
                            else {
                                if ((destination > 0) != (piece > 0)) ++mobility;
                                break;
                            }
                            next_row += direction[0];
                            next_column += direction[1];
                        }
                    }
                    raw_score += (piece > 0 ? 1 : -1) * mobility * config.bishop_mobility_weight;
                    break;
                }
                case 4:
                    (piece > 0 ? white_rooks : black_rooks).push_back({row, column});
                    break;
                case 6:
                    positional_score = config.king_sq_table[square_index];
                    if (piece > 0) { white_king_x = row; white_king_y = column; }
                    else { black_king_x = row; black_king_y = column; }
                    break;
                default:
                    break;
            }

            int material = config.piece_value[absolute_piece] * 100;
            raw_score += piece > 0
                ? material + positional_score
                : -(material + positional_score);
        }
    }

    for (int column = 0; column < 8; ++column) {
        if (pawn_count_white[column] > 1)
            raw_score -= (pawn_count_white[column] - 1) * config.doubled_pawn_penalty;
        if (pawn_count_white[column] > 0) {
            bool left = column > 0 && pawn_count_white[column - 1] > 0;
            bool right = column < 7 && pawn_count_white[column + 1] > 0;
            if (!left && !right) raw_score -= config.isolated_pawn_penalty;
        }
        if (pawn_count_black[column] > 1)
            raw_score += (pawn_count_black[column] - 1) * config.doubled_pawn_penalty;
        if (pawn_count_black[column] > 0) {
            bool left = column > 0 && pawn_count_black[column - 1] > 0;
            bool right = column < 7 && pawn_count_black[column + 1] > 0;
            if (!left && !right) raw_score += config.isolated_pawn_penalty;
        }
    }

    auto rook_connection = [&](const std::vector<std::pair<int, int>>& rooks) {
        int score = 0;
        for (std::size_t first = 0; first < rooks.size(); ++first) {
            for (std::size_t second = first + 1; second < rooks.size(); ++second) {
                int row1 = rooks[first].first, column1 = rooks[first].second;
                int row2 = rooks[second].first, column2 = rooks[second].second;
                if (row1 == row2) {
                    bool blocked = false;
                    for (int column = std::min(column1, column2) + 1;
                         column < std::max(column1, column2); ++column)
                        if (board.get_piece(row1, column) != 0) blocked = true;
                    if (!blocked) score += config.connected_rook_bonus;
                } else if (column1 == column2) {
                    bool blocked = false;
                    for (int row = std::min(row1, row2) + 1;
                         row < std::max(row1, row2); ++row)
                        if (board.get_piece(row, column1) != 0) blocked = true;
                    if (!blocked) score += config.connected_rook_bonus;
                }
            }
        }
        return score;
    };
    raw_score += rook_connection(white_rooks);
    raw_score -= rook_connection(black_rooks);

    auto king_shield = [&](int king_row, int king_column, int pawn_type) {
        if (king_row == -1) return 0;
        int forward_row = king_row + (pawn_type > 0 ? 1 : -1);
        int score = 0;
        if (forward_row >= 0 && forward_row < 8) {
            for (int offset = -1; offset <= 1; ++offset) {
                int column = king_column + offset;
                if (column >= 0 && column < 8 && board.get_piece(forward_row, column) == pawn_type)
                    score += config.king_shield_bonus;
            }
        }
        return score;
    };
    raw_score += king_shield(white_king_x, white_king_y, 1);
    raw_score -= king_shield(black_king_x, black_king_y, -1);

    return raw_score * (board.get_current_turn() ? -1 : 1);
}
