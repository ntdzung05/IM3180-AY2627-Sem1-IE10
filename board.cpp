#include "board.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <iostream>
#include <locale>
#include <limits>
#include <sstream>

namespace {

constexpr int WhiteHomeRow = 0;
constexpr int BlackHomeRow = 7;
constexpr int KingColumn = 3;
constexpr int QueensideRookColumn = 0;
constexpr int KingsideRookColumn = 7;

constexpr std::uint8_t right_mask(CastlingRight right) {
    return static_cast<std::uint8_t>(right);
}

CastlingRight kingside_right_for_piece(int piece) {
    return piece > 0 ? CastlingRight::WhiteKingside
                     : CastlingRight::BlackKingside;
}

CastlingRight queenside_right_for_piece(int piece) {
    return piece > 0 ? CastlingRight::WhiteQueenside
                     : CastlingRight::BlackQueenside;
}

int home_row_for_piece(int piece) {
    return piece > 0 ? WhiteHomeRow : BlackHomeRow;
}

bool is_promotion(SpecialMove s) {
    return s == SpecialMove::PromoteQueen || s == SpecialMove::PromoteRook
        || s == SpecialMove::PromoteBishop || s == SpecialMove::PromoteKnight;
}

int promotion_piece(SpecialMove s, int moved_piece) {
    const bool white = moved_piece > 0;
    switch (s) {
        case SpecialMove::PromoteQueen:  return white ? 5 : -5;
        case SpecialMove::PromoteRook:   return white ? 4 : -4;
        case SpecialMove::PromoteBishop: return white ? 3 : -3;
        case SpecialMove::PromoteKnight: return white ? 2 : -2;
        default:                         return moved_piece;
    }
}

} // namespace

Board::Board() {
    turn = 0;
    move_left = 1;
    game_status = 0;
    castling_rights = right_mask(CastlingRight::WhiteKingside)
        | right_mask(CastlingRight::WhiteQueenside)
        | right_mask(CastlingRight::BlackKingside)
        | right_mask(CastlingRight::BlackQueenside);
    board_hash = 0;
    last_move = Move{};

    for (int piece = -6; piece <= 6; ++piece) {
        for (int square = 0; square < 64; ++square) {
            piece_hash[piece * 64 + square] = kPieceHashes[(piece + 6) * 64 + square];
        }
    }
    turn_hash[0] = kTurnHashes[0];
    turn_hash[1] = kTurnHashes[1];
    move_left_hash[1] = kMoveLeftHashes[0];
    move_left_hash[2] = kMoveLeftHashes[1];
    for (int rights = 0; rights < 16; ++rights)
        castling_rights_hash[rights] = kCastlingRightsHashes[rights];

    for (int column = 0; column < 8; ++column) {
        current_board[1][column] = 1;
        current_board[6][column] = -1;
    }
    current_board[0] = {4, 2, 3, 6, 5, 3, 2, 4};
    for (int column = 0; column < 8; ++column) {
        current_board[7][column] = -current_board[0][column];
    }

    pawn_move_pattern[1] = {{1, 0}};
    pawn_move_pattern[-1] = {{-1, 0}};
    pawn_capture_pattern[1] = {{1, 1}, {1, -1}};
    pawn_capture_pattern[-1] = {{-1, 1}, {-1, -1}};

    const std::vector<std::pair<int, int>> knight_directions = {
        {2, 1}, {2, -1}, {-2, 1}, {-2, -1},
        {1, 2}, {1, -2}, {-1, 2}, {-1, -2}
    };
    move_pattern[2] = knight_directions;
    move_pattern[-2] = knight_directions;

    auto add_sliding_patterns = [&](int piece, const std::vector<std::pair<int, int>>& directions,
                                    int max_distance) {
        for (const auto& direction : directions) {
            for (int distance = 1; distance <= max_distance; ++distance) {
                move_pattern[piece].emplace_back(
                    direction.first * distance, direction.second * distance);
                move_pattern[-piece].emplace_back(
                    direction.first * distance, direction.second * distance);
            }
        }
    };

    const std::vector<std::pair<int, int>> bishop_directions = {
        {1, 1}, {1, -1}, {-1, 1}, {-1, -1}
    };
    const std::vector<std::pair<int, int>> rook_directions = {
        {1, 0}, {-1, 0}, {0, 1}, {0, -1}
    };
    const std::vector<std::pair<int, int>> king_directions = {
        {1, 0}, {-1, 0}, {0, 1}, {0, -1},
        {1, 1}, {1, -1}, {-1, 1}, {-1, -1}
    };
    add_sliding_patterns(3, bishop_directions, 7);
    add_sliding_patterns(4, rook_directions, 7);
    add_sliding_patterns(5, {
        {1, 0}, {-1, 0}, {0, 1}, {0, -1},
        {1, 1}, {1, -1}, {-1, 1}, {-1, -1}
    }, 7);
    add_sliding_patterns(6, king_directions, 1);

    refresh_hash();
}

