#include "extras.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace {
int pageCount(std::size_t totalMatches, int rowsPerPage) {
    if (totalMatches == 0) return 1;
    return static_cast<int>((totalMatches + rowsPerPage - 1) / rowsPerPage);
}
}

void drawMatchHistoryPagination(
    sf::RenderWindow& window, sf::Font& font,
    std::size_t totalMatches, const MatchHistoryPage& page,
    float x, float y) {

    const int pages = pageCount(totalMatches, page.rowsPerPage);
    const bool hasPrev = page.currentPage > 0;
    const bool hasNext = page.currentPage < pages - 1;

    sf::RectangleShape prevButton({90.f, 34.f});
    prevButton.setPosition({x, y});
    prevButton.setFillColor(hasPrev ? sf::Color(65, 65, 65) : sf::Color(42, 42, 42));
    window.draw(prevButton);

    sf::Text prevText(font, "< PREV", 14);
    prevText.setPosition({x + 14.f, y + 9.f});
    prevText.setFillColor(hasPrev ? sf::Color::White : sf::Color(110, 110, 110));
    window.draw(prevText);

    sf::Text pageText(font,
        "Page " + std::to_string(page.currentPage + 1) + " / " + std::to_string(pages), 14);
    pageText.setPosition({x + 110.f, y + 9.f});
    pageText.setFillColor(sf::Color(200, 200, 200));
    window.draw(pageText);

    sf::RectangleShape nextButton({90.f, 34.f});
    nextButton.setPosition({x + 280.f, y});
    nextButton.setFillColor(hasNext ? sf::Color(65, 65, 65) : sf::Color(42, 42, 42));
    window.draw(nextButton);

    sf::Text nextText(font, "NEXT >", 14);
    nextText.setPosition({x + 294.f, y + 9.f});
    nextText.setFillColor(hasNext ? sf::Color::White : sf::Color(110, 110, 110));
    window.draw(nextText);
}

bool handleMatchHistoryPaginationClick(
    std::size_t totalMatches, MatchHistoryPage& page,
    float x, float y, float mouseX, float mouseY) {

    const int pages = pageCount(totalMatches, page.rowsPerPage);

    if (mouseX >= x && mouseX <= x + 90.f && mouseY >= y && mouseY <= y + 34.f) {
        if (page.currentPage > 0) page.currentPage--;
        return true;
    }

    if (mouseX >= x + 280.f && mouseX <= x + 370.f && mouseY >= y && mouseY <= y + 34.f) {
        if (page.currentPage < pages - 1) page.currentPage++;
        return true;
    }

    return false;
}

void openMatchReplay(
    MatchReplayState& state,
    const std::string& white, const std::string& black,
    const std::string& date, const std::string& result,
    const std::vector<Move>& mainline) {

    state.active = true;
    state.white = white;
    state.black = black;
    state.date = date;
    state.result = result;
    state.mainline = mainline;
    state.moveIndex = -1;
    state.board = Board();
}

void closeMatchReplay(MatchReplayState& state) {
    state.active = false;
    state.mainline.clear();
    state.moveIndex = -1;
}

void stepMatchReplay(MatchReplayState& state, int delta) {
    const int lastIndex = static_cast<int>(state.mainline.size()) - 1;
    const int target = std::clamp(state.moveIndex + delta, -1, lastIndex);
    if (target == state.moveIndex)
        return;

    state.moveIndex = target;
    state.board = Board();
    for (int i = 0; i <= state.moveIndex; i++)
        state.board.make_move(state.mainline[static_cast<std::size_t>(i)]);
}

void drawMatchReplayControls(
    sf::RenderWindow& window, sf::Font& font,
    const MatchReplayState& state, float x, float y) {

    const int total = static_cast<int>(state.mainline.size());
    const bool hasPrev = state.moveIndex > -1;
    const bool hasNext = state.moveIndex < total - 1;

    auto drawButton = [&](float bx, const std::string& label, bool enabled, sf::Color fill) {
        sf::RectangleShape button({90.f, 34.f});
        button.setPosition({bx, y});
        button.setFillColor(enabled ? fill : sf::Color(42, 42, 42));
        window.draw(button);

        sf::Text text(font, label, 14);
        const sf::FloatRect bounds = text.getLocalBounds();
        text.setPosition({
            bx + (90.f - bounds.size.x) / 2.f - bounds.position.x,
            y + (34.f - bounds.size.y) / 2.f - bounds.position.y
        });
        text.setFillColor(enabled ? sf::Color::White : sf::Color(110, 110, 110));
        window.draw(text);
    };

    drawButton(x, "< PREV", hasPrev, sf::Color(65, 65, 65));

    const std::string moveLabel = state.moveIndex < 0
        ? ("Start  (" + std::to_string(total) + " moves)")
        : ("Move " + std::to_string(state.moveIndex + 1) + " / " + std::to_string(total));
    sf::Text moveText(font, moveLabel, 15);
    moveText.setPosition({x + 100.f, y + 9.f});
    moveText.setFillColor(sf::Color(210, 210, 210));
    window.draw(moveText);

    drawButton(x + 280.f, "NEXT >", hasNext, sf::Color(65, 65, 65));
    drawButton(x + 390.f, "CLOSE", true, sf::Color(110, 60, 60));
}

