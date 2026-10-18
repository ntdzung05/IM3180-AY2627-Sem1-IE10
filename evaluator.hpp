#pragma once

#include "board.hpp"

#include <array>

struct EngineConfig {
    std::array<int, 64> pawn_sq_table{};
    std::array<int, 64> knight_sq_table{};
    std::array<int, 64> king_sq_table{};
    std::array<int, 7> piece_value{};
    std::array<int, 8> passed_pawn_bonus{};

    int knight_mobility_weight = 4;
    int bishop_mobility_weight = 3;
    int doubled_pawn_penalty = 20;
    int isolated_pawn_penalty = 15;
    int connected_rook_bonus = 25;
    int king_shield_bonus = 25;

    static EngineConfig standard();
};

class Evaluator {
public:
    virtual ~Evaluator() = default;
    virtual int evaluate(const Board& board) const = 0;
};

class StandardEvaluator final : public Evaluator {
    EngineConfig config;

public:
    explicit StandardEvaluator(EngineConfig config = EngineConfig::standard());

    int evaluate(const Board& board) const override;
    const EngineConfig& get_config() const;
};