bool Board::import_fen(const std::string& fen) {
    std::istringstream input(fen);
    input.imbue(std::locale::classic());
    std::array<std::string, 6> fields;
    for (auto& field : fields)
        if (!(input >> field)) return false;
    std::string extra;
    if (input >> extra) return false;

    std::vector<std::vector<int>> placement(8, std::vector<int>(8, 0));
    const std::string pieces = "kqrbnp PNBRQK";
    int row = 7, column = 0;
    int white_kings = 0, black_kings = 0;
    bool previous_digit = false;
    for (char token : fields[0]) {
        if (token == '/') {
            if (column != 8 || row == 0) return false;
            --row;
            column = 0;
            previous_digit = false;
        } else if (token >= '1' && token <= '8') {
            if (previous_digit) return false;
            column += token - '0';
            if (column > 8) return false;
            previous_digit = true;
        } else {
            const auto index = pieces.find(token);
            if (index == std::string::npos || column >= 8) return false;
            placement[row][column++] = static_cast<int>(index) - 6;
            if (token == 'K') ++white_kings;
            if (token == 'k') ++black_kings;
            previous_digit = false;
        }
    }
    if (row != 0 || column != 8) return false;
    if (white_kings > 1 || black_kings > 1 || white_kings + black_kings == 0)
        return false;
    if (fields[1] != "w" && fields[1] != "b") return false;
    std::uint8_t rights = 0;
    if (fields[2] != "-") {
        const std::string symbols = "KQkq";
        for (char token : fields[2]) {
            const auto bit = symbols.find(token);
            if (bit == std::string::npos || (rights & (1u << bit))) return false;
            rights |= static_cast<std::uint8_t>(1u << bit);
        }
    }
    if (fields[3] != "1" && fields[3] != "2") return false;
    std::uint64_t halfmove, fullmove;
    auto parse_counter = [](const std::string& field, std::uint64_t& value) {
        const auto parsed = std::from_chars(field.data(), field.data() + field.size(), value);
        return parsed.ec == std::errc{} && parsed.ptr == field.data() + field.size();
    };
    if (!parse_counter(fields[4], halfmove) || !parse_counter(fields[5], fullmove)
        || fullmove == 0)
        return false;

    current_board = std::move(placement);
    turn = fields[1] == "w" ? 0 : 1;
    castling_rights = rights;
    move_left = fields[3][0] - '0';
    halfmove_clock = halfmove;
    fullmove_number = fullmove;
    game_status = white_kings == 0 ? -1 : (black_kings == 0 ? 1 : 0);
    rollback.clear();
    last_move = Move{};
    refresh_hash();
    return true;
}