bool handleMatchReplayControlsClick(
    MatchReplayState& state, float x, float y, float mouseX, float mouseY) {

    if (!state.active)
        return false;

    auto hit = [&](float bx) {
        return mouseX >= bx && mouseX <= bx + 90.f && mouseY >= y && mouseY <= y + 34.f;
    };

    if (hit(x)) { stepMatchReplay(state, -1); return true; }
    if (hit(x + 280.f)) { stepMatchReplay(state, 1); return true; }
    if (hit(x + 390.f)) { closeMatchReplay(state); return true; }

    return false;
}

void updateEvaluationBar(EvaluationBarState& state, int evaluationScore) {
    // The evaluator this bar reads from (whiteEngine's) scores in
    // centipawns -- pawn = 100, queen = 900, a mate sentinel of +-999999 --
    // so convert to pawns first. Clamp to +-15 pawns: comfortably past
    // "winning" for a normal position, and it absorbs the mate sentinel
    // without the bar math blowing up.
    const double pawns = std::clamp(static_cast<double>(evaluationScore) / 100.0, -15.0, 15.0);

    if (!state.initialized) {
        state.initialized = true;
        state.fromPawns = state.toPawns = state.displayedPawns = pawns;
        state.animClock.restart();
        return;
    }

    if (pawns != state.toPawns) {
        // Re-target mid-slide from wherever it currently is, not from the
        // old target -- otherwise a second change arriving before the first
        // slide finishes would cause a visible snap back to toPawns first.
        state.fromPawns = state.displayedPawns;
        state.toPawns = pawns;
        state.animClock.restart();
    }

    const float t = std::clamp(
        state.animClock.getElapsedTime().asSeconds() / EvaluationBarState::slideSeconds,
        0.f, 1.f);
    // Ease-out, matching the piece-slide animation elsewhere in the UI.
    const float eased = 1.f - (1.f - t) * (1.f - t);
    state.displayedPawns = state.fromPawns + (state.toPawns - state.fromPawns) * eased;
}

void drawEvaluationBar(
    sf::RenderWindow& window, sf::Font& font,
    const EvaluationBarState& state, float x, float y, float width, float height) {

    const double pawns = state.displayedPawns;

    // Logistic curve mapping the pawn-scale score to a White-win likelihood,
    // used only to size the fill split (same convention Lichess/chess.com
    // use for their eval bars). A divisor of 3 keeps a one-pawn edge from
    // reading as a near-certain win while a few pawns clearly tilts the bar.
    const double whiteProbability = 1.0 / (1.0 + std::exp(-pawns / 3.0));

    sf::RectangleShape track({width, height});
    track.setPosition({x, y});
    track.setFillColor(sf::Color(35, 35, 35));
    window.draw(track);

    // White fills from the bottom up, black from the top down -- the
    // standard orientation for a vertical eval bar next to the board.
    const float whiteHeight = static_cast<float>(whiteProbability) * height;

    sf::RectangleShape whiteFill({width, whiteHeight});
    whiteFill.setPosition({x, y + height - whiteHeight});
    whiteFill.setFillColor(sf::Color(235, 235, 235));
    window.draw(whiteFill);

    sf::RectangleShape outline({width, height});
    outline.setPosition({x, y});
    outline.setFillColor(sf::Color::Transparent);
    outline.setOutlineColor(sf::Color(100, 100, 100));
    outline.setOutlineThickness(1.f);
    window.draw(outline);

    // The label is the actual evaluation in pawns (e.g. "+1.3", "-0.8"),
    // not a percentage -- this is what makes it an evaluation bar rather
    // than a win-probability bar. It sits on the leading side's end of the
    // bar, matching where Lichess places its number.
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "%+.1f", pawns);

    const unsigned int fontSize = std::max(15u, static_cast<unsigned int>(width * 0.55f));
    const bool whiteLeading = whiteProbability >= 0.5;

    sf::Text text(font, buffer, fontSize);
    text.setFillColor(whiteLeading ? sf::Color(30, 30, 30) : sf::Color(230, 230, 230));
    const sf::FloatRect bounds = text.getLocalBounds();
    const float textY = whiteLeading
        ? y + height - bounds.size.y - 8.f
        : y + 4.f;
    text.setPosition({
        x + (width - bounds.size.x) / 2.f - bounds.position.x,
        textY - bounds.position.y
    });
    window.draw(text);
}
