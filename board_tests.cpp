#include "board.hpp"
#include "board_test_data.hpp"

#include <iostream>
#include <sstream>
#include <string>

struct BoardTestAccess {
    static void load(Board& board, const MockPosition& position) {
        for (int x = 0; x < 8; ++x) {
            for (int y = 0; y < 8; ++y) {
                board.current_board[x][y] = position.squares[x][y];
            }
        }

        board.turn = position.turn;
        board.move_left = position.move_left;
        board.game_status = position.game_status;
        board.rollback.clear();
        board.board_hash = recomputed_hash(board);
    }

    static MockPosition snapshot(const Board& board) {
        MockPosition position;

        for (int x = 0; x < 8; ++x) {
            for (int y = 0; y < 8; ++y) {
                position.squares[x][y] = board.current_board[x][y];
            }
        }

        position.turn = board.turn;
        position.move_left = board.move_left;
        position.game_status = board.game_status;
        return position;
    }

    static std::size_t undo_size(const Board& board) {
        return board.rollback.size();
    }

    static Hash hash(const Board& board) {
        return board.board_hash;
    }

    static void reset_search_nodes(Board& board) {
        board.search_nodes = 0;
        board.closed_window_nodes = 0;
    }

    static std::size_t search_nodes(const Board& board) {
        return board.search_nodes;
    }

    static std::size_t closed_window_nodes(const Board& board) {
        return board.closed_window_nodes;
    }

    static Hash recomputed_hash(const Board& board) {
        Hash result = 0;

        for (int x = 0; x < 8; ++x) {
            for (int y = 0; y < 8; ++y) {
                const int piece = board.current_board[x][y];
                result ^= board.piece_hash.at(piece * 64 + x * 8 + y);
            }
        }

        result ^= board.turn_hash.at(board.turn);
        result ^= board.move_left_hash.at(board.move_left);
        return result;
    }
};

namespace {

class SilenceCout {
public:
    SilenceCout() : previous(std::cout.rdbuf(output.rdbuf())) {}
    ~SilenceCout() { std::cout.rdbuf(previous); }

private:
    std::ostringstream output;
    std::streambuf* previous;
};

bool same_position(const MockPosition& left, const MockPosition& right) {
    return left.squares == right.squares
        && left.turn == right.turn
        && left.move_left == right.move_left
        && left.game_status == right.game_status;
}

class TestResults {
public:
    void check(bool passed, const std::string& name, const std::string& detail = {}) {
        if (passed) {
            ++passes;
            std::cout << "[PASS] " << name << '\n';
            return;
        }

        ++failures;
        std::cout << "[FAIL] " << name;
        if (!detail.empty()) {
            std::cout << " -- " << detail;
        }
        std::cout << '\n';
    }

