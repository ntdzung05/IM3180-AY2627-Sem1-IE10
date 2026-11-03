#pragma once
// Mirror's additions: match-history pagination and a win-probability bar.
// Kept as free functions so the shared "UI files/ui.cpp" only needs to call
// into these, not host their implementation.

#include "../board.hpp"
#include <SFML/Graphics.hpp>
#include <cstddef>
#include <string>
#include <vector>

struct MatchHistoryPage {
    int rowsPerPage = 7;
    int currentPage = 0;
};

// Draws the "< Prev  Page X / Y  Next >" control under the match table.
void drawMatchHistoryPagination(
    sf::RenderWindow& window, sf::Font& font,
    std::size_t totalMatches, const MatchHistoryPage& page,
    float x, float y);

// Call from the MatchHistory click handler. Returns true if the click was
// consumed (hit Prev or Next), and updates page.currentPage in that case.
bool handleMatchHistoryPaginationClick(
    std::size_t totalMatches, MatchHistoryPage& page,
    float x, float y, float mouseX, float mouseY);

// Step-through replay of a saved match's move list. The board itself is
// rendered by the caller (ChessUI::drawBoardPosition), which has access to
// the current piece/board theme textures; this only owns the replay state,
// the Prev/Next/Close controls, and recomputing the board for a given move.
struct MatchReplayState {
    bool active = false;
    std::string white;
    std::string black;
    std::string date;
    std::string result;
    std::vector<Move> mainline;
    int moveIndex = -1; // -1 = starting position, size-1 = final position
    Board board;
};

void openMatchReplay(
    MatchReplayState& state,
    const std::string& white, const std::string& black,
    const std::string& date, const std::string& result,
    const std::vector<Move>& mainline);

void closeMatchReplay(MatchReplayState& state);

// delta is +1 or -1; clamps at the start/end of the move list.
void stepMatchReplay(MatchReplayState& state, int delta);

// Draws the "< PREV  Move X / Y  NEXT >  CLOSE" control row.
void drawMatchReplayControls(
    sf::RenderWindow& window, sf::Font& font,
    const MatchReplayState& state, float x, float y);

// Returns true if the click was consumed (hit Prev/Next/Close) and updates
// state accordingly.
bool handleMatchReplayControlsClick(
    MatchReplayState& state, float x, float y, float mouseX, float mouseY);

// A vertical evaluation bar in the Lichess/chess.com style: a narrow strip
// next to the board, filled white from the bottom and black from the top,
// the split point driven by a logistic curve over the engine's score
// (positive = White ahead) so it isn't just a raw, unbounded material count.
// The numeric label shows the actual evaluation in pawns (e.g. "+1.3"),
// not a percentage.
//
// The fill/number don't jump instantly to a new evaluation -- they glide
// there over EvaluationBarState::slideSeconds. Call updateEvaluationBar()
// once per frame (every frame, not just while the bar is visible, so the
// clock it uses to time the slide stays accurate) with the latest raw
// score, then drawEvaluationBar() to render the already-animated result.
struct EvaluationBarState {
    static constexpr float slideSeconds = 0.7f;
    double displayedPawns = 0.0;
    double fromPawns = 0.0;
    double toPawns = 0.0;
    sf::Clock animClock;
    bool initialized = false;
};

void updateEvaluationBar(EvaluationBarState& state, int evaluationScore);

void drawEvaluationBar(
    sf::RenderWindow& window, sf::Font& font,
    const EvaluationBarState& state, float x, float y, float width, float height);