std::string Board::export_fen() const {
    constexpr char pieces[] = "kqrbnp PNBRQK";
    std::string result;
    for (int row = 7; row >= 0; --row) {
        int empty = 0;
        for (int piece : current_board[row]) {
            if (piece == 0) {
                ++empty;
            } else {
                if (empty != 0) result += static_cast<char>('0' + empty);
                empty = 0;
                result += pieces[piece + 6];
            }
        }
        if (empty != 0) result += static_cast<char>('0' + empty);
        if (row != 0) result += '/';
    }
    result += turn == 0 ? " w " : " b ";
    constexpr char rights[] = "KQkq";
    if (castling_rights == 0) result += '-';
    for (int bit = 0; bit < 4; ++bit)
        if (castling_rights & (1u << bit)) result += rights[bit];
    result += " " + std::to_string(move_left) + " " + std::to_string(halfmove_clock)
        + " " + std::to_string(fullmove_number);
    return result;
}

auto Board::begin() -> std::vector<std::vector<int>>::iterator {
    return current_board.begin();
}

auto Board::end() -> std::vector<std::vector<int>>::iterator {
    return current_board.end();
}

auto Board::cbegin() const -> std::vector<std::vector<int>>::const_iterator {
    return current_board.cbegin();
}

auto Board::cend() const -> std::vector<std::vector<int>>::const_iterator {
    return current_board.cend();
}

bool Board::inboard(int x, int y) const {
    return x >= 0 && y >= 0 && x < 8 && y < 8;
}

bool Board::check_game_ended() {
    if (game_status == 0) return false;
    std::cout << "Game ended\n";
    std::cout << (game_status > 0 ? "White won\n" : "Black won\n");
    return true;
}

bool Board::has_game_ended() const { return game_status != 0; }
int Board::get_current_turn() const { return turn; }
int Board::get_moves_left() const { return move_left; }
int Board::get_game_status() const { return game_status; }

int Board::get_piece(int x, int y) const {
    return inboard(x, y) ? current_board[x][y] : 0;
}

Hash Board::get_hash() const { return board_hash; }

std::uint8_t Board::get_castling_rights() const { return castling_rights; }

const Move& Board::get_last_move() const { return last_move; }

Hash Board::calculate_hash() const {
    Hash result = 0;
    for (int row = 0; row < 8; ++row) {
        for (int column = 0; column < 8; ++column) {
            const int piece = current_board[row][column];
            result ^= piece_hash.at(piece * 64 + row * 8 + column);
        }
    }
    result ^= turn_hash.at(turn);
    result ^= move_left_hash.at(move_left);
    result ^= castling_rights_hash.at(castling_rights);
    return result;
}

void Board::refresh_hash() {
    board_hash = calculate_hash();
}

void Board::set_piece_with_hash(int row, int column, int piece) {
    const int square = row * 8 + column;
    // Empty squares have keys too, so both sides of every replacement matter.
    board_hash ^= piece_hash.at(current_board[row][column] * 64 + square);
    current_board[row][column] = piece;
    board_hash ^= piece_hash.at(piece * 64 + square);
}

bool Board::valid_castle(int old_x, int old_y, int new_x, int new_y,
                         SpecialMove& special_move) const {
    if (!inboard(old_x, old_y) || !inboard(new_x, new_y)) return false;

    const int king = current_board[old_x][old_y];
    if (std::abs(king) != 6 || old_y != KingColumn) return false;

    const int home_row = home_row_for_piece(king);
    if (old_x != home_row || new_x != home_row) return false;

    int rook_column;
    CastlingRight right;
    if (new_y == KingsideRookColumn - 2) {
        rook_column = KingsideRookColumn;
        right = kingside_right_for_piece(king);
        special_move = SpecialMove::CastleKingside;
    } else if (new_y == QueensideRookColumn + 1) {
        rook_column = QueensideRookColumn;
        right = queenside_right_for_piece(king);
        special_move = SpecialMove::CastleQueenside;
    } else {
        return false;
    }

    if ((castling_rights & right_mask(right)) == 0) return false;
    if (current_board[home_row][KingColumn] != king) return false;

    const int rook = king > 0 ? 4 : -4;
    if (current_board[home_row][rook_column] != rook) return false;

    const int first_column = std::min(KingColumn, rook_column) + 1;
    const int last_column = std::max(KingColumn, rook_column) - 1;
    for (int column = first_column; column <= last_column; ++column) {
        if (current_board[home_row][column] != 0) return false;
    }

    return true;
}

