#include "board_test_data.hpp"

#include <initializer_list>
#include <tuple>

namespace {

using PiecePlacement = std::tuple<int, int, int>;

MockPosition make_position(
    std::initializer_list<PiecePlacement> pieces,
    int turn = 0,
    int move_left = 2,
    int game_status = 0
) {
    MockPosition position;
    position.turn = turn;
    position.move_left = move_left;
    position.game_status = game_status;

    for (const auto& [x, y, piece] : pieces) {
        position.squares[x][y] = piece;
    }

    return position;
}

MockPosition after_move(
    MockPosition position,
    int old_x,
    int old_y,
    int new_x,
    int new_y,
    int turn,
    int move_left,
    int game_status = 0
) {
    position.squares[new_x][new_y] = position.squares[old_x][old_y];
    position.squares[old_x][old_y] = 0;
    position.turn = turn;
    position.move_left = move_left;
    position.game_status = game_status;
    return position;
}

} // namespace

const std::vector<MoveValidationCase>& move_validation_cases() {
    static const std::vector<MoveValidationCase> cases = {
        {
            "valid white pawn quiet move",
            make_position({{3, 3, 1}}),
            3, 3, 4, 3, true
        },
        {
            "valid white pawn capture",
            make_position({{3, 3, 1}, {4, 4, -1}}),
            3, 3, 4, 4, true
        },
        {
            "pawn cannot capture an empty square",
            make_position({{3, 3, 1}}),
            3, 3, 4, 4, false
        },
        {
            "pawn cannot capture forward",
            make_position({{3, 3, 1}, {4, 3, -1}}),
            3, 3, 4, 3, false
        },
        {
            "valid knight move",
            make_position({{3, 3, 2}}),
            3, 3, 5, 4, true
        },
        {
            "empty source square",
            make_position({}),
            3, 3, 4, 3, false
        },
        {
            "source outside board",
            make_position({{3, 3, 2}}),
            -1, 3, 1, 4, false
        },
        {
            "destination outside board",
            make_position({{3, 3, 2}}),
            3, 3, 8, 4, false
        },
        {
            "cannot capture own piece",
            make_position({{3, 3, 4}, {3, 5, 1}}),
            3, 3, 3, 5, false
        },
        {
            "piece cannot use a different movement pattern",
            make_position({{3, 3, 4}}),
            3, 3, 5, 5, false
        },
        {
            "inactive side cannot move",
            make_position({{3, 3, -2}}, 0, 2),
            3, 3, 5, 4, false
        },
        {
            "rook can move on a clear rank",
            make_position({{3, 3, 4}}),
            3, 3, 3, 6, true
        },
        {
            "rook cannot jump over a blocker",
            make_position({{3, 3, 4}, {3, 4, 1}}),
            3, 3, 3, 6, false
        },
        {
            "bishop cannot jump over a blocker",
            make_position({{3, 3, 3}, {4, 4, 1}}),
            3, 3, 6, 6, false
        }
    };

    return cases;
}

const std::vector<StateTransitionCase>& state_transition_cases() {
    static const MockPosition first_quiet = make_position({{3, 3, 2}}, 0, 2);
    static const MockPosition second_quiet = make_position({{3, 3, 2}}, 0, 1);
    static const MockPosition first_capture = make_position({{3, 3, 2}, {5, 4, -1}}, 0, 2);
    static const MockPosition late_capture = make_position({{3, 3, 2}, {5, 4, -1}}, 0, 1);
    static const MockPosition white_king_capture = make_position({{3, 3, 4}, {3, 5, -6}}, 0, 2);
    static const MockPosition black_king_capture = make_position({{3, 3, -4}, {3, 5, 6}}, 1, 2);

    static const std::vector<StateTransitionCase> cases = {
        {
            "first quiet move keeps the turn",
            first_quiet,
            3, 3, 5, 4,
            after_move(first_quiet, 3, 3, 5, 4, 0, 1),
            1
        },
        {
            "second quiet move changes the turn",
            second_quiet,
            3, 3, 5, 4,
            after_move(second_quiet, 3, 3, 5, 4, 1, 2),
            1
        },
        {
            "capture on first move changes the turn",
            first_capture,
            3, 3, 5, 4,
            after_move(first_capture, 3, 3, 5, 4, 1, 2),
            1
        },
        {
            "capture with one move left is rejected",
            late_capture,
            3, 3, 5, 4,
            late_capture,
            0
        },
        {
            "white wins by capturing the black king",
            white_king_capture,
            3, 3, 3, 5,
            after_move(white_king_capture, 3, 3, 3, 5, 1, 2, 1),
            1
        },
        {
            "black wins by capturing the white king",
            black_king_capture,
            3, 3, 3, 5,
            after_move(black_king_capture, 3, 3, 3, 5, 0, 2, -1),
            1
        }
    };

    return cases;
}

const std::vector<EvaluationCase>& evaluation_cases() {
    static const std::vector<EvaluationCase> cases = {
        {
            "white material advantage on white turn",
            make_position({{3, 3, 4}}, 0, 2),
            5
        },
        {
            "white material advantage on black turn",
            make_position({{3, 3, 4}}, 1, 2),
            -5
        },
        {
            "black material advantage on black turn",
            make_position({{3, 3, -4}}, 1, 2),
            5
        },
        {
            "equal material",
            make_position({{3, 3, 4}, {4, 4, -4}}, 0, 2),
            0
        }
    };

    return cases;
}

const std::vector<NegamaxCase>& negamax_cases() {
    static const std::vector<NegamaxCase> cases = {
        {
            "one-turn knight search",
            make_position({{3, 3, 2}}, 0, 2),
            2,
            29,
            3
        },
        {
            "capture is preferred over losing material",
            make_position({{3, 3, 2}, {5, 4, -5}}, 0, 2),
            2,
            29,
            3
        },
        {
            "one-turn pawn search",
            make_position({{3, 3, 1}}, 0, 2),
            2,
            29,
            1
        }
    };

    return cases;
}
