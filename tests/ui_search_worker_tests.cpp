#include "../UI files/ui.hpp"
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

using namespace std::chrono_literals;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

struct SearchGate {
    std::mutex mutex;
    std::condition_variable changed;
    bool entered = false;
    bool released = false;
    bool timedOut = false;
    std::thread::id worker;
    int evaluations = 0;
    bool snapshotStable = true;
    bool throwOnEvaluation = false;
    bool evaluatorDestroyed = false;
    bool destroyedBeforeRelease = false;

    void release() {
        std::lock_guard<std::mutex> lock(mutex);
        released = true;
        changed.notify_all();
    }

    bool waitForEntry() {
        std::unique_lock<std::mutex> lock(mutex);
        return changed.wait_for(lock, 1s, [&] { return entered; });
    }
};

class GatedEvaluator final : public Evaluator {
    std::shared_ptr<SearchGate> gate;
public:
    explicit GatedEvaluator(std::shared_ptr<SearchGate> gate) : gate(std::move(gate)) {}
    ~GatedEvaluator() override {
        std::lock_guard<std::mutex> lock(gate->mutex);
        gate->evaluatorDestroyed = true;
        gate->destroyedBeforeRelease = gate->entered && !gate->released;
    }
    int evaluate(const Board& board) const override {
        const auto before = board.export_fen();
        std::unique_lock<std::mutex> lock(gate->mutex);
        ++gate->evaluations;
        gate->worker = std::this_thread::get_id();
        gate->entered = true;
        gate->changed.notify_all();
        if (!gate->changed.wait_for(lock, 2s, [&] { return gate->released; })) {
            gate->timedOut = true;
            gate->released = true;
        }
        gate->snapshotStable = gate->snapshotStable && board.export_fen() == before;
        if (gate->throwOnEvaluation) throw std::runtime_error("controlled evaluator failure");
        return 0;
    }
};

struct ReleaseOnExit {
    std::shared_ptr<SearchGate> gate;
    ~ReleaseOnExit() { gate->release(); }
};

struct ChessUITestAccess {
    static void start(ChessUI& ui) {
        ui.window.setVisible(false);
        ui.soundEffects = false;
        ui.startGame(-1, ChessUI::GameMode::EngineVsEngine);
        ui.slidePieces = false;
    }
    static void update(ChessUI& ui) { ui.updateEngine(); }
    static std::string position(const ChessUI& ui) { return ui.game.export_fen(); }
    static std::size_t moves(const ChessUI& ui) { return ui.historyTree.size(); }
    static bool stalled(const ChessUI& ui) { return ui.engineStalled; }
    static bool failed(const ChessUI& ui) { return ui.engineSearchFailed; }
    static bool pending(const ChessUI& ui) { return ui.searchFuture.valid(); }
    static bool ready(ChessUI& ui) {
        return ui.searchFuture.valid() && ui.searchFuture.wait_for(0s) == std::future_status::ready;
    }
    static void seedHistory(ChessUI& ui) {
        require(ui.applyMove(ui.game.legal_moves().front()), "could not seed history");
        require(ui.applyMove(ui.game.legal_moves().front()), "could not seed second history node");
    }
    static void frame(ChessUI& ui) { ui.handleEvents(); ui.draw(); ui.updateEngine(); }
    static bool flipped(const ChessUI& ui) { return ui.manualBoardFlip; }
    static void configure(ChessUI& ui, int mode, int engineSide, int turn) {
        ui.startGame(engineSide, mode == 0 ? ChessUI::GameMode::PlayerVsPlayer
            : (mode == 1 ? ChessUI::GameMode::PlayerVsEngine : ChessUI::GameMode::EngineVsEngine));
        require(ui.game.import_fen(std::string("3k4/8/8/8/8/8/8/3K4 ") +
                (turn == 0 ? "w" : "b") + " - 2 0 1"), "mode fixture import failed");
        ui.startingBoard = ui.game;
    }
    static void slide(ChessUI& ui) { ui.slidePieces = true; }
    static void startHumanGame(ChessUI& ui) {
        ui.startGame(-1, ChessUI::GameMode::PlayerVsPlayer);
        ui.slidePieces = true;
        ui.dragPieces = false;
        // Isolate worker timing from the existing click-slide handler's missing animatedMove assignment.
        ui.animatedMove = Move{1, 0, 2, 0};
    }
    static bool animating(const ChessUI& ui) { return ui.engineMoveAnimating; }
    static void noLegalMoves(ChessUI& ui) {
        require(ui.game.import_fen("PPPPPPPk/PPPPPPPP/PPPPPPPP/PPPPPPPP/PPPPPPPP/PPPPPPPP/PPPPPPPP/KPPPPPPP w - 1 0 1"),
                "no-legal-move fixture import failed");
        require(!ui.game.has_game_ended() && ui.game.legal_moves().empty(), "fixture has legal moves or is terminal");
        ui.startingBoard = ui.game;
    }
    static void backToGame(ChessUI& ui) { ui.currentScreen = ChessUI::Screen::Game; }
    static bool settings(const ChessUI& ui) { return ui.currentScreen == ChessUI::Screen::Settings; }
    static bool open(const ChessUI& ui) { return ui.window.isOpen(); }
#ifdef _WIN32
    static void click(ChessUI& ui, int x, int y) {
        SendMessageW(ui.window.getNativeHandle(), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(x, y));
        SendMessageW(ui.window.getNativeHandle(), WM_LBUTTONUP, 0, MAKELPARAM(x, y));
        ui.handleEvents();
    }
    static void navigate(ChessUI& ui, int node) {
        const auto position = ui.getHistoryNodePositions().at(node);
        click(ui, static_cast<int>(position.x + 20), static_cast<int>(position.y - ui.historyScroll + 20));
        require(ui.historyPaused && ui.currentHistoryNode == node, "history click did not navigate and pause");
    }
    static void resume(ChessUI& ui) {
        click(ui, 861, 38);
        require(!ui.historyPaused, "PLAY click did not resume");
    }
    static void close(ChessUI& ui) {
        SendMessageW(ui.window.getNativeHandle(), WM_CLOSE, 0, 0);
        ui.run();
    }
#endif
};

