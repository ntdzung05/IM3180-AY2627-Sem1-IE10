#pragma once

#include "../evaluator.hpp"

#include <array>

class NewEvaluator final : public Evaluator {
    StandardEvaluator standard_evaluator;
    EngineConfig config;

    static const std::array<int, 64> king_eg_sq_table;

    int evaluate_new_heuristic(const Board& board) const;

public:
    explicit NewEvaluator(EngineConfig evaluator_config = EngineConfig::standard());

    int evaluate(const Board& board) const override;
};