bool Board::valid_move_geometry(int old_x, int old_y, int new_x, int new_y,
                                SpecialMove* special_move) const {
    if (special_move) *special_move = SpecialMove::None;
    if (has_game_ended()) return false;
    if (!inboard(old_x, old_y) || !inboard(new_x, new_y)) return false;

    const int piece = current_board[old_x][old_y];
    if (piece == 0) return false;
    if ((turn == 1 && piece > 0) || (turn == 0 && piece < 0)) return false;

    SpecialMove castle = SpecialMove::None;
    if (std::abs(piece) == 6 && valid_castle(old_x, old_y, new_x, new_y, castle)) {
        if (special_move) *special_move = castle;
        return true;
    }

    const int destination = current_board[new_x][new_y];
    if (destination != 0 && move_left < 2) return false;
    if (destination != 0 && (piece > 0) == (destination > 0)) return false;

    const int dx = new_x - old_x;
    const int dy = new_y - old_y;

    if (std::abs(piece) == 3 || std::abs(piece) == 4 || std::abs(piece) == 5) {
        const int step_x = (dx > 0) - (dx < 0);
        const int step_y = (dy > 0) - (dy < 0);
        for (int distance = 1; distance < std::max(std::abs(dx), std::abs(dy)); ++distance) {
            const int check_x = old_x + distance * step_x;
            const int check_y = old_y + distance * step_y;
            if (!inboard(check_x, check_y)) return false;
            if (current_board[check_x][check_y] != 0)
                return false;
        }
    }

    bool valid = false;
    if (std::abs(piece) == 1) {
        const auto& patterns = destination == 0
            ? pawn_move_pattern.at(piece)
            : pawn_capture_pattern.at(piece);
        valid = std::find(patterns.begin(), patterns.end(), std::make_pair(dx, dy))
            != patterns.end();
    } else {
        const auto pattern = move_pattern.find(piece);
        if (pattern != move_pattern.end()) {
            valid = std::find(pattern->second.begin(), pattern->second.end(),
                              std::make_pair(dx, dy)) != pattern->second.end();
        }
    }

    if (!valid) return false;

    const bool reaches_final_rank = (piece > 0 && new_x == 7)
        || (piece < 0 && new_x == 0);
    if (special_move && std::abs(piece) == 1 && reaches_final_rank)
        *special_move = SpecialMove::PromoteQueen;

    return true;
}

std::vector<Move> Board::legal_moves() {
    std::vector<Move> moves;
    if (has_game_ended()) return moves;

    for (int old_x = 0; old_x < 8; ++old_x) {
        for (int old_y = 0; old_y < 8; ++old_y) {
            const int piece = current_board[old_x][old_y];
            if (piece == 0 || (piece < 0) != (turn == 1)) continue;

            const auto first_move = moves.size();
            auto add_candidate = [&](int new_x, int new_y) {
                SpecialMove special_move = SpecialMove::None;
                if (!valid_move_geometry(old_x, old_y, new_x, new_y, &special_move))
                    return;
                if (special_move == SpecialMove::PromoteQueen) {
                    const SpecialMove promos[] = {
                        SpecialMove::PromoteQueen, SpecialMove::PromoteRook,
                        SpecialMove::PromoteBishop, SpecialMove::PromoteKnight
                    };
                    for (const SpecialMove p : promos)
                        moves.push_back({old_x, old_y, new_x, new_y, p});
                } else {
                    moves.push_back({old_x, old_y, new_x, new_y, special_move});
                }
            };
            auto add_offsets = [&](const std::vector<std::pair<int, int>>& offsets) {
                for (const auto& [dx, dy] : offsets)
                    add_candidate(old_x + dx, old_y + dy);
            };

            if (std::abs(piece) == 1) {
                add_offsets(pawn_move_pattern.at(piece));
                if (move_left == 2)
                    add_offsets(pawn_capture_pattern.at(piece));
            } else {
                const auto pattern = move_pattern.find(piece);
                if (pattern != move_pattern.end())
                    add_offsets(pattern->second);
            }

            // Castling destinations are outside the king's ordinary offsets.
            if (std::abs(piece) == 6 && old_x == home_row_for_piece(piece)
                && old_y == KingColumn) {
                add_candidate(old_x, QueensideRookColumn + 1);
                add_candidate(old_x, KingsideRookColumn - 2);
            }

            // Keep search ordering and tie-breaking unchanged by this optimization.
            std::sort(moves.begin() + first_move, moves.end(),
                      [](const Move& left, const Move& right) {
                          if (left.new_x != right.new_x)
                              return left.new_x < right.new_x;
                          if (left.new_y != right.new_y)
                              return left.new_y < right.new_y;
                          return static_cast<std::uint8_t>(left.special_move)
                              < static_cast<std::uint8_t>(right.special_move);
                      });
        }
    }
    return moves;
}