    int exit_code() const {
        std::cout << "\n" << passes << " passed, " << failures << " failed\n";
        return failures == 0 ? 0 : 1;
    }

private:
    int passes = 0;
    int failures = 0;
};

void run_initial_state_test(TestResults& results) {
    Board board;
    const MockPosition actual = BoardTestAccess::snapshot(board);

    results.check(
        actual.turn == 0 && actual.move_left == 1 && actual.game_status == 0,
        "initial state gives White one opening move",
        "actual turn=" + std::to_string(actual.turn)
            + ", move_left=" + std::to_string(actual.move_left)
            + ", game_status=" + std::to_string(actual.game_status)
    );
}

void run_move_validation_tests(TestResults& results) {
    for (const auto& test : move_validation_cases()) {
        Board board;
        BoardTestAccess::load(board, test.position);

        const bool actual = board.valid_move(
            test.old_x,
            test.old_y,
            test.new_x,
            test.new_y
        );

        results.check(
            actual == test.expected_valid,
            "valid_move: " + test.name,
            "expected " + std::to_string(test.expected_valid)
                + ", got " + std::to_string(actual)
        );
    }
}

void run_state_transition_tests(TestResults& results) {
    for (const auto& test : state_transition_cases()) {
        Board board;
        BoardTestAccess::load(board, test.before);

        {
            SilenceCout silence;
            board.make_move(test.old_x, test.old_y, test.new_x, test.new_y);
        }

        const MockPosition actual_after = BoardTestAccess::snapshot(board);
        const std::size_t actual_undo_entries = BoardTestAccess::undo_size(board);

        results.check(
            same_position(actual_after, test.after)
                && actual_undo_entries == test.expected_undo_entries,
            "state transition: " + test.name,
            "state or undo-log size differed"
        );

        board.rollback_move();
        results.check(
            same_position(BoardTestAccess::snapshot(board), test.before),
            "rollback: " + test.name,
            "the original position was not fully restored"
        );
    }
}

void run_extended_chess_rule_tests(TestResults& results) {
    {
        Board board;
        MockPosition position{};
        position.squares[1][4] = 1;
        position.turn = 0;
        position.move_left = 2;
        BoardTestAccess::load(board, position);

        results.check(
            board.valid_move(1, 4, 3, 4),
            "chess rule: pawn may move two squares from its starting rank"
        );
    }

    {
        Board board;
        MockPosition position{};
        position.squares[0][3] = 6;
        position.squares[0][0] = 4;
        position.turn = 0;
        position.move_left = 2;
        BoardTestAccess::load(board, position);

        {
            SilenceCout silence;
            board.make_move(0, 3, 0, 1);
        }

        const MockPosition actual = BoardTestAccess::snapshot(board);
        const bool king_and_rook_moved = actual.squares[0][3] == 0
            && actual.squares[0][0] == 0
            && actual.squares[0][1] == 6
            && actual.squares[0][2] == 4;

        results.check(
            king_and_rook_moved,
            "chess rule: king-side castling moves both king and rook"
        );
    }

    {
        Board board;
        MockPosition position{};
        position.squares[6][0] = 1;
        position.turn = 0;
        position.move_left = 2;
        BoardTestAccess::load(board, position);

        {
            SilenceCout silence;
            board.make_move(6, 0, 7, 0);
        }

        results.check(
            BoardTestAccess::snapshot(board).squares[7][0] == 5,
            "chess rule: pawn promotes to a queen on the final rank"
        );

        board.rollback_move();
        results.check(
            same_position(BoardTestAccess::snapshot(board), position),
            "chess rule: rollback restores a pawn before promotion"
        );
    }

    {
        Board board;
        MockPosition position{};
        position.squares[4][4] = 1;
        position.squares[6][5] = -1;
        position.turn = 1;
        position.move_left = 1;
        BoardTestAccess::load(board, position);

        {
            SilenceCout silence;
            board.make_move(6, 5, 4, 5);
            board.make_move(4, 4, 5, 5);
        }

        const MockPosition actual = BoardTestAccess::snapshot(board);
        const bool en_passant_applied = actual.squares[4][4] == 0
            && actual.squares[4][5] == 0
            && actual.squares[5][5] == 1
            && actual.turn == 1;

        results.check(
            en_passant_applied,
            "chess rule: en passant captures a pawn after its double-step"
        );
    }

    {
        Board board;
        MockPosition position{};
        position.squares[0][3] = 6;
        position.squares[2][4] = -4;
        position.turn = 0;
        position.move_left = 2;
        BoardTestAccess::load(board, position);

        results.check(
            !board.valid_move(0, 3, 0, 4),
            "chess rule: king cannot move into check"
        );
    }

    {
        Board board;
        MockPosition position{};
        position.squares[0][3] = 6;
        position.squares[1][3] = 4;
        position.squares[7][3] = -4;
        position.turn = 0;
        position.move_left = 2;
        BoardTestAccess::load(board, position);

        results.check(
            !board.valid_move(1, 3, 1, 4),
            "chess rule: a pinned piece cannot expose its king"
        );
    }

    {
        Board board;
        MockPosition position{};
        position.squares[0][0] = 6;
        position.squares[1][1] = -5;
        position.squares[2][2] = -6;
        position.turn = 0;
        position.move_left = 2;
        BoardTestAccess::load(board, position);

        bool ended;
        {
            SilenceCout silence;
            ended = board.check_game_ended();
        }

        results.check(
            ended,
            "chess rule: checkmate ends the game before king capture"
        );
    }
}

void run_evaluation_tests(TestResults& results) {
    for (const auto& test : evaluation_cases()) {
        Board board;
        BoardTestAccess::load(board, test.position);
        const int actual = board.board_eval();

        results.check(
            actual == test.expected_score,
            "evaluation: " + test.name,
            "expected " + std::to_string(test.expected_score)
                + ", got " + std::to_string(actual)
        );
    }
}

void run_hash_tests(TestResults& results) {
    {
        Board board;
        results.check(
            BoardTestAccess::hash(board) == BoardTestAccess::recomputed_hash(board),
            "hash: initial key matches full recomputation",
            "the constructor's incremental key differs from the board state"
        );
    }

    for (const auto& test : state_transition_cases()) {
        Board board;
        BoardTestAccess::load(board, test.before);

        const Hash before_hash = BoardTestAccess::hash(board);

        {
            SilenceCout silence;
            board.make_move(test.old_x, test.old_y, test.new_x, test.new_y);
        }

        const bool position_changed =
            !same_position(BoardTestAccess::snapshot(board), test.before);
        const Hash after_hash = BoardTestAccess::hash(board);

        results.check(
            after_hash == BoardTestAccess::recomputed_hash(board),
            "hash recomputation: " + test.name,
            "incremental key differs from a full recomputation"
        );
        results.check(
            position_changed ? after_hash != before_hash : after_hash == before_hash,
            "hash transition: " + test.name,
            position_changed
                ? "an applied move did not change the key"
                : "a rejected move changed the key"
        );

        board.rollback_move();
        results.check(
            BoardTestAccess::hash(board) == before_hash
                && BoardTestAccess::hash(board) == BoardTestAccess::recomputed_hash(board),
            "hash rollback: " + test.name,
            "undo did not restore the original valid key"
        );
    }

    {
        Board board;
        MockPosition position{};
        position.squares[3][3] = 2;
        position.turn = 0;
        position.move_left = 2;

        BoardTestAccess::load(board, position);
        const Hash white_turn = BoardTestAccess::hash(board);

        position.turn = 1;
        BoardTestAccess::load(board, position);
        const Hash black_turn = BoardTestAccess::hash(board);

        position.turn = 0;
        position.move_left = 1;
        BoardTestAccess::load(board, position);
        const Hash one_move_left = BoardTestAccess::hash(board);

        results.check(
            white_turn != black_turn,
            "hash state: side to move changes the key"
        );
        results.check(
            white_turn != one_move_left,
            "hash state: moves remaining changes the key"
        );
    }

    {
        Board board;
        MockPosition start{};
        start.squares[3][2] = 2;
        start.squares[3][5] = 2;
        start.turn = 0;
        start.move_left = 2;

        BoardTestAccess::load(board, start);
        board.make_move(3, 2, 5, 1);
        board.make_move(3, 5, 5, 6);
        const MockPosition first_position = BoardTestAccess::snapshot(board);
        const Hash first_hash = BoardTestAccess::hash(board);

        BoardTestAccess::load(board, start);
        board.make_move(3, 5, 5, 6);
        board.make_move(3, 2, 5, 1);
        const MockPosition second_position = BoardTestAccess::snapshot(board);
        const Hash second_hash = BoardTestAccess::hash(board);

        results.check(
            same_position(first_position, second_position),
            "hash transposition setup reaches the same position"
        );
        results.check(
            first_hash == second_hash,
            "hash transposition: move order does not affect the key",
            "equivalent positions reached in different orders have different keys"
        );
        results.check(
            second_hash == BoardTestAccess::recomputed_hash(board),
            "hash transposition matches full recomputation"
        );
    }

    {
        Board first;
        Board second;
        results.check(
            BoardTestAccess::hash(first) == BoardTestAccess::hash(second),
            "hash key space is shared by equivalent Board instances",
            "each Board currently generates an independent random table"
        );
    }
}

void run_negamax_tests(TestResults& results) {
    for (const auto& test : negamax_cases()) {
        Board board;
        BoardTestAccess::load(board, test.position);
        const MockPosition before = BoardTestAccess::snapshot(board);

        int actual;
        {
            SilenceCout silence;
            actual = board.negamax(test.move_remaining, test.depth);
        }

        results.check(
            actual == test.expected_score,
            "negamax: " + test.name,
            "expected " + std::to_string(test.expected_score)
                + ", got " + std::to_string(actual)
        );
        results.check(
            same_position(BoardTestAccess::snapshot(board), before),
            "negamax restores state: " + test.name,
            "search changed the root position"
        );
    }
}

void run_alpha_beta_tests(TestResults& results) {
    MockPosition position{};
    position.squares[3][3] = 2;
    position.turn = 0;
    position.move_left = 2;

    Board board;
    BoardTestAccess::load(board, position);
    const MockPosition before = BoardTestAccess::snapshot(board);

    BoardTestAccess::reset_search_nodes(board);
    int full_window_score;
    {
        SilenceCout silence;
        full_window_score = board.negamax(2, 29, NEGINF, INF);
    }
    const std::size_t full_window_nodes = BoardTestAccess::search_nodes(board);

    results.check(
        full_window_score == 3,
        "alpha-beta: full window preserves the negamax score",
        "expected 3, got " + std::to_string(full_window_score)
    );
    results.check(
        same_position(BoardTestAccess::snapshot(board), before),
        "alpha-beta: full-window search restores the root state"
    );

    BoardTestAccess::load(board, position);
    BoardTestAccess::reset_search_nodes(board);
    int cutoff_window_score;
    {
        SilenceCout silence;
        cutoff_window_score = board.negamax(2, 29, 2, 3);
    }
    const std::size_t cutoff_window_nodes = BoardTestAccess::search_nodes(board);
    const std::size_t closed_window_nodes = BoardTestAccess::closed_window_nodes(board);

    std::cout << "[INFO] alpha-beta nodes: full=" << full_window_nodes
              << ", cutoff=" << cutoff_window_nodes
              << ", closed-window=" << closed_window_nodes << '\n';

    results.check(
        cutoff_window_score == full_window_score,
        "alpha-beta: cutoff window preserves the score",
        "full-window score=" + std::to_string(full_window_score)
            + ", cutoff score=" + std::to_string(cutoff_window_score)
    );
    results.check(
        cutoff_window_nodes < full_window_nodes,
        "alpha-beta: cutoff window visits fewer nodes",
        "full-window nodes=" + std::to_string(full_window_nodes)
            + ", cutoff-window nodes=" + std::to_string(cutoff_window_nodes)
    );
    results.check(
        closed_window_nodes == 0,
        "alpha-beta: search never recurses with a closed window",
        "closed-window recursive calls=" + std::to_string(closed_window_nodes)
    );
    results.check(
        same_position(BoardTestAccess::snapshot(board), before),
        "alpha-beta: pruned search restores the root state"
    );
    results.check(
        NEGINF == -INF,
        "alpha-beta: bounds are symmetric and safe to negate",
        "NEGINF must be -INF; INT32_MIN cannot be safely negated"
    );
}

} // namespace

int main() {
    TestResults results;

    run_initial_state_test(results);
    run_move_validation_tests(results);
    run_state_transition_tests(results);
    run_extended_chess_rule_tests(results);
    run_evaluation_tests(results);
    run_hash_tests(results);
    run_negamax_tests(results);
    run_alpha_beta_tests(results);

    return results.exit_code();
}
