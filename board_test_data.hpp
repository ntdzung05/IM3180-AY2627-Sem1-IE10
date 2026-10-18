#pragma once

#include <array>
#include <string>
#include <vector>

using MockBoard = std::array<std::array<int, 8>, 8>;

struct MockPosition {
    MockBoard squares{};
    int turn = 0;
    int move_left = 2;
    int game_status = 0;
};

struct MoveValidationCase {
    std::string name;
    MockPosition position;
    int old_x;
    int old_y;
    int new_x;
    int new_y;
    bool expected_valid;
};

struct StateTransitionCase {
    std::string name;
    MockPosition before;
    int old_x;
    int old_y;
    int new_x;
    int new_y;
    MockPosition after;
    std::size_t expected_undo_entries;
};

struct EvaluationCase {
    std::string name;
    MockPosition position;
    int expected_score;
};

struct NegamaxCase {
    std::string name;
    MockPosition position;
    int move_remaining;
    int depth;
    int expected_score;
};

const std::vector<MoveValidationCase>& move_validation_cases();
const std::vector<StateTransitionCase>& state_transition_cases();
const std::vector<EvaluationCase>& evaluation_cases();
const std::vector<NegamaxCase>& negamax_cases();