void waitForResult(ChessUI& ui) {
    const auto deadline = std::chrono::steady_clock::now() + 1s;
    while (!ChessUITestAccess::ready(ui) && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(2ms);
    require(ChessUITestAccess::ready(ui), "worker did not finish within test deadline");
}

void test_search_launch_does_not_block_ui() {
    auto gate = std::make_shared<SearchGate>();
    ChessUI ui(1, 1, std::make_unique<GatedEvaluator>(gate));
    ReleaseOnExit release{gate};
    ChessUITestAccess::start(ui);
    const auto original = ChessUITestAccess::position(ui);
    std::this_thread::sleep_for(550ms);
    const auto start = std::chrono::steady_clock::now();
    ChessUITestAccess::update(ui);
    const auto elapsed = std::chrono::steady_clock::now() - start;
    std::cout << "launch_ms=" << std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() << '\n';
    require(elapsed < 400ms, "search launch blocked the UI on the gated evaluator");
    require(gate->waitForEntry(), "search did not reach the evaluator");
    {
        std::lock_guard<std::mutex> lock(gate->mutex);
        require(gate->worker != std::this_thread::get_id(), "search ran on the UI thread");
        require(!gate->timedOut, "test watchdog released search before assertion");
    }
    require(ChessUITestAccess::position(ui) == original, "worker changed the live board");
    const auto framesStart = std::chrono::steady_clock::now();
#ifdef _WIN32
    const bool flipped = ChessUITestAccess::flipped(ui);
    ChessUITestAccess::click(ui, 775, 38);
    require(ChessUITestAccess::flipped(ui) != flipped, "flip event was not handled during search");
#endif
    for (int frame = 0; frame < 3; ++frame) ChessUITestAccess::frame(ui);
    const auto framesElapsed = std::chrono::steady_clock::now() - framesStart;
    std::cout << "gated_frames_ms=" << std::chrono::duration_cast<std::chrono::milliseconds>(framesElapsed).count() << '\n';
    require(framesElapsed < 400ms, "event/draw/update frames blocked on search");
    std::lock_guard<std::mutex> lock(gate->mutex);
    require(gate->evaluations == 1 && !gate->timedOut, "frames launched duplicate workers or waited for watchdog");
}

void test_restart_same_position_discards_old_result() {
    auto gate = std::make_shared<SearchGate>();
    ChessUI ui(1, 1, std::make_unique<GatedEvaluator>(gate));
    ReleaseOnExit release{gate};
    ChessUITestAccess::start(ui);
    const auto original = ChessUITestAccess::position(ui);
    std::this_thread::sleep_for(550ms);
    ChessUITestAccess::update(ui);
    require(gate->waitForEntry(), "restart fixture search never started");
    const auto start = std::chrono::steady_clock::now();
    ChessUITestAccess::start(ui);
    ChessUITestAccess::update(ui);
    require(std::chrono::steady_clock::now() - start < 400ms, "restart waited for active search");
    std::this_thread::sleep_for(550ms);
    ChessUITestAccess::update(ui);
    {
        std::lock_guard<std::mutex> lock(gate->mutex);
        require(gate->evaluations == 1 && !gate->timedOut, "restart launched overlapping search");
    }
    gate->release();
    waitForResult(ui);
    ChessUITestAccess::update(ui);
    require(ChessUITestAccess::moves(ui) == 0 && ChessUITestAccess::position(ui) == original,
            "restart to the same position consumed the old search result");
    ChessUITestAccess::update(ui);
    waitForResult(ui);
    ChessUITestAccess::update(ui);
    require(ChessUITestAccess::moves(ui) == 1, "replacement search did not apply its result");
    for (int frame = 0; frame < 5; ++frame) ChessUITestAccess::update(ui);
    require(ChessUITestAccess::moves(ui) == 1, "result was consumed more than once");
}

#ifdef _WIN32
void test_history_navigation_discards_old_result() {
    for (int node : {1, 0}) {
        auto gate = std::make_shared<SearchGate>();
        ChessUI ui(1, 1, std::make_unique<GatedEvaluator>(gate), std::make_unique<GatedEvaluator>(gate));
        ReleaseOnExit release{gate};
        ChessUITestAccess::start(ui);
        ChessUITestAccess::seedHistory(ui);
        std::this_thread::sleep_for(550ms);
        ChessUITestAccess::update(ui);
        require(gate->waitForEntry(), "navigation fixture search never started");
        ChessUITestAccess::navigate(ui, node);
        const auto selected = ChessUITestAccess::position(ui);
        ChessUITestAccess::resume(ui);
        std::this_thread::sleep_for(550ms);
        ChessUITestAccess::update(ui);
        gate->release();
        waitForResult(ui);
        ChessUITestAccess::update(ui);
        require(ChessUITestAccess::moves(ui) == 2 && ChessUITestAccess::position(ui) == selected,
                "history navigation/resume consumed the invalidated search result");
        std::lock_guard<std::mutex> lock(gate->mutex);
        require(gate->snapshotStable && !gate->timedOut, "live navigation changed the worker board snapshot");
    }
}
#endif

void test_worker_exception_is_handled_on_ui_thread() {
    auto gate = std::make_shared<SearchGate>();
    gate->throwOnEvaluation = true;
    ChessUI ui(1, 1, std::make_unique<GatedEvaluator>(gate));
    ReleaseOnExit release{gate};
    ChessUITestAccess::start(ui);
    std::this_thread::sleep_for(550ms);
    ChessUITestAccess::update(ui);
    require(gate->waitForEntry(), "exception fixture search never started");
    gate->release();
    waitForResult(ui);
    bool escaped = false;
    try { ChessUITestAccess::update(ui); }
    catch (...) { escaped = true; }
    require(!escaped, "worker exception escaped UI result polling");
    require(ChessUITestAccess::stalled(ui) && ChessUITestAccess::failed(ui) && ChessUITestAccess::moves(ui) == 0 &&
            !ChessUITestAccess::pending(ui), "failed search was applied or not stopped");
    for (int frame = 0; frame < 5; ++frame) ChessUITestAccess::update(ui);
    require(!ChessUITestAccess::pending(ui), "failed search retried every frame");
    gate->throwOnEvaluation = false;
    ChessUITestAccess::start(ui);
    std::this_thread::sleep_for(550ms);
    ChessUITestAccess::update(ui);
    waitForResult(ui);
    ChessUITestAccess::update(ui);
    require(ChessUITestAccess::moves(ui) == 1 && !ChessUITestAccess::stalled(ui) && !ChessUITestAccess::failed(ui),
            "restart did not recover after a handled worker exception");
}

void test_only_one_worker_across_different_engine_requests() {
    auto white = std::make_shared<SearchGate>();
    auto black = std::make_shared<SearchGate>();
    ChessUI ui(1, 1, std::make_unique<GatedEvaluator>(white), std::make_unique<GatedEvaluator>(black));
    ReleaseOnExit releaseWhite{white}, releaseBlack{black};
    ChessUITestAccess::start(ui);
    std::this_thread::sleep_for(550ms);
    ChessUITestAccess::update(ui);
    require(white->waitForEntry(), "white search did not start");
    ChessUITestAccess::configure(ui, 2, 0, 1);
    std::this_thread::sleep_for(550ms);
    ChessUITestAccess::update(ui);
    {
        std::lock_guard<std::mutex> lock(black->mutex);
        require(!black->entered, "black worker overlapped an invalidated white worker");
    }
    white->release();
    waitForResult(ui);
    ChessUITestAccess::update(ui);
    require(ChessUITestAccess::moves(ui) == 0, "old white result applied to black position");
    ChessUITestAccess::update(ui);
    require(black->waitForEntry(), "replacement black search did not start after white finished");
    black->release();
    waitForResult(ui);
    ChessUITestAccess::update(ui);
    require(ChessUITestAccess::moves(ui) == 1 && !ChessUITestAccess::stalled(ui), "replacement black search did not apply");
}

void test_restart_discards_an_already_ready_result() {
    auto gate = std::make_shared<SearchGate>();
    gate->release();
    ChessUI ui(1, 1, std::make_unique<GatedEvaluator>(gate));
    ChessUITestAccess::start(ui);
    const auto before = ChessUITestAccess::position(ui);
    std::this_thread::sleep_for(550ms);
    ChessUITestAccess::update(ui);
    waitForResult(ui);
    ChessUITestAccess::start(ui);
    std::this_thread::sleep_for(550ms);
    ChessUITestAccess::update(ui);
    require(ChessUITestAccess::moves(ui) == 0 && ChessUITestAccess::position(ui) == before &&
            !ChessUITestAccess::pending(ui), "restart consumed an already completed stale result");
}

void test_modes_and_sides_use_only_the_assigned_engine() {
    auto white = std::make_shared<SearchGate>();
    auto black = std::make_shared<SearchGate>();
    white->release();
    black->release();
    ChessUI ui(1, 1, std::make_unique<GatedEvaluator>(white), std::make_unique<GatedEvaluator>(black));
    ChessUITestAccess::start(ui);
    for (int mode : {0, 1, 2}) {
        for (int side : {0, 1}) {
            for (int turn : {0, 1}) {
                ChessUITestAccess::configure(ui, mode, side, turn);
                const auto before = ChessUITestAccess::position(ui);
                const int whiteBefore = white->evaluations, blackBefore = black->evaluations;
                std::this_thread::sleep_for(550ms);
                ChessUITestAccess::update(ui);
                const bool engineTurn = mode == 2 || (mode == 1 && side == turn);
                require(ChessUITestAccess::pending(ui) == engineTurn, "mode/side launched an unassigned engine or no engine");
                if (engineTurn) {
                    waitForResult(ui);
                    require(ChessUITestAccess::position(ui) == before && ChessUITestAccess::moves(ui) == 0,
                            "ready worker applied the move without UI consumption");
                    ChessUITestAccess::update(ui);
                    require(ChessUITestAccess::moves(ui) == 1 && !ChessUITestAccess::stalled(ui), "assigned engine move was not applied");
                    // A warm TT search need not call its evaluator again.
                    require(turn == 0 ? black->evaluations == blackBefore : white->evaluations == whiteBefore,
                            "worker touched the unassigned evaluator");
                } else {
                    require(ChessUITestAccess::position(ui) == before && white->evaluations == whiteBefore &&
                            black->evaluations == blackBefore, "human turn invoked search");
                }
            }
        }
    }
}

void test_delay_and_slide_gates_are_preserved() {
    auto gate = std::make_shared<SearchGate>();
    gate->release();
    ChessUI ui(1, 1, std::make_unique<GatedEvaluator>(gate));
    ChessUITestAccess::start(ui);
    ChessUITestAccess::slide(ui);
    ChessUITestAccess::update(ui);
    require(!ChessUITestAccess::pending(ui), "search skipped the existing pre-search delay");
    std::this_thread::sleep_for(550ms);
    ChessUITestAccess::update(ui);
    waitForResult(ui);
    ChessUITestAccess::update(ui);
    require(ChessUITestAccess::animating(ui) && ChessUITestAccess::moves(ui) == 0 &&
            !ChessUITestAccess::pending(ui), "search result skipped slide animation or remained pending");
    ChessUITestAccess::update(ui);
    require(ChessUITestAccess::animating(ui) && ChessUITestAccess::moves(ui) == 0, "slide applied its move too early");
    std::this_thread::sleep_for(350ms);
    ChessUITestAccess::update(ui);
    require(!ChessUITestAccess::animating(ui) && ChessUITestAccess::moves(ui) == 1, "slide did not apply exactly one move");
    ChessUITestAccess::update(ui);
    require(!ChessUITestAccess::pending(ui) && ChessUITestAccess::moves(ui) == 1, "animation started a duplicate search/move");
}

void test_false_search_stops_without_applying_a_move() {
    auto gate = std::make_shared<SearchGate>();
    gate->release();
    ChessUI ui(1, 1, std::make_unique<GatedEvaluator>(gate));
    ChessUITestAccess::start(ui);
    ChessUITestAccess::noLegalMoves(ui);
    const auto before = ChessUITestAccess::position(ui);
    std::this_thread::sleep_for(550ms);
    ChessUITestAccess::update(ui);
    waitForResult(ui);
    ChessUITestAccess::update(ui);
    require(ChessUITestAccess::stalled(ui) && !ChessUITestAccess::failed(ui) &&
            ChessUITestAccess::position(ui) == before && ChessUITestAccess::moves(ui) == 0,
            "false search was treated as a move or an exception");
    ChessUITestAccess::update(ui);
    require(!ChessUITestAccess::pending(ui), "false search retried every frame");
}

void test_stale_exception_is_discarded_and_restart_recovers() {
    auto gate = std::make_shared<SearchGate>();
    gate->throwOnEvaluation = true;
    ChessUI ui(1, 1, std::make_unique<GatedEvaluator>(gate));
    ReleaseOnExit release{gate};
    ChessUITestAccess::start(ui);
    std::this_thread::sleep_for(550ms);
    ChessUITestAccess::update(ui);
    require(gate->waitForEntry(), "stale-exception search never started");
    ChessUITestAccess::start(ui);
    gate->release();
    waitForResult(ui);
    ChessUITestAccess::update(ui);
    require(!ChessUITestAccess::stalled(ui) && !ChessUITestAccess::pending(ui) && ChessUITestAccess::moves(ui) == 0,
            "stale exception stopped the restarted game");
    gate->throwOnEvaluation = false;
    std::this_thread::sleep_for(550ms);
    ChessUITestAccess::update(ui);
    waitForResult(ui);
    ChessUITestAccess::update(ui);
    require(ChessUITestAccess::moves(ui) == 1 && !ChessUITestAccess::stalled(ui), "restart did not recover after stale exception");
}

#ifdef _WIN32
void test_settings_holds_ready_result_until_game_resumes() {
    auto gate = std::make_shared<SearchGate>();
    ChessUI ui(1, 1, std::make_unique<GatedEvaluator>(gate));
    ReleaseOnExit release{gate};
    ChessUITestAccess::start(ui);
    std::this_thread::sleep_for(550ms);
    ChessUITestAccess::update(ui);
    require(gate->waitForEntry(), "settings search never started");
    ChessUITestAccess::click(ui, 943, 38);
    require(ChessUITestAccess::settings(ui), "settings event not handled during search");
    ChessUITestAccess::frame(ui);
    gate->release();
    waitForResult(ui);
    ChessUITestAccess::update(ui);
    require(ChessUITestAccess::pending(ui) && ChessUITestAccess::moves(ui) == 0, "settings consumed ready search result");
    ChessUITestAccess::backToGame(ui);
    ChessUITestAccess::update(ui);
    require(ChessUITestAccess::moves(ui) == 1, "return from settings lost the valid result");
}

void test_stale_search_does_not_delay_human_animation() {
    auto gate = std::make_shared<SearchGate>();
    ChessUI ui(1, 1, std::make_unique<GatedEvaluator>(gate));
    ReleaseOnExit release{gate};
    ChessUITestAccess::start(ui);
    std::this_thread::sleep_for(550ms);
    ChessUITestAccess::update(ui);
    require(gate->waitForEntry(), "human-animation search never started");
    ChessUITestAccess::startHumanGame(ui);
    ChessUITestAccess::click(ui, 90, 200);
    ChessUITestAccess::click(ui, 90, 280);
    require(ChessUITestAccess::animating(ui), "human move click did not start animation");
    std::this_thread::sleep_for(350ms);
    ChessUITestAccess::update(ui);
    require(ChessUITestAccess::moves(ui) == 1 && !ChessUITestAccess::animating(ui),
            "invalidated search delayed the new human game's animation");
    std::lock_guard<std::mutex> lock(gate->mutex);
    require(!gate->released && !gate->timedOut, "old search was not still gated during human move");
}

void test_close_returns_but_destruction_joins_worker() {
    for (bool throws : {false, true}) {
        auto gate = std::make_shared<SearchGate>();
        gate->throwOnEvaluation = throws;
        auto ui = std::make_unique<ChessUI>(1, 1, std::make_unique<GatedEvaluator>(gate));
        ReleaseOnExit release{gate};
        ChessUITestAccess::start(*ui);
        std::this_thread::sleep_for(550ms);
        ChessUITestAccess::update(*ui);
        require(gate->waitForEntry(), "shutdown search never started");
        const auto closeStart = std::chrono::steady_clock::now();
        ChessUITestAccess::close(*ui);
        require(!ChessUITestAccess::open(*ui) && std::chrono::steady_clock::now() - closeStart < 400ms,
                "window close/run waited for search");
        std::thread releaser([gate] { std::this_thread::sleep_for(150ms); gate->release(); });
        const auto destroyStart = std::chrono::steady_clock::now();
        ui.reset();
        const auto elapsed = std::chrono::steady_clock::now() - destroyStart;
        releaser.join();
        require(elapsed >= 100ms, "UI destruction returned without joining active search");
        require(gate->evaluatorDestroyed && !gate->destroyedBeforeRelease && !gate->timedOut,
                "engine/evaluator destroyed before worker completed, or watchdog fired");
        std::cout << "shutdown_join_ms=" << std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() << '\n';
    }
}
#endif

int main() {
    try {
        test_search_launch_does_not_block_ui();
        std::cout << "PASS search_launch_does_not_block_ui\n";
        test_restart_same_position_discards_old_result();
        std::cout << "PASS restart_same_position_discards_old_result\n";
#ifdef _WIN32
        test_history_navigation_discards_old_result();
        std::cout << "PASS history_navigation_discards_old_result (same/different board)\n";
#endif
        test_worker_exception_is_handled_on_ui_thread();
        std::cout << "PASS worker_exception_is_handled_on_ui_thread\n";
        test_only_one_worker_across_different_engine_requests();
        std::cout << "PASS only_one_worker_across_different_engine_requests\n";
        test_restart_discards_an_already_ready_result();
        std::cout << "PASS restart_discards_an_already_ready_result\n";
        test_modes_and_sides_use_only_the_assigned_engine();
        std::cout << "PASS modes_and_sides_use_only_the_assigned_engine\n";
        test_delay_and_slide_gates_are_preserved();
        std::cout << "PASS delay_and_slide_gates_are_preserved\n";
        test_false_search_stops_without_applying_a_move();
        std::cout << "PASS false_search_stops_without_applying_a_move\n";
        test_stale_exception_is_discarded_and_restart_recovers();
        std::cout << "PASS stale_exception_is_discarded_and_restart_recovers\n";
#ifdef _WIN32
        test_settings_holds_ready_result_until_game_resumes();
        std::cout << "PASS settings_holds_ready_result_until_game_resumes\n";
        test_stale_search_does_not_delay_human_animation();
        std::cout << "PASS stale_search_does_not_delay_human_animation\n";
        test_close_returns_but_destruction_joins_worker();
        std::cout << "PASS close_returns_but_destruction_joins_worker\n";
#endif
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
}