void Board::get_turn() {
    std::cout << (turn == 0 ? "White turn\n" : "Black turn\n");
    std::cout << move_left << " move(s) left\n";
}

bool Board::valid_move(int old_x, int old_y, int new_x, int new_y) {
    return valid_move_geometry(old_x, old_y, new_x, new_y);
}

void Board::update_castling_rights(int old_x, int old_y, int new_x, int new_y,
                                   int moved_piece, int captured_piece) {
    auto clear_right = [&](CastlingRight right) {
        castling_rights &= static_cast<std::uint8_t>(~right_mask(right));
    };

    if (std::abs(moved_piece) == 6) {
        if (moved_piece > 0) {
            clear_right(CastlingRight::WhiteKingside);
            clear_right(CastlingRight::WhiteQueenside);
        } else {
            clear_right(CastlingRight::BlackKingside);
            clear_right(CastlingRight::BlackQueenside);
        }
    }

    if (std::abs(moved_piece) == 4) {
        const int home_row = home_row_for_piece(moved_piece);
        if (old_x == home_row && old_y == QueensideRookColumn)
            clear_right(queenside_right_for_piece(moved_piece));
        if (old_x == home_row && old_y == KingsideRookColumn)
            clear_right(kingside_right_for_piece(moved_piece));
    }

    if (std::abs(captured_piece) == 4) {
        const int home_row = home_row_for_piece(captured_piece);
        if (new_x == home_row && new_y == QueensideRookColumn)
            clear_right(queenside_right_for_piece(captured_piece));
        if (new_x == home_row && new_y == KingsideRookColumn)
            clear_right(kingside_right_for_piece(captured_piece));
    }
}

bool Board::make_move(const Move& requested_move) {
    SpecialMove canonical_special = SpecialMove::None;
    if (!valid_move_geometry(requested_move.old_x, requested_move.old_y,
                             requested_move.new_x, requested_move.new_y,
                             &canonical_special)) {
        return false;
    }

    const bool promo_square = (canonical_special == SpecialMove::PromoteQueen);
    if (promo_square) {
        if (requested_move.special_move != SpecialMove::None
            && !is_promotion(requested_move.special_move)) {
            return false;
        }
    } else if (requested_move.special_move != SpecialMove::None
        && requested_move.special_move != canonical_special) {
        return false;
    }

    const SpecialMove applied_special =
        promo_square && requested_move.special_move != SpecialMove::None
            ? requested_move.special_move
            : canonical_special;

    Move applied_move{
        requested_move.old_x,
        requested_move.old_y,
        requested_move.new_x,
        requested_move.new_y,
        applied_special
    };
    const int moved_piece = current_board[applied_move.old_x][applied_move.old_y];
    const int captured_piece = current_board[applied_move.new_x][applied_move.new_y];

    rollback.push_back({
        applied_move,
        moved_piece,
        captured_piece,
        turn,
        move_left,
        game_status,
        castling_rights,
        last_move,
        board_hash,
        halfmove_clock,
        fullmove_number
    });

    board_hash ^= turn_hash.at(turn);
    board_hash ^= move_left_hash.at(move_left);
    board_hash ^= castling_rights_hash.at(castling_rights);

    update_castling_rights(applied_move.old_x, applied_move.old_y,
                           applied_move.new_x, applied_move.new_y,
                           moved_piece, captured_piece);

    const int placed_piece = promotion_piece(applied_special, moved_piece);
    set_piece_with_hash(applied_move.old_x, applied_move.old_y, 0);
    set_piece_with_hash(applied_move.new_x, applied_move.new_y, placed_piece);

    if (canonical_special == SpecialMove::CastleKingside
        || canonical_special == SpecialMove::CastleQueenside) {
        const int home_row = home_row_for_piece(moved_piece);
        const int old_rook_column = canonical_special == SpecialMove::CastleKingside
            ? KingsideRookColumn : QueensideRookColumn;
        const int new_rook_column = canonical_special == SpecialMove::CastleKingside
            ? KingsideRookColumn - 3 : QueensideRookColumn + 2;
        set_piece_with_hash(home_row, old_rook_column, 0);
        set_piece_with_hash(home_row, new_rook_column, moved_piece > 0 ? 4 : -4);
    }

    if (std::abs(moved_piece) == 1 || captured_piece != 0) {
        halfmove_clock = 0;
    } else if (halfmove_clock != std::numeric_limits<std::uint64_t>::max()) {
        ++halfmove_clock;
    }
    const int previous_turn = turn;
    if (captured_piece != 0) {
        if (std::abs(captured_piece) == 6)
            game_status = -captured_piece / 6;
        turn = 1 - turn;
    } else {
        --move_left;
        if (move_left == 0) {
            turn = 1 - turn;
            move_left = 2;
        }
    }

    if (previous_turn == 1 && turn == 0
        && fullmove_number != std::numeric_limits<std::uint64_t>::max()) ++fullmove_number;
    last_move = applied_move;
    board_hash ^= turn_hash.at(turn);
    board_hash ^= move_left_hash.at(move_left);
    board_hash ^= castling_rights_hash.at(castling_rights);
    return true;
}

bool Board::make_move(int old_x, int old_y, int new_x, int new_y) {
    return make_move({old_x, old_y, new_x, new_y, SpecialMove::None});
}

bool Board::make_capture(int old_x, int old_y, int new_x, int new_y) {
    if (!inboard(new_x, new_y) || current_board[new_x][new_y] == 0)
        return false;
    return make_move(old_x, old_y, new_x, new_y);
}

void Board::rollback_move() {
    if (rollback.empty()) return;

    const RollbackState state = rollback.back();
    rollback.pop_back();

    if (state.move.special_move == SpecialMove::CastleKingside
        || state.move.special_move == SpecialMove::CastleQueenside) {
        const int home_row = home_row_for_piece(state.moved_piece);
        const int old_rook_column = state.move.special_move == SpecialMove::CastleKingside
            ? KingsideRookColumn : QueensideRookColumn;
        const int new_rook_column = state.move.special_move == SpecialMove::CastleKingside
            ? KingsideRookColumn - 3 : QueensideRookColumn + 2;
        current_board[home_row][new_rook_column] = 0;
        current_board[home_row][old_rook_column] = state.moved_piece > 0 ? 4 : -4;
    }

    current_board[state.move.old_x][state.move.old_y] = state.moved_piece;
    current_board[state.move.new_x][state.move.new_y] = state.captured_piece;

    turn = state.old_turn;
    move_left = state.old_move_left;
    game_status = state.old_game_status;
    castling_rights = state.old_castling_rights;
    last_move = state.old_last_move;
    board_hash = state.old_hash;
    halfmove_clock = state.old_halfmove_clock;
    fullmove_number = state.old_fullmove_number;
}
