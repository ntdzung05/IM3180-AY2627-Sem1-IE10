#include "ui.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <iomanip>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {
bool gLightMode = false;

// Maps the dark-theme greys to light-theme greys; saturated and translucent colours are kept.
sf::Color textWhite() {
    return gLightMode ? sf::Color(25, 25, 28) : sf::Color::White;
}

sf::Color uiColor(int r, int g, int b, int a = 255) {
    sf::Color c(static_cast<std::uint8_t>(r), static_cast<std::uint8_t>(g),
                static_cast<std::uint8_t>(b), static_cast<std::uint8_t>(a));
    if (!gLightMode || a < 255)
        return c;
    if (std::max({r, g, b}) - std::min({r, g, b}) > 24)
        return c;
    return sf::Color(static_cast<std::uint8_t>(255 - r), static_cast<std::uint8_t>(255 - g),
                     static_cast<std::uint8_t>(255 - b), 255);
}
}

namespace {
constexpr float topFlipX = 740.f;
constexpr float topPlayX = 826.f;
constexpr float topSettingsX = 908.f;
constexpr float topProfileX = 1086.f;
constexpr float topProfileRadius = 27.f;


void loadDefaultProfilePicture(sf::Texture& texture, std::string& path) {
    static_cast<void>(texture.loadFromFile("assets/pfp/default_pfp.jpe"));
}

SpecialMove promoPieceToSpecial(int piece) {
    switch (std::abs(piece)) {
        case 5: return SpecialMove::PromoteQueen;
        case 4: return SpecialMove::PromoteRook;
        case 3: return SpecialMove::PromoteBishop;
        case 2: return SpecialMove::PromoteKnight;
        default: return SpecialMove::None;
    }
}

int specialToPromoPiece(SpecialMove special) {
    switch (special) {
        case SpecialMove::PromoteQueen: return 5;
        case SpecialMove::PromoteRook: return 4;
        case SpecialMove::PromoteBishop: return 3;
        case SpecialMove::PromoteKnight: return 2;
        default: return 0;
    }
}
}

ChessUI::ChessUI(int engineSide, int searchDepth,
                 std::unique_ptr<Evaluator> whiteEvaluator,
                 std::unique_ptr<Evaluator> blackEvaluator)
: game(),
whiteEngine(0, searchDepth, std::move(whiteEvaluator)),
blackEngine(1, searchDepth, std::move(blackEvaluator)),
engineSide(engineSide),
window( sf::VideoMode({1200, 800}), "Chess AI", sf::Style::Default, sf::State::Windowed), 
engineSearchDepth(searchDepth), startingBoard(game) {
    if(searchDepth < 1) throw std::invalid_argument("searchDepth must be positive");
    window.setFramerateLimit(60);
    updateResponsiveView(window.getSize().x, window.getSize().y);
    loadDefaultProfilePicture(profilePictureTexture, profilePicturePath);
    refreshPieceThemes();
    loadPieceTextures();
    refreshBoardThemes();
    loadBoardTextures();


    static_cast<void>(font.openFromFile("assets/fonts/font.ttf"));
    static_cast<void>(startBackgroundTexture.loadFromFile("assets/design/start_background.png"));
    static_cast<void>(selectionFrameTexture.loadFromFile("assets/design/selection_frames/frame001.jpg"));
    static_cast<void>(moveSoundBuffer.loadFromFile("assets/sounds/move.wav"));
    static_cast<void>(captureSoundBuffer.loadFromFile("assets/sounds/capture.wav"));
    static_cast<void>(uiClickSoundBuffer.loadFromFile("assets/sounds/ui_click.wav"));
    static_cast<void>(victorySoundBuffer.loadFromFile("assets/sounds/victory.wav"));
}

void ChessUI::run() {
    while (window.isOpen()) {
        handleEvents();
        if (!window.isOpen()) break;
        draw();
        updateClocks();
        updateEngine();
    }
}

void ChessUI::updateClocks() {
    float dt = clockTimer.restart().asSeconds();
    if (dt > 5.f)
        dt = 5.f;

    if (currentScreen != Screen::Game || !gameStarted ||
        game.has_game_ended() || historyPaused || timeExpired ||
        gameMode == GameMode::EngineVsEngine)
        return;

    int side = game.get_current_turn();

    if (clockKeyNode != currentHistoryNode ||
        clockKeyTurn != side ||
        clockKeyMovesLeft != game.get_moves_left()) {
        clockKeyNode = currentHistoryNode;
        clockKeyTurn = side;
        clockKeyMovesLeft = game.get_moves_left();
        moveTimeLeft = matchSeconds;
        return;
    }

    float& left = moveTimeLeft;
    left -= dt;

    if (left <= 0.f) {
        left = 0.f;
        timeExpired = true;
        timeoutLoser = side;
        engineMoveAnimating = false;
        animatedPiece = 0;
        dragging = false;
        clearSelection();
        playSound(victorySoundBuffer);
        currentScreen = Screen::EndGame;
    }
}

bool ChessUI::applyMove(int fromRow, int fromCol, int toRow, int toCol, int promotionPiece) {
    if (promotionPiece != 0) {
        promotionPiece = std::abs(promotionPiece);
        if (promotionPiece != 2 &&
            promotionPiece != 3 &&
            promotionPiece != 4 &&
            promotionPiece != 5) {
            return false;
        }
    }
    return applyMove(Move{fromRow, fromCol, toRow, toCol,
                          promoPieceToSpecial(promotionPiece)});
}

bool ChessUI::applyMove(const Move& requested) {
    if (!game.valid_move(requested.old_x, requested.old_y,
                         requested.new_x, requested.new_y))
        return false;

    const int fromRow = requested.old_x;
    const int fromCol = requested.old_y;
    const int toRow = requested.new_x;
    const int toCol = requested.new_y;

    int movedPiece = (*(game.begin() + fromRow))[fromCol];
    int capturedPiece = (*(game.begin() + toRow))[toCol];

    const bool promotionMove = std::abs(movedPiece) == 1 && ((movedPiece > 0 && toRow == 7) || (movedPiece < 0 && toRow == 0));

    SpecialMove special = requested.special_move;
    if (promotionMove) {
        if (specialToPromoPiece(special) == 0) {
            if (engineTurn()) {
                special = SpecialMove::PromoteQueen;
            }
            else {
                promotionFromRow = fromRow;
                promotionFromCol = fromCol;
                promotionToRow = toRow;
                promotionToCol = toCol;
                currentScreen = Screen::Promotion;
                return true;
            }
        }
    }
    else if (special != SpecialMove::None &&
             special != SpecialMove::CastleKingside &&
             special != SpecialMove::CastleQueenside) {
        return false;
    }

    Move applied = requested;
    applied.special_move = special;
    if (!game.make_move(applied)) return false;

    if (promotionMove) {
        promotionFromRow = -1;
        promotionFromCol = -1;
        promotionToRow = -1;
        promotionToCol = -1;
    }

    const int entryPromotionPiece = promotionMove
        ? (movedPiece < 0 ? -specialToPromoPiece(special) : specialToPromoPiece(special))
        : 0;

    MoveHistoryEntry entry{
        movedPiece,
        fromRow,
        fromCol,
        toRow,
        toCol,
        capturedPiece,
        entryPromotionPiece,
        game
    };

    int newNode = static_cast<int>(historyTree.size());
    historyTree.push_back({entry, currentHistoryNode, {}});

    if (currentHistoryNode >= 0) {
        historyTree[currentHistoryNode].children.push_back(newNode);
    }

    currentHistoryNode = newNode;
    engineMoveWaiting = true;
    engineMoveClock.restart();
    engineMoveAnimating = false;

    rebuildLinearHistoryFromNode(currentHistoryNode);
    {
        const float spacing = 70.f;
        const float visibleHeight = sidePanelHeight - 90.f;

        int maxDepth = -1;

        for (int i = 0; i < static_cast<int>(historyTree.size()); i++) {
            maxDepth = std::max(maxDepth, getHistoryDepth(i));
        }

        const float contentHeight = (maxDepth + 1) * spacing;
        historyScroll = std::max(0.f, contentHeight - visibleHeight);
    }

    engineStalled = false;
    legalMoves.clear();
    clearSelection();

    playSound(capturedPiece != 0 ? captureSoundBuffer : moveSoundBuffer);

    if (game.has_game_ended()) {
        playSound(victorySoundBuffer);
        currentScreen = Screen::EndGame;
    }

    return true;
}

bool ChessUI::engineTurn() const {
    if (game.has_game_ended()) return false;
    if (gameMode == GameMode::EngineVsEngine) return true;
    if (gameMode == GameMode::PlayerVsPlayer) return false;
    if (engineSide == 0) return whiteEngine.is_turn(game);
    return blackEngine.is_turn(game);
}

void ChessUI::updateEngine() {
    if (currentScreen != Screen::Game)
        return;

    if (engineMoveAnimating) {
        if (engineAnimationClock.getElapsedTime().asSeconds() < engineSlideSeconds)
            return;

        const int fromRow = animatedFromRow;
        const int fromCol = animatedFromCol;
        const int toRow = animatedToRow;
        const int toCol = animatedToCol;
        engineMoveAnimating = false;
        animatedPiece = 0;

        if (!applyMove(animatedMove))
            engineStalled = true;
        return;
    }

    if (game.has_game_ended() || engineStalled || historyPaused ||
        gameMode == GameMode::PlayerVsPlayer)
        return;

    if (gameMode == GameMode::PlayerVsEngine) {
        if (!engineTurn())
            return;
    }

    if (!engineMoveWaiting) {
        engineMoveWaiting = true;
        engineMoveClock.restart();
        return;
    }

    if (engineMoveClock.getElapsedTime().asSeconds() < engineMoveDelaySeconds)
        return;

    if (currentHistoryIndex + 1 <
        static_cast<int>(moveHistory.size())) {
        return;
    }

    Engine& engine = (game.get_current_turn() == 0) ? whiteEngine : blackEngine;
    if (!engine.find_best_move(game, engineSearchDepth)) {
        engineStalled = true;
        return;
    }

    animatedMove = engine.get_best_move();
    const int fromRow = animatedMove.old_x;
    const int fromCol = animatedMove.old_y;
    const int toRow = animatedMove.new_x;
    const int toCol = animatedMove.new_y;

    if (slidePieces) {
        animatedFromRow = fromRow;
        animatedFromCol = fromCol;
        animatedToRow = toRow;
        animatedToCol = toCol;
        animatedPiece = (*(game.begin() + fromRow))[fromCol];
        engineMoveAnimating = true;
        engineMoveWaiting = false;
        engineAnimationClock.restart();
        return;
    }

    if (!applyMove(animatedMove)) {
        engineStalled = true;
    }
}

void ChessUI::updateResponsiveView(unsigned int width, unsigned int height) {
    if (width == 0 || height == 0)
        return;

    const float logicalWidth = static_cast<float>(windowWidth);
    const float logicalHeight = static_cast<float>(windowHeight);
    const float targetAspect = logicalWidth / logicalHeight;
    const float actualAspect = static_cast<float>(width) / static_cast<float>(height);

    sf::FloatRect viewport;
    if (actualAspect > targetAspect) {
        const float viewportWidth = targetAspect / actualAspect;
        viewport = sf::FloatRect(
            {(1.f - viewportWidth) / 2.f, 0.f},
            {viewportWidth, 1.f});
    }
    else {
        const float viewportHeight = actualAspect / targetAspect;
        viewport = sf::FloatRect(
            {0.f, (1.f - viewportHeight) / 2.f},
            {1.f, viewportHeight});
    }

    logicalView = sf::View(sf::FloatRect(
        {0.f, 0.f}, {logicalWidth, logicalHeight}));
    logicalView.setViewport(viewport);
    window.setView(logicalView);
}

void ChessUI::toggleFullscreen() {
    fullscreen = !fullscreen;
    if (fullscreen) {
        window.create( sf::VideoMode::getDesktopMode(), "Chess AI", sf::State::Fullscreen);
    }
    else {
        window.create( sf::VideoMode({static_cast<unsigned int>(windowWidth), static_cast<unsigned int>(windowHeight)}), "Chess AI", sf::Style::Default, sf::State::Windowed);
    }
    window.setFramerateLimit(60);
    updateResponsiveView(window.getSize().x, window.getSize().y);
}

void ChessUI::handleEvents() {
    while (const std::optional event = window.pollEvent()) {
        if (const auto* resized = event->getIf<sf::Event::Resized>()) {
            static_cast<void>(resized);
            updateResponsiveView(window.getSize().x, window.getSize().y);
        }

        if (const auto* keyPressed = event->getIf<sf::Event::KeyPressed>()) {
            if (keyPressed->code == sf::Keyboard::Key::F11) {
                toggleFullscreen();
                continue;
            }

            if (keyPressed->code == sf::Keyboard::Key::Escape && fullscreen) {
                toggleFullscreen();
                continue;
            }

            if (currentScreen == Screen::Account) {
                if (accountSignedIn && profilePicturePickerOpen && keyPressed->code == sf::Keyboard::Key::Escape) {
                    profilePicturePickerOpen = false;
                    continue;
                }
                if (keyPressed->code == sf::Keyboard::Key::Backspace) {
                    if (!accountSignedIn) {
                        std::string& input = accountPasswordField
                            ? accountPassword : accountInput;
                        if (!input.empty()) input.pop_back();
                    }
                }
                else if (keyPressed->code == sf::Keyboard::Key::Enter) {
                    if (!accountSignedIn)
                        submitAccountForm();
                }
                continue;
            }

            if (currentScreen == Screen::Promotion) {
                int chosenPiece = 0;
                if (keyPressed->code == sf::Keyboard::Key::Q) chosenPiece = 5;
                else if (keyPressed->code == sf::Keyboard::Key::R) chosenPiece = 4;
                else if (keyPressed->code == sf::Keyboard::Key::B) chosenPiece = 3;
                else if (keyPressed->code == sf::Keyboard::Key::N) chosenPiece = 2;

                if (chosenPiece != 0 && applyMove(promotionFromRow, promotionFromCol, promotionToRow, promotionToCol, chosenPiece) && currentScreen == Screen::Promotion) {
                    currentScreen = Screen::Game;
                }
                continue;
            }
        }

        if (const auto* textEntered = event->getIf<sf::Event::TextEntered>()) {
            if (currentScreen == Screen::Account && !accountSignedIn &&  textEntered->unicode >= 32 && textEntered->unicode <= 126) {
                std::string& input = accountPasswordField ? accountPassword : accountInput;
                if (input.size() < 40)
                    input.push_back(static_cast<char>(textEntered->unicode));
            }
        }

        if (const auto* wheel = event->getIf<sf::Event::MouseWheelScrolled>())
        {
            const sf::Vector2f logicalMouse = window.mapPixelToCoords(wheel->position, logicalView);
            float mouseX = logicalMouse.x;
            float mouseY = logicalMouse.y;

            if (currentScreen == Screen::Game && mouseX >= sidePanelX && mouseX <= sidePanelX + sidePanelWidth && mouseY >= sidePanelY && mouseY <= sidePanelY + sidePanelHeight)
            {
                const float iconSize = 45.f;
                const float verticalSpacing = 70.f;
                const float visibleHeight = sidePanelHeight - 90.f;

                int maxDepth = -1;

                for (int i = 0; i < static_cast<int>(historyTree.size()); i++) {
                    maxDepth = std::max(maxDepth, getHistoryDepth(i));
                }

                const float contentHeight = (maxDepth + 1) * verticalSpacing;
                const float maxVerticalScroll =
                    std::max(0.f, contentHeight - visibleHeight);

                std::vector<sf::Vector2f> positions = getHistoryNodePositions();
                float contentRight = sidePanelX + 25.f;

                for (const auto& pos : positions) {
                    contentRight = std::max(
                        contentRight,
                        pos.x + historyHorizontalScroll + iconSize
                    );
                }

                const float visibleLeft = sidePanelX + 20.f;
                const float visibleRight = sidePanelX + sidePanelWidth - 20.f;
                const float visibleWidth = visibleRight - visibleLeft;
                const float contentWidth =
                    std::max(visibleWidth, contentRight - visibleLeft);

                const float maxHorizontalScroll =
                    std::max(0.f, contentWidth - visibleWidth);

                bool shiftHeld =
                    sf::Keyboard::isKeyPressed(sf::Keyboard::Key::LShift) ||
                    sf::Keyboard::isKeyPressed(sf::Keyboard::Key::RShift);

                bool overHorizontalBar =
                    mouseY >= sidePanelY + sidePanelHeight - 28.f;

                if (shiftHeld || overHorizontalBar) {
                    historyHorizontalScroll -= wheel->delta * 40.f;

                    if (historyHorizontalScroll < 0.f)
                        historyHorizontalScroll = 0.f;

                    if (historyHorizontalScroll > maxHorizontalScroll)
                        historyHorizontalScroll = maxHorizontalScroll;
                }

                else {
                    historyScroll -= wheel->delta * 40.f;

                    if (historyScroll < 0.f)
                        historyScroll = 0.f;

                    if (historyScroll > maxVerticalScroll)
                        historyScroll = maxVerticalScroll;
                }
            }

            if (currentScreen == Screen::MatchHistory) {
                const float contentHeight = static_cast<float>(matchSummaries.size()) * 48.f;
                const float maxScroll = std::max(0.f, contentHeight - 380.f);
                matchHistoryScroll = std::clamp( matchHistoryScroll - wheel->delta * 40.f, 0.f, maxScroll);
            }

            if (currentScreen == Screen::Account && accountSignedIn && profilePicturePickerOpen && mouseX >= 200.f && mouseX <= 1000.f && mouseY >= 230.f && mouseY <= 650.f) {
                const float contentHeight = static_cast<float>((profilePictureOptions.size() + 5) / 6) * 92.f;
                const float maxScroll = std::max(0.f, contentHeight - 400.f);
                profilePicturePickerScroll = std::clamp( profilePicturePickerScroll - wheel->delta * 92.f, 0.f, maxScroll);
            }
        }

        if (event->is<sf::Event::Closed>()) { window.close(); }

        if (const auto* mousePressed = event->getIf<sf::Event::MouseButtonPressed>()) {
            if (mousePressed->button == sf::Mouse::Button::Left) {
                const sf::Vector2f logicalMouse = window.mapPixelToCoords(mousePressed->position, logicalView);
                float mouseX = logicalMouse.x;
                float mouseY = logicalMouse.y;

                const bool profileButtonAvailable = currentScreen != Screen::StartScreen && currentScreen != Screen::MainMenu && currentScreen != Screen::Settings && currentScreen != Screen::Account && currentScreen != Screen::Promotion;
                if (profileButtonAvailable && mouseX >= topProfileX && mouseX <= topProfileX + topProfileRadius * 2.f && mouseY >= 13.f && mouseY <= 13.f + topProfileRadius * 2.f) {
                    playSound(uiClickSoundBuffer);
                    accountReturnScreen = currentScreen;
                    accountMessage.clear();
                    profilePicturePickerOpen = false;
                    if (!accountSignedIn)
                        accountSignUpMode = false;
                    currentScreen = Screen::Account;
                    continue;
                }

                if (currentScreen == Screen::Account) {
                    if (accountSignedIn) {
                        if (profilePicturePickerOpen) {
                            if (mouseX >= 930.f && mouseX <= 975.f &&
                                mouseY >= 135.f && mouseY <= 180.f) {
                                profilePicturePickerOpen = false;
                            } else if (mouseX >= 285.f && mouseX < 933.f &&
                                       mouseY >= 245.f && mouseY < 645.f) {
                                constexpr float cardWidth = 90.f;
                                constexpr float cardHeight = 82.f;
                                constexpr float stepX = 108.f;
                                constexpr float stepY = 92.f;
                                constexpr int columns = 6;
                                const int col = static_cast<int>(
                                    (mouseX - 285.f) / stepX);
                                const int row = static_cast<int>(
                                    (mouseY - 245.f + profilePicturePickerScroll) / stepY);
                                const int index = row * columns + col;
                                const float cardX = 285.f + col * stepX;
                                const float cardY = 245.f + row * stepY
                                    - profilePicturePickerScroll;
                                if (col < columns && mouseX < cardX + cardWidth &&
                                    mouseY >= cardY && mouseY < cardY + cardHeight &&
                                    index >= 0 && index < static_cast<int>(profilePictureOptions.size())) {
                                    profilePicturePath = profilePictureOptions[index];
                                    static_cast<void>(profilePictureTexture.loadFromFile(profilePicturePath));
                                    profilePicturePickerOpen = false;
                                }
                            }
                            else if (mouseX < 200.f || mouseX > 1000.f ||
                                     mouseY < 120.f || mouseY > 710.f) {
                                profilePicturePickerOpen = false;
                            }
                            continue;
                        }

                        if (mouseX >= 556.f && mouseX <= 644.f &&
                            mouseY >= 162.f && mouseY <= 250.f) {
                            playSound(uiClickSoundBuffer);
                            loadProfilePictureOptions();
                            profilePicturePickerScroll = 0.f;
                            profilePicturePickerOpen = true;
                        }
                        else if (mouseX >= 430.f && mouseX <= 770.f &&
                            mouseY >= 430.f && mouseY <= 482.f) {
                            playSound(uiClickSoundBuffer);
                            matchHistoryReturnScreen = Screen::Account;
                            matchHistoryScroll = 0.f;
                            if (matchHistoryProvider) {
                                try {
                                    matchSummaries = matchHistoryProvider();
                                    matchHistoryMessage.clear();
                                }
                                catch (...) {
                                    matchSummaries.clear();
                                }
                            }
                            currentScreen = Screen::MatchHistory;
                        }
                        else if (mouseX >= 430.f && mouseX <= 770.f &&
                                 mouseY >= 495.f && mouseY <= 547.f) {
                            playSound(uiClickSoundBuffer);
                            if (logoutHandler) {
                                try {
                                    logoutHandler();
                                }
                                catch (...) {
                                    accountMessage = "Could not complete sign out.";
                                    continue;
                                }
                            }
                            accountSignedIn = false;
                            accountName.clear();
                            accountInput.clear();
                            accountPassword.clear();
                            accountSignUpMode = false;
                            profilePicturePickerOpen = false;
                            loadDefaultProfilePicture(
                                profilePictureTexture, profilePicturePath);
                            matchSummaries.clear();
                            matchHistoryMessage.clear();
                            matchHistoryScroll = 0.f;
                            currentScreen = accountReturnScreen;
                        }
                        else if (mouseX >= 500.f && mouseX <= 700.f &&
                                 mouseY >= 585.f && mouseY <= 629.f) {
                            profilePicturePickerOpen = false;
                            currentScreen = accountReturnScreen;
                        }
                        continue;
                    }

                    if (mouseX >= 370.f && mouseX <= 830.f &&
                        mouseY >= 250.f && mouseY <= 304.f) {
                        accountPasswordField = false;
                    }
                    else if (mouseX >= 370.f && mouseX <= 830.f &&
                             mouseY >= 349.f && mouseY <= 403.f) {
                        accountPasswordField = true;
                    }
                    else if (mouseX >= 370.f && mouseX <= 830.f &&
                             mouseY >= 425.f && mouseY <= 473.f) {
                        accountSignUpMode = !accountSignUpMode;
                        accountMessage.clear();
                    }
                    else if (mouseX >= 430.f && mouseX <= 770.f &&
                             mouseY >= 530.f && mouseY <= 586.f) {
                        submitAccountForm();
                    }
                    else if (mouseX >= 500.f && mouseX <= 700.f &&
                             mouseY >= 620.f && mouseY <= 670.f) {
                        currentScreen = accountReturnScreen;
                    }
                    continue;
                }

                if (currentScreen == Screen::MatchHistory) {
                    if (mouseX >= 500.f && mouseX <= 700.f &&
                        mouseY >= 665.f && mouseY <= 720.f) {
                        currentScreen = matchHistoryReturnScreen;
                    }
                    continue;
                }

                if (currentScreen == Screen::StartScreen) {
                    if (mouseX >= 440.f && mouseX <= 760.f && mouseY >= 250.f && mouseY <= 325.f) {
                        playSound(uiClickSoundBuffer);
                        currentScreen = Screen::MainMenu;
                        selectionFrame = 1;
                        selectionFrameClock.restart();
                    }

                    continue;
                }

                if (currentScreen == Screen::MainMenu) {
                    if (mouseX >= 440.f && mouseX <= 760.f &&
                        mouseY >= 220.f && mouseY <= 280.f) {
                        playSound(uiClickSoundBuffer);
                        startGame(-1, GameMode::PlayerVsPlayer);
                    }

                    else if (mouseX >= 440.f && mouseX <= 760.f &&
                             mouseY >= 288.f && mouseY <= 348.f) {
                        playSound(uiClickSoundBuffer);
                        currentScreen = Screen::ChooseSide;
                    }

                    else if (mouseX >= 440.f && mouseX <= 760.f &&
                             mouseY >= 356.f && mouseY <= 416.f) {
                        playSound(uiClickSoundBuffer);
                        startGame(-1, GameMode::EngineVsEngine);
                    }

                    else if (mouseX >= 440.f && mouseX <= 760.f &&
                             mouseY >= 424.f && mouseY <= 484.f) {
                        playSound(uiClickSoundBuffer);
                        previousScreen = Screen::MainMenu;
                        settingsTab = 0;
                        currentScreen = Screen::Settings;
                    }

                    continue;
                }

                if (currentScreen == Screen::ChooseSide) {
                    if (mouseX >= 440.f && mouseX <= 760.f && mouseY >= 300.f && mouseY <= 370.f) {
                        playSound(uiClickSoundBuffer);
                        startGame(1, GameMode::PlayerVsEngine);
                    }

                    else if (mouseX >= 440.f && mouseX <= 760.f && mouseY >= 400.f && mouseY <= 470.f) {
                        playSound(uiClickSoundBuffer);
                        startGame(0, GameMode::PlayerVsEngine);
                    }

                    continue;
                }

                if (currentScreen == Screen::Promotion) {
                    int chosenPiece = 0;
                    constexpr float optionX = 402.f;
                    constexpr float optionY = 358.f;
                    constexpr float optionSize = 84.f;
                    constexpr float optionStep = 104.f;
                    const int pieces[4] = {5, 4, 3, 2};

                    for (int i = 0; i < 4; ++i) {
                        const float x = optionX + i * optionStep;
                        if (mouseX >= x && mouseX < x + optionSize &&
                            mouseY >= optionY && mouseY < optionY + optionSize) {
                            chosenPiece = pieces[i];
                            break;
                        }
                    }

                    if (chosenPiece != 0) {
                        playSound(uiClickSoundBuffer);

                        if (applyMove(
                                promotionFromRow,
                                promotionFromCol,
                                promotionToRow,
                                promotionToCol,
                                chosenPiece)) {
                            if (currentScreen == Screen::Promotion)
                                currentScreen = Screen::Game;
                        }
                    }

                    continue;
                }

                if (currentScreen == Screen::Settings) {
                    if (mouseX >= 28.f && mouseX <= 64.f &&
                        mouseY >= 30.f && mouseY <= 78.f) {
                        playSound(uiClickSoundBuffer);
                        currentScreen = previousScreen;
                    }

                    else if (mouseX >= 812.f && mouseX <= 952.f &&
                        mouseY >= 42.f && mouseY <= 80.f) {
                        playSound(uiClickSoundBuffer);
                        settingsTab = 0;
                    }

                    else if (mouseX >= 962.f && mouseX <= 1102.f &&
                             mouseY >= 42.f && mouseY <= 80.f) {
                        playSound(uiClickSoundBuffer);
                        settingsTab = 1;
                        refreshPieceThemes();
                        refreshBoardThemes();
                    }

                    else if (settingsTab == 0) {
                        auto clickedGameplayRow = [&](float y) {
                            return mouseX >= 290.f && mouseX <= 910.f &&
                                   mouseY >= y && mouseY <= y + 48.f;
                        };

                        if (clickedGameplayRow(220.f)) {
                            playSound(uiClickSoundBuffer);
                            dragPieces = !dragPieces;
                            clearSelection();
                        }

                        else if (clickedGameplayRow(274.f)) {
                            playSound(uiClickSoundBuffer);
                            slidePieces = !slidePieces;
                        }

                        else if (clickedGameplayRow(328.f)) {
                            playSound(uiClickSoundBuffer);
                            showPossibleMoves = !showPossibleMoves;

                            if (!showPossibleMoves)
                                legalMoves.clear();
                            else if (pieceSelected)
                                selectPiece(selectedRow, selectedCol);
                        }

                        else if (clickedGameplayRow(382.f)) {
                            playSound(uiClickSoundBuffer);
                            showCoordinates = !showCoordinates;
                        }

                        else if (clickedGameplayRow(436.f)) {
                            playSound(uiClickSoundBuffer);
                            soundEffects = !soundEffects;
                        }

                        else if (clickedGameplayRow(490.f)) {
                            playSound(uiClickSoundBuffer);
                            historyStyle = historyStyle == HistoryStyle::Pictogram
                                ? HistoryStyle::Algebraic
                                : HistoryStyle::Pictogram;
                        }

                        else if (clickedGameplayRow(544.f)) {
                            playSound(uiClickSoundBuffer);
                            toggleFullscreen();
                            continue;
                        }

                        else if (clickedGameplayRow(598.f)) {
                            playSound(uiClickSoundBuffer);
                            gLightMode = !gLightMode;
                        }

                    }

                    else {
                        for (int i = 0; i < static_cast<int>(pieceThemeFolders.size()); i++) {
                            const float x = 290.f + i * 157.f;
                            const float y = 235.f;

                            if (mouseX >= x && mouseX <= x + 148.f &&
                                mouseY >= y && mouseY <= y + 290.f) {
                                playSound(uiClickSoundBuffer);
                                pieceTheme = i;
                                boardTheme = i;
                                loadPieceTextures();
                                loadBoardTextures();
                                break;
                            }
                        }
                    }

                    if (gameStarted &&
                        mouseX >= 350.f && mouseX <= 500.f &&
                        mouseY >= 662.f && mouseY <= 704.f) {
                        playSound(uiClickSoundBuffer);
                        startGame(engineSide, gameMode);
                        continue;
                    }

                    if (mouseX >= 525.f && mouseX <= 675.f &&
                        mouseY >= 662.f && mouseY <= 704.f) {
                        playSound(uiClickSoundBuffer);
                        currentScreen = Screen::MainMenu;
                        hoveredHistoryNode = -1;
                        continue;
                    }

                    if (mouseX >= 700.f && mouseX <= 850.f &&
                        mouseY >= 662.f && mouseY <= 704.f) {
                        playSound(uiClickSoundBuffer);
                        currentScreen = previousScreen;
                        continue;
                    }

                    continue;
                }

                if (currentScreen == Screen::EndGame) {
                    if (mouseX >= 390.f && mouseX <= 590.f &&
                        mouseY >= 500.f && mouseY <= 565.f) {
                        playSound(uiClickSoundBuffer);
                        startGame(engineSide, gameMode);
                        continue;
                    }

                    if (mouseX >= 610.f && mouseX <= 810.f &&
                        mouseY >= 500.f && mouseY <= 565.f) {
                        playSound(uiClickSoundBuffer);
                        previousScreen = Screen::EndGame;
                        currentScreen = Screen::Settings;
                        settingsTab = 0;
                        continue;
                    }

                    if (mouseX >= 500.f && mouseX <= 700.f &&
                        mouseY >= 590.f && mouseY <= 650.f) {
                        playSound(uiClickSoundBuffer);
                        currentScreen = Screen::MainMenu;
                        continue;
                    }

                    continue;
                }

                if (currentScreen != Screen::Game || engineMoveAnimating)
                    continue;

                if (mouseX >= topFlipX && mouseX <= topFlipX + 74.f &&
                    mouseY >= 20.f && mouseY <= 56.f) {
                    playSound(uiClickSoundBuffer);
                    manualBoardFlip = !manualBoardFlip;
                    clearSelection();
                    continue;
                }

                if (historyPaused &&
                    mouseX >= topPlayX && mouseX <= topPlayX + 70.f &&
                    mouseY >= 20.f && mouseY <= 56.f) {
                    playSound(uiClickSoundBuffer);
                    historyPaused = false;
                    engineStalled = false;
                    engineMoveWaiting = true;
                    engineMoveClock.restart();
                    clearSelection();
                    continue;
                }

                if (mouseX >= topSettingsX && mouseX <= topSettingsX + 160.f &&
                    mouseY >= 20.f && mouseY <= 56.f) {
                    playSound(uiClickSoundBuffer);
                    previousScreen = Screen::Game;
                    settingsTab = 0;
                    currentScreen = Screen::Settings;
                    clearSelection();
                    continue;
                }

                const float iconSize = 45.f;
                std::vector<sf::Vector2f> historyPositions = getHistoryNodePositions();

                bool clickedHistory = false;

                for (int i = 0; i < static_cast<int>(historyTree.size()); i++)
                {
                    float nodeX = historyPositions[i].x;
                    float nodeY = historyPositions[i].y - historyScroll;

                    if (mouseX >= nodeX && mouseX <= nodeX + iconSize &&
                        mouseY >= nodeY && mouseY <= nodeY + iconSize)
                    {
                        playSound(uiClickSoundBuffer);
                        currentHistoryNode = i;
                        game = historyTree[i].move.boardAfterMove;
                        rebuildLinearHistoryFromNode(currentHistoryNode);

                        engineStalled = false;
                        historyPaused = true;
                        clearSelection();

                        clickedHistory = true;
                        break;
                    }
                }

                if (clickedHistory ||
                    historyPaused ||
                    gameMode == GameMode::EngineVsEngine ||
                    engineTurn() ||
                    game.has_game_ended()) continue;

                if (mouseX < boardX || mouseX >= boardX + boardSize ||
                    mouseY < boardY || mouseY >= boardY + boardSize) continue;

                int shownCol = static_cast<int>((mouseX - boardX) / squareSize);
                int shownRow = static_cast<int>((mouseY - boardY) / squareSize);

                int col = isBoardFlipped() ? 7 - shownCol : shownCol;
                int row = isBoardFlipped() ? 7 - shownRow : shownRow;

                if (row < 0 || row >= 8 || col < 0 || col >= 8)
                    continue;

                if (!dragPieces) {
                    if (!pieceSelected) {
                        auto boardRow = game.begin() + row;
                        int piece = (*boardRow)[col];

                        if (piece != 0 && (piece < 0) == (game.get_current_turn() == 1)) {
                            selectPiece(row, col);
                        }
                    }

                    else {
                        if (row == selectedRow && col == selectedCol) {
                            clearSelection();
                            continue;
                        }

                        auto boardRow = game.begin() + row;
                        int piece = (*boardRow)[col];

                        if (piece != 0 && (piece < 0) == (game.get_current_turn() == 1)) {
                            selectPiece(row, col);
                            continue;
                        }

                        if (game.valid_move(selectedRow, selectedCol, row, col)) {
                            if (slidePieces) {
                                animatedFromRow = selectedRow;
                                animatedFromCol = selectedCol;
                                animatedToRow = row;
                                animatedToCol = col;
                                animatedPiece =
                                    (*(game.begin() + selectedRow))[selectedCol];
                                engineMoveAnimating = true;
                                engineMoveWaiting = false;
                                engineAnimationClock.restart();
                                clearSelection();
                            }
                            else if (applyMove(selectedRow, selectedCol, row, col)) {
                                clearSelection();
                            }
                        }
                    }

                    continue;
                }

                auto boardRow = game.begin() + row;
                int piece = (*boardRow)[col];

                if (piece != 0 && (piece < 0) == (game.get_current_turn() == 1)) {
                    dragging = true;
                    draggedRow = row;
                    draggedCol = col;
                    mousePosition = {mouseX, mouseY};
                    legalMoves.clear();

                    if (showPossibleMoves) {
                        for (int newRow = 0; newRow < 8; newRow++) {
                            for (int newCol = 0; newCol < 8; newCol++) {
                                if (game.valid_move(draggedRow, draggedCol, newRow, newCol)) {
                                    legalMoves.push_back({newRow, newCol});
                                }
                            }
                        }
                    }
                }
            }
        }

        if (const auto* mouseMoved = event->getIf<sf::Event::MouseMoved>()) {
            mousePosition = window.mapPixelToCoords(mouseMoved->position, logicalView);
        }

        if (const auto* mouseReleased = event->getIf<sf::Event::MouseButtonReleased>())
        if (mouseReleased->button == sf::Mouse::Button::Left && dragging) {
            const sf::Vector2f logicalMouse =
                window.mapPixelToCoords(mouseReleased->position, logicalView);
            float mouseX = logicalMouse.x;
            float mouseY = logicalMouse.y;
            int shownCol = static_cast<int>((mouseX - boardX) / squareSize);
            int shownRow = static_cast<int>((mouseY - boardY) / squareSize);

            int newCol = isBoardFlipped() ? 7 - shownCol : shownCol;
            int newRow = isBoardFlipped() ? 7 - shownRow : shownRow;

            if (gameMode != GameMode::EngineVsEngine &&
                !engineTurn() && !game.has_game_ended() &&
                mouseX >= boardX && mouseX < boardX + boardSize &&
                mouseY >= boardY && mouseY < boardY + boardSize) {
                applyMove(draggedRow, draggedCol, newRow, newCol);
            }

            dragging = false;
            draggedRow = -1;
            draggedCol = -1;
            legalMoves.clear();
        }
    }
}

void ChessUI::draw() {
    updateResponsiveView(window.getSize().x, window.getSize().y);
    window.clear(uiColor(30, 30, 30));

    if (currentScreen == Screen::StartScreen) {
        drawStartScreen();
        window.display();
        return;
    }

    if (currentScreen == Screen::MainMenu) {
        drawMainMenu();
        window.display();
        return;
    }

    if (currentScreen == Screen::ChooseSide) {
        drawChooseSide();
        drawProfileButton();
        window.display();
        return;
    }

    if (currentScreen == Screen::Settings) {
        drawSettings();
        window.display();
        return;
    }

    if (currentScreen == Screen::Account) {
        drawAccountScreen();
        window.display();
        return;
    }

    if (currentScreen == Screen::MatchHistory) {
        drawMatchHistoryScreen();
        drawProfileButton();
        window.display();
        return;
    }

    if (currentScreen == Screen::EndGame) {
        drawBoard();
        drawLastMoveHighlights();

        if (showCoordinates)
            drawCoordinates();

        drawPieces();
        drawCapturedPieces();
        drawSidePanel();
        drawText();
        drawClocks();
        drawMoveHistory();
        drawEndScreen();
        drawProfileButton();
        window.display();
        return;
    }

    drawBoard();
    drawLastMoveHighlights();

    if (showCoordinates)
        drawCoordinates();

    drawLegalMoves();
    drawPieces();

    if (!dragPieces && pieceSelected) {
        sf::RectangleShape selectedSquare( sf::Vector2f(squareSize - 6.f, squareSize - 6.f) );
        selectedSquare.setPosition({
            static_cast<float>(boardX + displayCol(selectedCol) * squareSize + 3),
            static_cast<float>(boardY + displayRow(selectedRow) * squareSize + 3)
        });
        selectedSquare.setFillColor(sf::Color::Transparent);
        selectedSquare.setOutlineColor(uiColor(255, 215, 0));
        selectedSquare.setOutlineThickness(4.f);
        window.draw(selectedSquare);
    }

    drawHistoryPreview();
    drawCapturedPieces();

    drawSidePanel();
    drawText();
    drawClocks();
    drawMoveHistory();

    sf::RectangleShape flipButton({74.f, 36.f});
    flipButton.setPosition({topFlipX, 20.f});
    flipButton.setFillColor(uiColor(60, 60, 60));
    window.draw(flipButton);

    sf::Text flipText(font, "FLIP", 13);
    flipText.setPosition({topFlipX + 22.f, 29.f});
    flipText.setFillColor(textWhite());
    window.draw(flipText);

    if (historyPaused) {
        sf::RectangleShape playButton({70.f, 36.f});
        playButton.setPosition({topPlayX, 20.f});
        playButton.setFillColor(uiColor(70, 120, 75));
        window.draw(playButton);

        sf::Text playText(font, "PLAY", 13);
        playText.setPosition({topPlayX + 20.f, 29.f});
        playText.setFillColor(textWhite());
        window.draw(playText);
    }

    sf::RectangleShape settingsButton({160.f, 36.f});
    settingsButton.setPosition({topSettingsX, 20.f});
    settingsButton.setFillColor(uiColor(60, 60, 60));
    window.draw(settingsButton);

    sf::Text settingsText(font, "SETTINGS", 15);
    settingsText.setPosition({topSettingsX + 40.f, 28.f});
    settingsText.setFillColor(textWhite());
    window.draw(settingsText);

    if (currentScreen == Screen::Promotion)
        drawPromotionDialog();
    else
        drawProfileButton();

    window.display();
}

void ChessUI::drawPromotionDialog() {
    sf::RectangleShape overlay({static_cast<float>(windowWidth), static_cast<float>(windowHeight)});
    overlay.setFillColor(uiColor(0, 0, 0, 175));
    window.draw(overlay);

    sf::RectangleShape dialog({520.f, 180.f});
    dialog.setPosition({340.f, 310.f});
    dialog.setFillColor(uiColor(38, 38, 38));
    dialog.setOutlineColor(uiColor(220, 220, 220));
    dialog.setOutlineThickness(2.f);
    window.draw(dialog);

    const int pieces[4] = {5, 4, 3, 2};
    int side = 1;
    if (promotionFromRow >= 0 && promotionFromRow < 8 &&
        promotionFromCol >= 0 && promotionFromCol < 8) {
        const int pawn = (*(game.begin() + promotionFromRow))[promotionFromCol];
        side = pawn < 0 ? -1 : 1;
    }

    for (int i = 0; i < 4; ++i) {
        const float x = 402.f + static_cast<float>(i) * 104.f;
        const float y = 358.f;

        sf::RectangleShape option({84.f, 84.f});
        option.setPosition({x, y});
        const bool hovered = mousePosition.x >= x && mousePosition.x <= x + 84.f &&
            mousePosition.y >= y && mousePosition.y <= y + 84.f;
        option.setFillColor(hovered
            ? uiColor(105, 91, 55) : uiColor(70, 70, 70));
        option.setOutlineColor(uiColor(210, 190, 120));
        option.setOutlineThickness(2.f);
        window.draw(option);

        sf::Texture& texture = pieceTextures[side * pieces[i]];
        sf::Sprite piece(texture);
        const sf::Vector2u size = texture.getSize();
        const float maxSize = static_cast<float>(std::max(size.x, size.y));
        const float scale = 58.f / maxSize;
        piece.setScale({scale, scale});
        piece.setPosition({x + (84.f - size.x * scale) / 2.f,
            y + (84.f - size.y * scale) / 2.f});
        window.draw(piece);
    }
}

void ChessUI::drawCoverTexture(const sf::Texture& texture) {
    sf::Sprite background(texture);

    auto textureSize = texture.getSize();
    float scaleX = static_cast<float>(windowWidth) / textureSize.x;
    float scaleY = static_cast<float>(windowHeight) / textureSize.y;
    float scale = std::max(scaleX, scaleY);

    background.setScale({scale, scale});

    float width = textureSize.x * scale;
    float height = textureSize.y * scale;

    background.setPosition({
        (windowWidth - width) / 2.f,
        (windowHeight - height) / 2.f
    });

    window.draw(background);
}

void ChessUI::drawStartScreen() {
    drawCoverTexture(startBackgroundTexture);

    sf::RectangleShape darkOverlay({1200.f, 800.f});
    darkOverlay.setFillColor(uiColor(0, 0, 0, 55));
    window.draw(darkOverlay);

    sf::Text title(font, "CHESS", 64);
    title.setPosition({500.f, 105.f});
    title.setFillColor(textWhite());
    window.draw(title);

    sf::RectangleShape startButton({320.f, 75.f});
    startButton.setPosition({440.f, 250.f});
    startButton.setFillColor(uiColor(20, 20, 20, 205));
    startButton.setOutlineColor(uiColor(220, 220, 220));
    startButton.setOutlineThickness(2.f);
    window.draw(startButton);

    sf::Text startText(font, "START", 28);
    startText.setPosition({550.f, 270.f});
    startText.setFillColor(textWhite());
    window.draw(startText);
}

void ChessUI::updateSelectionVideo() {
    if (selectionFrameClock.getElapsedTime().asSeconds() < 0.125f)
        return;

    selectionFrameClock.restart();

    selectionFrame++;

    if (selectionFrame > selectionFrameCount)
        selectionFrame = 1;

    std::ostringstream filename;

    filename << "assets/design/selection_frames/frame"
             << std::setfill('0') << std::setw(3)
             << selectionFrame << ".jpg";

    static_cast<void>(selectionFrameTexture.loadFromFile(filename.str()));
}

void ChessUI::drawProfileButton() {
    sf::CircleShape background(topProfileRadius);
    background.setPosition({topProfileX, 13.f});
    background.setTexture(&profilePictureTexture);
    background.setOutlineThickness(0.f);
    window.draw(background);

    if (accountSignedIn) {
        sf::CircleShape signedInDot(4.f);
        signedInDot.setPosition({topProfileX + 41.f, 52.f});
        signedInDot.setFillColor(uiColor(90, 190, 105));
        signedInDot.setOutlineColor(uiColor(18, 20, 22));
        signedInDot.setOutlineThickness(1.f);
        window.draw(signedInDot);
    }
}

void ChessUI::drawMainMenu() {
    updateSelectionVideo();

    drawCoverTexture(selectionFrameTexture);

    sf::RectangleShape darkOverlay({1200.f, 800.f});
    darkOverlay.setFillColor(uiColor(0, 0, 0, 95));
    window.draw(darkOverlay);

    sf::Text title(font, "MAIN MENU", 42);
    title.setPosition({495.f, 105.f});
    title.setFillColor(textWhite());
    window.draw(title);

    const float buttonWidth = 300.f;
    const float buttonHeight = 56.f;
    const float buttonX = 450.f;
    const char* labels[] = {
        "PLAYER VS PLAYER",
        "PLAYER VS ENGINE",
        "ENGINE VS ENGINE",
        "SETTINGS"
    };
    const float firstY = 220.f;
    const float stepY = 68.f;

    for (int i = 0; i < 4; ++i) {
        const float y = firstY + stepY * i;
        sf::RectangleShape button({buttonWidth, buttonHeight});
        button.setPosition({buttonX, y});
        button.setFillColor(uiColor(20, 20, 20, 210));
        button.setOutlineColor(uiColor(210, 210, 210));
        button.setOutlineThickness(1.f);
        window.draw(button);

        sf::Text label(font, labels[i], 19);
        label.setPosition({buttonX + 30.f, y + 17.f});
        label.setFillColor(textWhite());
        window.draw(label);
    }
}

void ChessUI::drawChooseSide() {
    sf::Text title(font, "CHOOSE YOUR SIDE", 42);
    title.setPosition({390.f, 140.f});
    title.setFillColor(textWhite());
    window.draw(title);

    const float buttonWidth = 320.f;
    const float buttonHeight = 70.f;
    const float buttonX = 440.f;

    sf::RectangleShape whiteButton({buttonWidth, buttonHeight});
    whiteButton.setPosition({buttonX, 300.f});
    whiteButton.setFillColor(uiColor(60, 60, 60));
    window.draw(whiteButton);

    sf::Text whiteText(font, "PLAY AS WHITE", 24);
    whiteText.setPosition({490.f, 320.f});
    whiteText.setFillColor(textWhite());
    window.draw(whiteText);

    sf::RectangleShape blackButton({buttonWidth, buttonHeight});
    blackButton.setPosition({buttonX, 400.f});
    blackButton.setFillColor(uiColor(60, 60, 60));
    window.draw(blackButton);

    sf::Text blackText(font, "PLAY AS BLACK", 24);
    blackText.setPosition({490.f, 420.f});
    blackText.setFillColor(textWhite());
    window.draw(blackText);
}

void ChessUI::drawAccountScreen() {
    if (accountSignedIn) {
        sf::RectangleShape shade({1200.f, 800.f});
        shade.setFillColor(uiColor(10, 11, 13, 235));
        window.draw(shade);

        sf::RectangleShape panel({600.f, 640.f});
        panel.setPosition({300.f, 80.f});
        panel.setFillColor(uiColor(28, 30, 33));
        panel.setOutlineColor(uiColor(70, 73, 78));
        panel.setOutlineThickness(1.f);
        window.draw(panel);

        sf::Text title(font, "PROFILE", 34);
        title.setPosition({510.f, 108.f});
        title.setFillColor(textWhite());
        window.draw(title);

        sf::CircleShape avatar(44.f);
        avatar.setPosition({556.f, 162.f});
        avatar.setTexture(&profilePictureTexture);
        avatar.setOutlineColor(uiColor(110, 114, 121));
        avatar.setOutlineThickness(1.f);
        window.draw(avatar);

        sf::Text signedInAs(font, "SIGNED IN AS", 14);
        signedInAs.setPosition({370.f, 268.f});
        signedInAs.setFillColor(uiColor(160, 163, 169));
        window.draw(signedInAs);

        sf::Text profileName(font, accountName, 22);
        profileName.setPosition({370.f, 287.f});
        profileName.setFillColor(textWhite());
        window.draw(profileName);

        if (!accountMessage.empty()) {
            sf::Text statusText(font, accountMessage, 13);
            statusText.setPosition({370.f, 393.f});
            statusText.setFillColor(uiColor(245, 170, 135));
            window.draw(statusText);
        }

        sf::RectangleShape historyButton({340.f, 52.f});
        historyButton.setPosition({430.f, 430.f});
        historyButton.setFillColor(uiColor(45, 48, 52));
        historyButton.setOutlineColor(uiColor(74, 78, 84));
        historyButton.setOutlineThickness(1.f);
        window.draw(historyButton);

        sf::Text historyText(font, "MATCH HISTORY", 17);
        historyText.setPosition({531.f, 446.f});
        historyText.setFillColor(textWhite());
        window.draw(historyText);

        sf::RectangleShape signOutButton({340.f, 52.f});
        signOutButton.setPosition({430.f, 495.f});
        signOutButton.setFillColor(uiColor(94, 52, 49));
        signOutButton.setOutlineColor(uiColor(128, 73, 68));
        signOutButton.setOutlineThickness(1.f);
        window.draw(signOutButton);

        sf::Text signOutText(font, "SIGN OUT", 17);
        signOutText.setPosition({558.f, 511.f});
        signOutText.setFillColor(textWhite());
        window.draw(signOutText);

        sf::RectangleShape backButton({170.f, 44.f});
        backButton.setPosition({515.f, 585.f});
        backButton.setFillColor(uiColor(39, 41, 45));
        backButton.setOutlineColor(uiColor(65, 68, 73));
        backButton.setOutlineThickness(1.f);
        window.draw(backButton);

        sf::Text backText(font, "BACK", 16);
        backText.setPosition({574.f, 598.f});
        backText.setFillColor(textWhite());
        window.draw(backText);
        if (profilePicturePickerOpen)
            drawProfilePicturePicker();
        return;
    }

    sf::RectangleShape shade({1200.f, 800.f});
    shade.setFillColor(uiColor(20, 20, 20, 210));
    window.draw(shade);

    sf::RectangleShape panel({600.f, 580.f});
    panel.setPosition({300.f, 120.f});
    panel.setFillColor(uiColor(42, 42, 42));
    panel.setOutlineColor(uiColor(190, 190, 190));
    panel.setOutlineThickness(2.f);
    window.draw(panel);

    sf::Text title(font, accountSignUpMode ? "CREATE ACCOUNT" : "SIGN IN", 34);
    title.setPosition({420.f, 153.f});
    title.setFillColor(textWhite());
    window.draw(title);

    sf::Text usernameLabel(font, "Username", 18);
    usernameLabel.setPosition({370.f, 225.f});
    usernameLabel.setFillColor(uiColor(225, 225, 225));
    window.draw(usernameLabel);

    sf::RectangleShape usernameBox({460.f, 54.f});
    usernameBox.setPosition({370.f, 250.f});
    usernameBox.setFillColor(uiColor(27, 27, 27));
    usernameBox.setOutlineColor(accountPasswordField
        ? uiColor(95, 95, 95) : uiColor(230, 190, 90));
    usernameBox.setOutlineThickness(2.f);
    window.draw(usernameBox);

    sf::Text username(font,
        accountInput.empty() ? "Enter username" : accountInput, 18);
    username.setPosition({388.f, 266.f});
    username.setFillColor(accountInput.empty()
        ? uiColor(145, 145, 145) : textWhite());
    window.draw(username);

    sf::Text passwordLabel(font, "Password", 18);
    passwordLabel.setPosition({370.f, 324.f});
    passwordLabel.setFillColor(uiColor(225, 225, 225));
    window.draw(passwordLabel);

    sf::RectangleShape passwordBox({460.f, 54.f});
    passwordBox.setPosition({370.f, 349.f});
    passwordBox.setFillColor(uiColor(27, 27, 27));
    passwordBox.setOutlineColor(accountPasswordField
        ? uiColor(230, 190, 90) : uiColor(95, 95, 95));
    passwordBox.setOutlineThickness(2.f);
    window.draw(passwordBox);

    const std::string maskedPassword = accountPassword.empty()
        ? "Enter password" : std::string(accountPassword.size(), '*');
    sf::Text password(font, maskedPassword, 18);
    password.setPosition({388.f, 365.f});
    password.setFillColor(accountPassword.empty()
        ? uiColor(145, 145, 145) : textWhite());
    window.draw(password);

    sf::RectangleShape modeButton({460.f, 48.f});
    modeButton.setPosition({370.f, 425.f});
    modeButton.setFillColor(uiColor(58, 58, 58));
    window.draw(modeButton);

    sf::Text modeText(font, accountSignUpMode
        ? "Already registered? Sign in" : "New here? Create an account", 17);
    modeText.setPosition({435.f, 439.f});
    modeText.setFillColor(textWhite());
    window.draw(modeText);

    const std::string status = !accountMessage.empty()
        ? accountMessage
        : (authHandler ? "" : "Default account: jarvis / jarvis1234");
    sf::Text statusText(font, status, 14);
    statusText.setPosition({370.f, 487.f});
    statusText.setFillColor(accountMessage.empty()
        ? uiColor(190, 190, 190) : uiColor(245, 190, 120));
    window.draw(statusText);

    sf::RectangleShape submitButton({340.f, 56.f});
    submitButton.setPosition({430.f, 530.f});
    submitButton.setFillColor(uiColor(70, 125, 75));
    window.draw(submitButton);

    sf::Text submitText(font, accountSignUpMode ? "SIGN UP" : "LOG IN", 20);
    submitText.setPosition({552.f, 547.f});
    submitText.setFillColor(textWhite());
    window.draw(submitText);

    sf::RectangleShape backButton({200.f, 50.f});
    backButton.setPosition({500.f, 620.f});
    backButton.setFillColor(uiColor(62, 62, 62));
    window.draw(backButton);

    sf::Text backText(font, "BACK", 18);
    backText.setPosition({570.f, 634.f});
    backText.setFillColor(textWhite());
    window.draw(backText);
}

void ChessUI::drawProfilePicturePicker() {
    sf::RectangleShape shade({1200.f, 800.f});
    shade.setFillColor(uiColor(0, 0, 0, 185));
    window.draw(shade);

    sf::RectangleShape panel({800.f, 590.f});
    panel.setPosition({200.f, 120.f});
    panel.setFillColor(uiColor(28, 30, 33));
    panel.setOutlineColor(uiColor(90, 94, 100));
    panel.setOutlineThickness(2.f);
    window.draw(panel);

    sf::Text title(font, "CHOOSE A PROFILE PICTURE", 27);
    title.setPosition({390.f, 145.f});
    title.setFillColor(textWhite());
    window.draw(title);

    sf::RectangleShape closeButton({42.f, 38.f});
    closeButton.setPosition({930.f, 133.f});
    closeButton.setFillColor(uiColor(67, 48, 48));
    window.draw(closeButton);
    sf::Text closeText(font, "X", 18);
    closeText.setPosition({944.f, 141.f});
    closeText.setFillColor(textWhite());
    window.draw(closeText);

    constexpr float cardWidth = 90.f;
    constexpr float cardHeight = 82.f;
    constexpr float cardGapX = 18.f;
    constexpr float cardStartX = 285.f;
    constexpr float cardStartY = 245.f;
    constexpr int cardColumns = 6;

    for (std::size_t i = 0; i < profilePictureOptions.size(); ++i) {
        const int row = static_cast<int>(i) / cardColumns;
        const int col = static_cast<int>(i) % cardColumns;
        const float x = cardStartX + col * (cardWidth + cardGapX);
        const float y = cardStartY + row * 92.f - profilePicturePickerScroll;
        if (y < cardStartY || y + cardHeight > 645.f)
            continue;

        const bool selected = profilePictureOptions[i] == profilePicturePath;
        sf::RectangleShape choice({cardWidth, cardHeight});
        choice.setPosition({x, y});
        choice.setFillColor(selected
            ? uiColor(82, 115, 82) : uiColor(58, 58, 58));
        if (selected) {
            choice.setOutlineColor(sf::Color::White);
            choice.setOutlineThickness(2.f);
        }
        window.draw(choice);

        sf::CircleShape picture(30.f);
        picture.setPosition({x + (cardWidth - 60.f) / 2.f,
            y + (cardHeight - 60.f) / 2.f});
        picture.setFillColor(sf::Color::White);
        picture.setTexture(&profilePictureOptionTextures[i]);
        window.draw(picture);
    }

    const float contentHeight =
        static_cast<float>((profilePictureOptions.size() + 5) / 6) * 92.f;
    if (contentHeight > 400.f) {
        sf::Text scrollHint(font, "Scroll to see more pictures", 13);
        scrollHint.setPosition({488.f, 672.f});
        scrollHint.setFillColor(uiColor(165, 168, 173));
        window.draw(scrollHint);
    }
}

void ChessUI::drawMatchHistoryScreen() {
    sf::RectangleShape shade({1200.f, 800.f});
    shade.setFillColor(uiColor(24, 24, 24));
    window.draw(shade);

    sf::Text title(font, "MATCH HISTORY", 36);
    title.setPosition({440.f, 96.f});
    title.setFillColor(textWhite());
    window.draw(title);

    sf::Text count(font,
        "Matches: " + std::to_string(matchSummaries.size()), 17);
    count.setPosition({285.f, 165.f});
    count.setFillColor(uiColor(210, 210, 210));
    window.draw(count);

    auto shortText = [](const std::string& value, std::size_t maxLength) {
        if (value.size() <= maxLength) return value;
        return value.substr(0, maxLength > 3 ? maxLength - 3 : 0) + "...";
    };

    if (matchSummaries.empty()) {
        const std::string emptyMessage = !matchHistoryMessage.empty()
            ? matchHistoryMessage
            : (matchHistoryProvider
                ? "No matches have been recorded yet."
                : "Match history is not connected yet.");
        sf::Text empty(font, emptyMessage, 22);
        empty.setPosition({365.f, 340.f});
        empty.setFillColor(uiColor(200, 200, 200));
        window.draw(empty);
    }
    else {
        const float tableX = 275.f;
        const float tableY = 205.f;
        sf::RectangleShape header({650.f, 42.f});
        header.setPosition({tableX, tableY});
        header.setFillColor(uiColor(67, 67, 67));
        window.draw(header);

        const char* headings[] = {"DATE", "WHITE", "BLACK", "RESULT"};
        const float columns[] = {292.f, 415.f, 565.f, 715.f};
        for (int i = 0; i < 4; ++i) {
            sf::Text heading(font, headings[i], 15);
            heading.setPosition({columns[i], tableY + 13.f});
            heading.setFillColor(uiColor(230, 230, 230));
            window.draw(heading);
        }

        for (std::size_t i = 0; i < matchSummaries.size(); ++i) {
            const float y = tableY + 47.f + static_cast<float>(i) * 48.f
                - matchHistoryScroll;
            if (y < tableY + 47.f || y + 42.f > 635.f)
                continue;

            sf::RectangleShape row({650.f, 42.f});
            row.setPosition({tableX, y});
            row.setFillColor(i % 2 == 0
                ? uiColor(48, 48, 48) : uiColor(39, 39, 39));
            window.draw(row);

            const MatchSummary& match = matchSummaries[i];
            const std::string values[] = {
                shortText(match.date.empty() ? "-" : match.date, 12),
                shortText(match.white, 18),
                shortText(match.black, 18),
                shortText(match.result, 14)
            };
            for (int col = 0; col < 4; ++col) {
                sf::Text value(font, values[col], 15);
                value.setPosition({columns[col], y + 13.f});
                value.setFillColor(textWhite());
                window.draw(value);
            }
        }

        if (matchSummaries.size() > 8) {
            sf::Text more(font,
                "Scroll to view all " +
                    std::to_string(matchSummaries.size()) + " matches.", 14);
            more.setPosition({475.f, 640.f});
            more.setFillColor(uiColor(190, 190, 190));
            window.draw(more);
        }
    }

    sf::RectangleShape backButton({200.f, 55.f});
    backButton.setPosition({500.f, 665.f});
    backButton.setFillColor(uiColor(65, 65, 65));
    window.draw(backButton);

    sf::Text backText(font, "BACK", 18);
    backText.setPosition({570.f, 682.f});
    backText.setFillColor(textWhite());
    window.draw(backText);
}

void ChessUI::submitAccountForm() {
    if (accountInput.empty() || accountPassword.empty()) {
        accountMessage = "Enter both a username and password.";
        return;
    }

    const bool defaultAccount = !accountSignUpMode &&
        accountInput == "jarvis" && accountPassword == "jarvis1234";

    if (!defaultAccount && !authHandler) {
        accountMessage = accountSignUpMode
            ? "The authentication service is not connected yet."
            : "Use the default account: jarvis / jarvis1234.";
        return;
    }

    bool accepted = defaultAccount;
    if (!defaultAccount) {
        try {
            accepted = authHandler(accountInput, accountPassword, accountSignUpMode);
        }
        catch (...) {
            accountMessage = "The authentication service returned an error.";
            return;
        }
    }

    if (!accepted) {
        accountMessage = accountSignUpMode
            ? "Sign up failed. Check the details and try again."
            : "Sign in failed. Check the username and password.";
        return;
    }

    accountSignedIn = true;
    accountName = accountInput;
    accountPassword.clear();
    accountMessage.clear();
    profilePicturePickerOpen = false;
    currentScreen = Screen::Account;
}

void ChessUI::loadProfilePictureOptions() {
    profilePictureOptions.clear();
    profilePictureOptionTextures.clear();

    std::vector<std::filesystem::path> pictureFiles;
    std::error_code fsError;
    if (!std::filesystem::exists("assets/pfp", fsError) || fsError) return;
    for (const auto& entry : std::filesystem::directory_iterator("assets/pfp", fsError)) {
        if (fsError) break;
        if (entry.is_regular_file())
            pictureFiles.push_back(entry.path());
    }
    std::sort(pictureFiles.begin(), pictureFiles.end());

    for (const std::filesystem::path& file : pictureFiles) {
        sf::Texture texture;
        static_cast<void>(texture.loadFromFile(file.string()));
        profilePictureOptions.push_back(file.string());
        profilePictureOptionTextures.push_back(std::move(texture));
    }
}

bool ChessUI::isBoardFlipped() const {
    bool automaticFlip =
        gameMode == GameMode::PlayerVsEngine &&
        engineSide == 1;

    return automaticFlip != manualBoardFlip;
}

int ChessUI::displayRow(int row) const { return isBoardFlipped() ? 7 - row : row; }
int ChessUI::displayCol(int col) const { return isBoardFlipped() ? 7 - col : col; }

void ChessUI::drawBoard() {
    for (int row = 0; row < 8; row++) {
        for (int col = 0; col < 8; col++) {
            float x = static_cast<float>(boardX + col * squareSize);
            float y = static_cast<float>(boardY + row * squareSize);

            sf::Texture& tileTexture =
                ((row + col) % 2 == 0)
                ? whiteTileTexture
                : blackTileTexture;

            sf::Sprite tile(tileTexture);
            const sf::Vector2u size = tileTexture.getSize();
            tile.setScale({
                static_cast<float>(squareSize) / size.x,
                static_cast<float>(squareSize) / size.y
            });
            tile.setPosition({x, y});
            window.draw(tile);
        }
    }
}

void ChessUI::drawLastMoveHighlights() {
    if (currentHistoryNode < 0 || currentHistoryNode >= static_cast<int>(historyTree.size()))
        return;

    auto drawSquare = [this](int row, int col, sf::Color color) {
        if (row < 0 || row >= 8 || col < 0 || col >= 8)
            return;

        sf::RectangleShape highlight({ static_cast<float>(squareSize), static_cast<float>(squareSize)});
        highlight.setPosition({ static_cast<float>(boardX + displayCol(col) * squareSize), static_cast<float>(boardY + displayRow(row) * squareSize)});
        highlight.setFillColor(color);
        window.draw(highlight);
    };

    const MoveHistoryEntry& last = historyTree[currentHistoryNode].move;
    const sf::Color sourceYellow(255, 220, 0, 105);
    const sf::Color destinationOrange(255, 155, 0, 120);
    const sf::Color finalDarkOrange(205, 112, 18, 130);

    const int parent = historyTree[currentHistoryNode].parent;
    if (parent >= 0 && parent < static_cast<int>(historyTree.size())) {
        const MoveHistoryEntry& previous = historyTree[parent].move;
        const bool sameSide = (previous.piece > 0) == (last.piece > 0);

        if (sameSide &&
            previous.toRow == last.fromRow && previous.toCol == last.fromCol) {
            drawSquare(previous.fromRow, previous.fromCol, sourceYellow);
            drawSquare(previous.toRow, previous.toCol, destinationOrange);
            drawSquare(last.toRow, last.toCol, finalDarkOrange);
            return;
        }

        if (sameSide) {
            drawSquare(previous.fromRow, previous.fromCol, sourceYellow);
            drawSquare(previous.toRow, previous.toCol, destinationOrange);
            drawSquare(last.fromRow, last.fromCol, sourceYellow);
            drawSquare(last.toRow, last.toCol, destinationOrange);
            return;
        }
    }

    drawSquare(last.fromRow, last.fromCol, sourceYellow);
    drawSquare(last.toRow, last.toCol, destinationOrange);
}

void ChessUI::drawSidePanel() {
    sf::RectangleShape panel( sf::Vector2f( sidePanelWidth, sidePanelHeight ) );
    panel.setPosition( sf::Vector2f( sidePanelX, sidePanelY ) );
    panel.setFillColor( uiColor(45, 45, 45) );

    window.draw(panel);
}

void ChessUI::drawClocks() {
    if (gameMode == GameMode::EngineVsEngine)
        return;

    auto fmt = [](float seconds) {
        int total = static_cast<int>(std::ceil(seconds));
        if (total < 0) total = 0;
        char buffer[16];
        std::snprintf(buffer, sizeof(buffer), "%02d:%02d", total / 60, total % 60);
        return std::string(buffer);
    };

    const int turn = game.get_current_turn();
    const bool running = currentScreen == Screen::Game && !game.has_game_ended() && !historyPaused;

    auto drawOne = [&](const char* label, float seconds, bool active, float x) {
        sf::RectangleShape box({150.f, 40.f});
        box.setPosition({x, 730.f});
        box.setFillColor(active && running ? uiColor(70, 120, 75) : uiColor(60, 60, 60));
        window.draw(box);

        sf::Text text(font, std::string(label) + " " + fmt(seconds), 20);
        text.setPosition({x + 12.f, 737.f});
        text.setFillColor(seconds <= 15.f ? uiColor(255, 120, 120) : textWhite());
        window.draw(text);
    };

    drawOne("White", turn == 0 ? moveTimeLeft : matchSeconds, turn == 0, 760.f);
    drawOne("Black", turn == 1 ? moveTimeLeft : matchSeconds, turn == 1, 930.f);
}

void ChessUI::drawText() {
    std::string mode;

    if (gameMode == GameMode::PlayerVsPlayer) {
        mode = "Player vs Player";
    }

    else if (gameMode == GameMode::PlayerVsEngine) {
        if (engineSide == 0)
            mode = "Player vs Engine | Player: Black";
        else
            mode = "Player vs Engine | Player: White";
    }

    else {
        mode = "Engine vs Engine";
    }
    std::string turnText = game.get_current_turn() == 0 ? "White" : "Black";
    std::string status = game.has_game_ended() ? "Game ended" :
        turnText + " to move (" + std::to_string(game.get_moves_left()) + " actions left)";
    if (engineStalled) status = "Engine has no legal move";
    sf::Text turnLabel(font, mode + " | " + status, 20);
    turnLabel.setPosition({static_cast<float>(boardX), 35.f});
    window.draw(turnLabel);

    sf::Text historyTitle(font, "MOVE HISTORY", 28);
    historyTitle.setPosition({sidePanelX + 20.f, sidePanelY + 20.f});
    historyTitle.setFillColor(textWhite());
    window.draw(historyTitle);
}

void ChessUI::drawPieces() {
    int draggedPiece = 0;
    int row = 0;

    for (auto& boardRow : game) {
        int col = 0;
        for (auto& piece : boardRow) {
            if (piece != 0) {
                if (engineMoveAnimating &&
                    row == animatedFromRow && col == animatedFromCol) {
                    col++;
                    continue;
                }

                if (dragging && row == draggedRow && col == draggedCol) {
                    draggedPiece = piece;
                    col++;
                    continue;
                }
                sf::Sprite sprite(pieceTextures[piece]);

                auto textureSize = pieceTextures[piece].getSize();
                float maxSize = static_cast<float>( std::max(textureSize.x, textureSize.y) );
                float scale = 64.f / maxSize;
                sprite.setScale({scale, scale});

                float width = textureSize.x * scale;
                float height = textureSize.y * scale;
                int shownCol = displayCol(col);
                int shownRow = displayRow(row);

                float x = boardX + shownCol * squareSize + (squareSize - width) / 2.f;
                float y = boardY + shownRow * squareSize + (squareSize - height) / 2.f;
                sprite.setPosition({x, y});
                window.draw(sprite);
            }
            col++;
        }
        row++;
    }

    if (dragging && draggedPiece != 0) {
        sf::Sprite sprite(pieceTextures[draggedPiece]);

        auto textureSize =
            pieceTextures[draggedPiece].getSize();

        float maxSize = static_cast<float>(
            std::max(textureSize.x, textureSize.y)
        );

        float scale = 64.f * 1.3f / maxSize;

        sprite.setScale({scale, scale});

        float width = textureSize.x * scale;
        float height = textureSize.y * scale;

        sprite.setPosition({
            mousePosition.x - width / 2.f,
            mousePosition.y - height / 2.f
        });

        window.draw(sprite);
    }

    if (engineMoveAnimating && animatedPiece != 0) {
        sf::Texture& texture = pieceTextures[animatedPiece];
        const sf::Vector2u textureSize = texture.getSize();
        const float maxSize = static_cast<float>(
            std::max(textureSize.x, textureSize.y));
        sf::Sprite sprite(texture);
        const float scale = 64.f / maxSize;
        const float width = textureSize.x * scale;
        const float height = textureSize.y * scale;
        const float progress = std::clamp(
            engineAnimationClock.getElapsedTime().asSeconds() /
                engineSlideSeconds,
            0.f, 1.f);

        const sf::Vector2f from{
            boardX + displayCol(animatedFromCol) * squareSize + squareSize / 2.f,
            boardY + displayRow(animatedFromRow) * squareSize + squareSize / 2.f};
        const sf::Vector2f to{
            boardX + displayCol(animatedToCol) * squareSize + squareSize / 2.f,
            boardY + displayRow(animatedToRow) * squareSize + squareSize / 2.f};
        sprite.setScale({scale, scale});
        sprite.setPosition({
            from.x + (to.x - from.x) * progress - width / 2.f,
            from.y + (to.y - from.y) * progress - height / 2.f});
        window.draw(sprite);
    }
}

namespace {
std::vector<std::string> scanThemeFolders(const std::string& prefix, int plainNumber) {
    namespace fs = std::filesystem;
    std::vector<std::pair<int, std::string>> found;

    if (fs::exists("assets")) {
        for (const auto& entry : fs::directory_iterator("assets")) {
            if (!entry.is_directory())
                continue;

            std::string name = entry.path().filename().string();

            if (name == prefix) {
                found.push_back({plainNumber, "assets/" + name + "/"});
            }
            else if (name.rfind(prefix, 0) == 0) {
                try {
                    int number = std::stoi(name.substr(prefix.size()));
                    found.push_back({number, "assets/" + name + "/"});
                }
                catch (...) {
                }
            }
        }
    }

    std::sort(found.begin(), found.end(),
        [](const auto& a, const auto& b) { return a.first < b.first; });

    std::vector<std::string> folders;
    for (const auto& item : found)
        folders.push_back(item.second);
    return folders;
}
}

void ChessUI::refreshPieceThemes() {
    // Curated pairs: piece set i is always used together with board set i.
    const std::vector<std::pair<std::string, std::string>> pairs = {
        {"assets/pieces/",  "assets/board8/"},
        {"assets/pieces5/", "assets/board6/"},
        {"assets/pieces7/", "assets/board17/"},
        {"assets/pieces8/", "assets/board18/"}
    };

    pieceThemeFolders.clear();
    boardThemeFolders.clear();
    for (const auto& pair : pairs) {
        if (std::filesystem::exists(pair.first + "white_king.png") &&
            std::filesystem::exists(pair.second + "white_tile.png") &&
            std::filesystem::exists(pair.second + "black_tile.png")) {
            pieceThemeFolders.push_back(pair.first);
            boardThemeFolders.push_back(pair.second);
        }
    }
    if (pieceThemeFolders.empty()) {
        pieceThemeFolders.push_back("assets/pieces/");
        boardThemeFolders.push_back("assets/board/");
    }

    pieceThemeKings.clear();
    boardWhitePreviews.clear();
    boardBlackPreviews.clear();
    themePreviewPieces.clear();

    const char* names[12] = {
        "white_king", "white_queen", "white_bishop",
        "white_knight", "white_rook", "white_pawn",
        "black_king", "black_queen", "black_bishop",
        "black_knight", "black_rook", "black_pawn"
    };

    for (size_t i = 0; i < pieceThemeFolders.size(); i++) {
        sf::Texture kingTexture;
        static_cast<void>(kingTexture.loadFromFile(pieceThemeFolders[i] + "white_king.png"));
        pieceThemeKings.push_back(std::move(kingTexture));

        std::vector<sf::Texture> set(12);
        for (int k = 0; k < 12; k++)
            static_cast<void>(set[k].loadFromFile(pieceThemeFolders[i] + names[k] + ".png"));
        themePreviewPieces.push_back(std::move(set));

        sf::Texture whitePreview;
        sf::Texture blackPreview;
        static_cast<void>(whitePreview.loadFromFile(boardThemeFolders[i] + "white_tile.png"));
        static_cast<void>(blackPreview.loadFromFile(boardThemeFolders[i] + "black_tile.png"));
        boardWhitePreviews.push_back(std::move(whitePreview));
        boardBlackPreviews.push_back(std::move(blackPreview));
    }

    if (pieceTheme >= static_cast<int>(pieceThemeFolders.size()))
        pieceTheme = 0;
    boardTheme = pieceTheme;
}

void ChessUI::refreshBoardThemes() {
    // Board themes are paired with piece themes; refreshPieceThemes() fills both.
    if (boardThemeFolders.empty())
        refreshPieceThemes();
    boardTheme = pieceTheme;
}

void ChessUI::loadBoardTextures() {
    const std::string& folder = boardThemeFolders[boardTheme];
    static_cast<void>(whiteTileTexture.loadFromFile(folder + "white_tile.png"));
    static_cast<void>(blackTileTexture.loadFromFile(folder + "black_tile.png"));
}

void ChessUI::loadPieceTextures() {
    const std::string& folder = pieceThemeFolders[pieceTheme];

    static_cast<void>(pieceTextures[1].loadFromFile(folder + "white_pawn.png"));
    static_cast<void>(pieceTextures[2].loadFromFile(folder + "white_knight.png"));
    static_cast<void>(pieceTextures[3].loadFromFile(folder + "white_bishop.png"));
    static_cast<void>(pieceTextures[4].loadFromFile(folder + "white_rook.png"));
    static_cast<void>(pieceTextures[5].loadFromFile(folder + "white_queen.png"));
    static_cast<void>(pieceTextures[6].loadFromFile(folder + "white_king.png"));

    static_cast<void>(pieceTextures[-1].loadFromFile(folder + "black_pawn.png"));
    static_cast<void>(pieceTextures[-2].loadFromFile(folder + "black_knight.png"));
    static_cast<void>(pieceTextures[-3].loadFromFile(folder + "black_bishop.png"));
    static_cast<void>(pieceTextures[-4].loadFromFile(folder + "black_rook.png"));
    static_cast<void>(pieceTextures[-5].loadFromFile(folder + "black_queen.png"));
    static_cast<void>(pieceTextures[-6].loadFromFile(folder + "black_king.png"));
}

void ChessUI::drawLegalMoves() {
    for (const auto& move : legalMoves) {
        int row = move.first;
        int col = move.second;
        auto boardRow = game.begin() + row;
        int pieceOnSquare = (*boardRow)[col];

        if (pieceOnSquare == 0) {
            sf::CircleShape dot(10.f);
            dot.setOrigin({10.f, 10.f});
            int shownCol = displayCol(col);
            int shownRow = displayRow(row);
            dot.setPosition({ boardX + shownCol * squareSize + squareSize / 2.f, boardY + shownRow * squareSize + squareSize / 2.f});
            dot.setFillColor( uiColor(40, 40, 40, 110) );
            window.draw(dot);
        }

        else {
            sf::CircleShape ring(32.f);
            ring.setOrigin({32.f, 32.f});
            int shownCol = displayCol(col);
            int shownRow = displayRow(row);
            ring.setPosition({ boardX + shownCol * squareSize + squareSize / 2.f, boardY + shownRow * squareSize + squareSize / 2.f });
            ring.setFillColor(sf::Color::Transparent);
            ring.setOutlineColor( uiColor(180, 40, 40, 180) );
            ring.setOutlineThickness(5.f);
            window.draw(ring);
        }
    }
}

void ChessUI::drawMoveHistory() {
    if (historyTree.empty())
        return;

    const float iconSize = 45.f;
    std::vector<sf::Vector2f> positions = getHistoryNodePositions();

    for (int i = 0; i < static_cast<int>(historyTree.size()); i++) {
        int parent = historyTree[i].parent;

        if (parent < 0)
            continue;

        float x1 = positions[parent].x + iconSize / 2.f;
        float y1 = positions[parent].y - historyScroll + iconSize / 2.f;

        float x2 = positions[i].x + iconSize / 2.f;
        float y2 = positions[i].y - historyScroll + iconSize / 2.f;

        float historyTop = sidePanelY + 70.f;
        float historyBottom = sidePanelY + sidePanelHeight;

        if (y1 - iconSize / 2.f < historyTop || y1 + iconSize / 2.f > historyBottom || y2 - iconSize / 2.f < historyTop || y2 + iconSize / 2.f > historyBottom)
            continue;

        float dx = x2 - x1;
        float dy = y2 - y1;

        float length = std::sqrt(dx * dx + dy * dy);
        float angle = std::atan2(dy, dx) * 180.f / 3.14159265f;

        sf::RectangleShape line({length, 3.f});
        line.setOrigin({0.f, 1.5f});
        line.setPosition({x1, y1});
        line.setRotation(sf::degrees(angle));
        line.setFillColor(uiColor(120, 120, 120));

        window.draw(line);
    }

    for (int i = 0; i < static_cast<int>(historyTree.size()); i++) {
        float x = positions[i].x;
        float y = positions[i].y - historyScroll;
        float historyTop = sidePanelY + 70.f;
        float historyBottom = sidePanelY + sidePanelHeight;

        if (y < historyTop || y + iconSize > historyBottom)
            continue;

        bool currentNode = i == currentHistoryNode;

        sf::RectangleShape nodeBox({iconSize, iconSize});
        nodeBox.setPosition({x, y});
        nodeBox.setFillColor(uiColor(55, 55, 55));

        if (currentNode) {
            nodeBox.setOutlineColor(uiColor(255, 215, 0));
            nodeBox.setOutlineThickness(3.f);
        }

        window.draw(nodeBox);

        const MoveHistoryEntry& move = historyTree[i].move;

        if (historyStyle == HistoryStyle::Pictogram) {
            int piece = move.piece;
            sf::Sprite sprite(pieceTextures[piece]);

            auto textureSize = pieceTextures[piece].getSize();
            float maxSize = static_cast<float>( std::max(textureSize.x, textureSize.y) );
            float scale = 36.f / maxSize;

            sprite.setScale({scale, scale});

            float width = textureSize.x * scale;
            float height = textureSize.y * scale;

            sprite.setPosition({
                x + (iconSize - width) / 2.f,
                y + (iconSize - height) / 2.f
            });

            window.draw(sprite);
        }

        else {
            sf::Text moveText(font, moveToText(move), 11);
            moveText.setPosition({x + 3.f, y + 14.f});
            moveText.setFillColor(textWhite());

            window.draw(moveText);
        }
    }

    const auto& positionsForScroll = positions;

    float contentRight = sidePanelX + 20.f;

    for (const auto& pos : positionsForScroll) {
        contentRight = std::max(
            contentRight,
            pos.x + historyHorizontalScroll + 45.f
        );
    }

    const float visibleLeft = sidePanelX + 20.f;
    const float visibleRight = sidePanelX + sidePanelWidth - 20.f;
    const float visibleWidth = visibleRight - visibleLeft;
    const float contentWidth =
        std::max(visibleWidth, contentRight - visibleLeft);

    if (contentWidth > visibleWidth) {
        const float trackY = sidePanelY + sidePanelHeight - 18.f;

        sf::RectangleShape track({visibleWidth, 7.f});
        track.setPosition({visibleLeft, trackY});
        track.setFillColor(uiColor(70, 70, 70));
        window.draw(track);

        float thumbWidth =
            visibleWidth * (visibleWidth / contentWidth);

        thumbWidth = std::max(45.f, thumbWidth);

        const float maxHorizontalScroll =
            contentWidth - visibleWidth;

        const float thumbTravel =
            visibleWidth - thumbWidth;

        float thumbX = visibleLeft;

        if (maxHorizontalScroll > 0.f) {
            thumbX +=
                (historyHorizontalScroll / maxHorizontalScroll)
                * thumbTravel;
        }

        sf::RectangleShape thumb({thumbWidth, 9.f});
        thumb.setPosition({thumbX, trackY - 1.f});
        thumb.setFillColor(uiColor(175, 175, 175));
        window.draw(thumb);
    }
}

void ChessUI::drawHistoryPreview() {
    const float historyLeft = sidePanelX;
    const float historyRight = sidePanelX + sidePanelWidth;
    const float historyTop = sidePanelY;
    const float historyBottom = sidePanelY + sidePanelHeight;

    bool mouseInsideHistory =
        mousePosition.x >= historyLeft &&
        mousePosition.x <= historyRight &&
        mousePosition.y >= historyTop &&
        mousePosition.y <= historyBottom;

    if (!mouseInsideHistory) {
        hoveredHistoryNode = -1;
        return;
    }

    if (historyTree.empty())
        return;

    const float iconSize = 45.f;
    std::vector<sf::Vector2f> positions = getHistoryNodePositions();

    for (int i = 0; i < static_cast<int>(historyTree.size()); i++) {
        float x = positions[i].x;
        float y = positions[i].y - historyScroll;

        const float nodeTop = sidePanelY + 70.f;
        const float nodeBottom = sidePanelY + sidePanelHeight - 30.f;

        if (x + iconSize < historyLeft || x > historyRight)
            continue;

        if (y < nodeTop || y + iconSize > nodeBottom)
            continue;

        if (mousePosition.x >= x &&
            mousePosition.x <= x + iconSize &&
            mousePosition.y >= y &&
            mousePosition.y <= y + iconSize) {
            hoveredHistoryNode = i;
            break;
        }
    }

    if (hoveredHistoryNode == -1)
        return;

    int parent = historyTree[hoveredHistoryNode].parent;

    Board previewBoard = parent == -1
        ? startingBoard
        : historyTree[parent].move.boardAfterMove;

    drawBoardPosition(previewBoard);
    drawMoveArrow(historyTree[hoveredHistoryNode].move);
}

int ChessUI::getHistoryDepth(int nodeIndex) const {
    int depth = 0;
    int node = nodeIndex;

    while (node >= 0 && historyTree[node].parent >= 0) {
        depth++;
        node = historyTree[node].parent;
    }

    return depth;
}

std::vector<sf::Vector2f> ChessUI::getHistoryNodePositions() const {
    std::vector<sf::Vector2f> positions(historyTree.size());

    if (historyTree.empty())
        return positions;

    std::vector<int> branchColumn(historyTree.size(), 0);
    int nextFreeColumn = 1;

    for (int i = 0; i < static_cast<int>(historyTree.size()); i++) {
        int parent = historyTree[i].parent;

        if (parent < 0) {
            branchColumn[i] = 0;
            continue;
        }

        const std::vector<int>& siblings = historyTree[parent].children;

        int childNumber = 0;

        for (int j = 0; j < static_cast<int>(siblings.size()); j++) {
            if (siblings[j] == i) {
                childNumber = j;
                break;
            }
        }

        if (childNumber == 0) {
            branchColumn[i] = branchColumn[parent];
        }

        else {
            branchColumn[i] = nextFreeColumn;
            nextFreeColumn++;
        }
    }

    const float startX = sidePanelX + 25.f;
    const float startY = sidePanelY + 75.f;
    const float branchSpacing = 62.f;
    const float rowSpacing = 70.f;

    for (int i = 0; i < static_cast<int>(historyTree.size()); i++) {
        int depth = getHistoryDepth(i);

        positions[i] = {
            startX + branchColumn[i] * branchSpacing - historyHorizontalScroll,
            startY + depth * rowSpacing
        };
    }

    return positions;
}

void ChessUI::drawSettings() {
    sf::RectangleShape windowBox({1200.f, 800.f});
    windowBox.setPosition({0.f, 0.f});
    windowBox.setFillColor(uiColor(12, 13, 15));
    window.draw(windowBox);

    sf::Text title(font, "SETTINGS", 34);
    title.setPosition({72.f, 38.f});
    title.setFillColor(textWhite());
    window.draw(title);

    sf::Text backArrow(font, "<", 30);
    backArrow.setPosition({36.f, 37.f});
    backArrow.setFillColor(uiColor(190, 192, 196));
    window.draw(backArrow);

    sf::Text description(font,
        "Adjust how you move pieces and view the chessboard.", 14);
    description.setPosition({74.f, 91.f});
    description.setFillColor(uiColor(160, 163, 169));
    window.draw(description);

    sf::RectangleShape gameplayTab({140.f, 38.f});
    gameplayTab.setPosition({812.f, 42.f});
    gameplayTab.setFillColor(settingsTab == 0
        ? uiColor(46, 47, 50) : uiColor(25, 26, 28));
    gameplayTab.setOutlineColor(uiColor(55, 57, 61));
    gameplayTab.setOutlineThickness(1.f);
    window.draw(gameplayTab);

    sf::Text gameplayText(font, "GAMEPLAY", 16);
    gameplayText.setPosition({832.f, 52.f});
    gameplayText.setFillColor(textWhite());
    window.draw(gameplayText);

    sf::RectangleShape designTab({140.f, 38.f});
    designTab.setPosition({962.f, 42.f});
    designTab.setFillColor(settingsTab == 1
        ? uiColor(46, 47, 50) : uiColor(25, 26, 28));
    designTab.setOutlineColor(uiColor(55, 57, 61));
    designTab.setOutlineThickness(1.f);
    window.draw(designTab);

    sf::Text designText(font, "DESIGN", 16);
    designText.setPosition({1008.f, 52.f});
    designText.setFillColor(textWhite());
    window.draw(designText);

    if (settingsTab == 0)
        drawGameplaySettings();
    else
        drawDesignSettings();

    if (gameStarted) {
        sf::RectangleShape playAgainButton({150.f, 42.f});
        playAgainButton.setPosition({350.f, 662.f});
        playAgainButton.setFillColor(uiColor(70, 120, 75));
        window.draw(playAgainButton);

        sf::Text playAgainText(font, "PLAY AGAIN", 14);
        playAgainText.setPosition({374.f, 676.f});
        playAgainText.setFillColor(textWhite());
        window.draw(playAgainText);
    }

    sf::RectangleShape menuButton({150.f, 42.f});
    menuButton.setPosition({525.f, 662.f});
    menuButton.setFillColor(uiColor(65, 65, 65));
    window.draw(menuButton);

    sf::Text menuText(font, "MAIN MENU", 14);
    menuText.setPosition({552.f, 676.f});
    menuText.setFillColor(textWhite());
    window.draw(menuText);

    sf::RectangleShape backButton({150.f, 42.f});
    backButton.setPosition({700.f, 662.f});
    backButton.setFillColor(uiColor(55, 55, 55));
    window.draw(backButton);

    sf::Text backText(font, "BACK", 14);
    backText.setPosition({756.f, 676.f});
    backText.setFillColor(textWhite());
    window.draw(backText);
}

void ChessUI::drawGameplaySettings() {
    auto drawRow = [&](const std::string& label, float y, bool hasHint) {
        sf::Text labelText(font, label, hasHint ? 17 : 18);
        labelText.setPosition({315.f, y + (hasHint ? 3.f : 13.f)});
        labelText.setFillColor(uiColor(238, 239, 242));
        window.draw(labelText);

        sf::RectangleShape separator({620.f, 1.f});
        separator.setPosition({290.f, y + 49.f});
        separator.setFillColor(uiColor(47, 49, 53));
        window.draw(separator);
    };

    auto drawToggle = [&](const std::string& label, bool enabled, float y,
                          bool slideHint = false) {
        drawRow(label, y, slideHint);

        if (slideHint) {
            sf::Text hint(font,
                "Click and engine moves slide; dragging stays direct.", 12);
            hint.setPosition({315.f, y + 29.f});
            hint.setFillColor(uiColor(170, 174, 181));
            window.draw(hint);
        }

        sf::RectangleShape toggle({86.f, 30.f});
        toggle.setPosition({824.f, y + 9.f});
        toggle.setFillColor(enabled
            ? uiColor(63, 128, 78)
            : uiColor(78, 82, 88));
        toggle.setOutlineColor(enabled
            ? uiColor(91, 157, 104)
            : uiColor(104, 108, 114));
        toggle.setOutlineThickness(1.f);
        window.draw(toggle);

        sf::Text state(font, enabled ? "ON" : "OFF", 13);
        state.setPosition({enabled ? 857.f : 852.f, y + 16.f});
        state.setFillColor(enabled ? sf::Color::White : textWhite());
        window.draw(state);
    };

    drawToggle("Drag pieces", dragPieces, 220.f);
    drawToggle("Slide pieces", slidePieces, 274.f, true);
    drawToggle("Show possible moves", showPossibleMoves, 328.f);
    drawToggle("Board coordinates", showCoordinates, 382.f);
    drawToggle("Sound effects", soundEffects, 436.f);

    drawRow("Move history", 490.f, false);
    sf::RectangleShape historyToggle({86.f, 30.f});
    historyToggle.setPosition({824.f, 499.f});
    historyToggle.setFillColor(uiColor(78, 82, 88));
    historyToggle.setOutlineColor(uiColor(104, 108, 114));
    historyToggle.setOutlineThickness(1.f);
    window.draw(historyToggle);

    sf::Text historyState(
        font,
        historyStyle == HistoryStyle::Pictogram ? "ICONS" : "TEXT",
        12
    );
    historyState.setPosition({historyStyle == HistoryStyle::Pictogram ? 847.f : 851.f,
                              507.f});
    historyState.setFillColor(textWhite());
    window.draw(historyState);

    drawToggle("Fullscreen (F11)", fullscreen, 544.f);
    drawToggle("Light mode", gLightMode, 598.f);
}

void ChessUI::drawDesignSettings() {
    sf::Text title(font, "Game theme  -  pieces and board come as a pair", 20);
    title.setPosition({290.f, 200.f});
    title.setFillColor(uiColor(220, 220, 220));
    window.draw(title);

    const float tile = 46.f;
    const float pieceMax = 42.f;

    for (int i = 0; i < static_cast<int>(pieceThemeFolders.size()); i++) {
        const float x = 290.f + i * 157.f;
        const float y = 235.f;
        const bool selected = (i == pieceTheme);

        sf::RectangleShape card({148.f, 290.f});
        card.setPosition({x, y});
        card.setFillColor(selected ? uiColor(70, 100, 70) : uiColor(52, 52, 52));
        card.setOutlineColor(selected ? uiColor(255, 255, 255) : uiColor(80, 80, 80));
        card.setOutlineThickness(selected ? 3.f : 1.f);
        window.draw(card);

        static const char* themeNames[] = {"Classic", "Pixel", "Latte", "Cat"};
        sf::Text name(font, i < 4 ? themeNames[i] : "Theme " + std::to_string(i + 1), 17);
        name.setPosition({x + 10.f, y + 8.f});
        name.setFillColor(textWhite());
        window.draw(name);

        const float bx = x + 5.f;
        const float by = y + 38.f;

        for (int r = 0; r < 4; r++) {
            for (int c = 0; c < 3; c++) {
                const bool light = (r + c) % 2 == 0;
                const sf::Texture& tex = light ? boardWhitePreviews[i] : boardBlackPreviews[i];
                sf::Sprite sq(tex);
                const sf::Vector2u ts = tex.getSize();
                sq.setScale({tile / ts.x, tile / ts.y});
                sq.setPosition({bx + c * tile, by + r * tile});
                window.draw(sq);

                const sf::Texture& pt = themePreviewPieces[i][r * 3 + c];
                const sf::Vector2u ps = pt.getSize();
                const float m = static_cast<float>(std::max(ps.x, ps.y));
                if (m <= 0.f)
                    continue;
                const float sc = pieceMax / m;
                sf::Sprite piece(pt);
                piece.setScale({sc, sc});
                piece.setPosition({
                    bx + c * tile + (tile - ps.x * sc) / 2.f,
                    by + r * tile + (tile - ps.y * sc) / 2.f
                });
                window.draw(piece);
            }
        }

        sf::Text state(font, selected ? "SELECTED" : "Click to use", 13);
        state.setPosition({x + 10.f, y + 262.f});
        state.setFillColor(selected ? uiColor(190, 230, 190) : uiColor(170, 174, 181));
        window.draw(state);
    }
}

void ChessUI::drawEndScreen() {
    sf::RectangleShape dimmer({1200.f, 800.f});
    dimmer.setPosition({0.f, 0.f});
    dimmer.setFillColor(uiColor(0, 0, 0, 155));
    window.draw(dimmer);

    sf::RectangleShape endBox({520.f, 550.f});
    endBox.setPosition({340.f, 140.f});
    endBox.setFillColor(uiColor(45, 45, 45));
    window.draw(endBox);

    sf::Text title(font, "GAME OVER", 48);
    title.setPosition({465.f, 235.f});
    title.setFillColor(textWhite());
    window.draw(title);

    std::string endMessage = "The game has ended.";
    if (timeExpired)
        endMessage = std::string("Time's up! ") + (timeoutLoser == 0 ? "Black" : "White") + " wins on time.";
    sf::Text message(font, endMessage, 24);
    message.setPosition({477.f, 330.f});
    message.setFillColor(uiColor(210, 210, 210));
    window.draw(message);

    sf::RectangleShape playAgain({200.f, 65.f});
    playAgain.setPosition({390.f, 500.f});
    playAgain.setFillColor(uiColor(70, 120, 75));
    window.draw(playAgain);

    sf::Text playAgainText(font, "PLAY AGAIN", 20);
    playAgainText.setPosition({435.f, 520.f});
    playAgainText.setFillColor(textWhite());
    window.draw(playAgainText);

    sf::RectangleShape settings({200.f, 65.f});
    settings.setPosition({610.f, 500.f});
    settings.setFillColor(uiColor(65, 65, 65));
    window.draw(settings);

    sf::Text settingsText(font, "SETTINGS", 20);
    settingsText.setPosition({665.f, 520.f});
    settingsText.setFillColor(textWhite());
    window.draw(settingsText);

    sf::RectangleShape menu({200.f, 60.f});
    menu.setPosition({500.f, 590.f});
    menu.setFillColor(uiColor(55, 55, 55));
    window.draw(menu);

    sf::Text menuText(font, "MAIN MENU", 18);
    menuText.setPosition({550.f, 608.f});
    menuText.setFillColor(textWhite());
    window.draw(menuText);
}

void ChessUI::drawCapturedPieces() {
    std::vector<int> capturedByWhite;
    std::vector<int> capturedByBlack;

    for (const auto& move : moveHistory) {
        if (move.capturedPiece == 0)
            continue;

        if (move.capturedPiece < 0)
            capturedByWhite.push_back(move.capturedPiece);
        else
            capturedByBlack.push_back(move.capturedPiece);
    }

    sf::Text whiteLabel(font, "White captured:", 14);
    whiteLabel.setPosition({50.f, 726.f});
    whiteLabel.setFillColor(textWhite());
    window.draw(whiteLabel);

    sf::Text blackLabel(font, "Black captured:", 14);
    blackLabel.setPosition({50.f, 760.f});
    blackLabel.setFillColor(textWhite());
    window.draw(blackLabel);

    auto drawCapturedRow = [&](const std::vector<int>& pieces, float y) {
        float x = 175.f;

        for (int piece : pieces) {
            sf::Sprite sprite(pieceTextures[piece]);

            auto textureSize = pieceTextures[piece].getSize();
            float maxSize = static_cast<float>(std::max(textureSize.x, textureSize.y));
            float scale = 25.f / maxSize;

            sprite.setScale({scale, scale});

            float width = textureSize.x * scale;
            float height = textureSize.y * scale;

            sprite.setPosition({x, y + (25.f - height) / 2.f});
            window.draw(sprite);

            x += width + 4.f;
        }
    };

    drawCapturedRow(capturedByWhite, 724.f);
    drawCapturedRow(capturedByBlack, 758.f);
}

void ChessUI::drawCoordinates() {
    for (int col = 0; col < 8; col++) {
        char fileLetter = static_cast<char>('a' + col);

        sf::Text fileText(font, std::string(1, fileLetter), 13);

        int shownCol = displayCol(col);

        fileText.setPosition({
            static_cast<float>(boardX + shownCol * squareSize + squareSize - 14),
            static_cast<float>(boardY + boardSize - 18)
        });

        fileText.setFillColor(uiColor(35, 35, 35, 210));
        window.draw(fileText);
    }

    for (int row = 0; row < 8; row++) {
        char rankNumber = static_cast<char>('8' - row);

        sf::Text rankText(font, std::string(1, rankNumber), 13);

        int shownRow = displayRow(row);

        rankText.setPosition({
            static_cast<float>(boardX + 5),
            static_cast<float>(boardY + shownRow * squareSize + 2)
        });

        rankText.setFillColor(uiColor(35, 35, 35, 210));
        window.draw(rankText);
    }
}

void ChessUI::playSound(const sf::SoundBuffer& buffer) {
    if (!soundEffects)
        return;

    if (buffer.getDuration() == sf::Time::Zero)
        return;

    activeSound.emplace(buffer);
    activeSound->play();
}

void ChessUI::drawBoardPosition(Board& board) {
    drawBoard();

    if (showCoordinates)
        drawCoordinates();

    int row = 0;

    for (auto& boardRow : board) {
        int col = 0;

        for (auto& piece : boardRow) {
            if (piece != 0) {
                sf::Sprite sprite(pieceTextures[piece]);

                auto textureSize = pieceTextures[piece].getSize();
                float maxSize = static_cast<float>( std::max(textureSize.x, textureSize.y) );
                float scale = 64.f / maxSize;

                sprite.setScale({scale, scale});

                float width = textureSize.x * scale;
                float height = textureSize.y * scale;

                int shownCol = displayCol(col);
                int shownRow = displayRow(row);

                float x = boardX + shownCol * squareSize + (squareSize - width) / 2.f;
                float y = boardY + shownRow * squareSize + (squareSize - height) / 2.f;

                sprite.setPosition({x, y});
                window.draw(sprite);
            }

            col++;
        }

        row++;
    }
}

void ChessUI::drawMoveArrow(const MoveHistoryEntry& move) {
    float startArrowX = boardX + displayCol(move.fromCol) * squareSize + squareSize / 2.f;
    float startArrowY = boardY + displayRow(move.fromRow) * squareSize + squareSize / 2.f;

    float endArrowX = boardX + displayCol(move.toCol) * squareSize + squareSize / 2.f;
    float endArrowY = boardY + displayRow(move.toRow) * squareSize + squareSize / 2.f;

    float dx = endArrowX - startArrowX;
    float dy = endArrowY - startArrowY;

    float length = std::sqrt(dx * dx + dy * dy);
    float angle = std::atan2(dy, dx) * 180.f / 3.14159265f;

    const float arrowThickness = 7.f;
    const float arrowHeadLength = 24.f;

    sf::RectangleShape arrowLine({length, arrowThickness});
    arrowLine.setOrigin({0.f, arrowThickness / 2.f});
    arrowLine.setPosition({startArrowX, startArrowY});
    arrowLine.setRotation(sf::degrees(angle));
    arrowLine.setFillColor(uiColor(220, 40, 40, 220));
    window.draw(arrowLine);

    sf::RectangleShape arrowHead1({arrowHeadLength, arrowThickness});
    arrowHead1.setOrigin({0.f, arrowThickness / 2.f});
    arrowHead1.setPosition({endArrowX, endArrowY});
    arrowHead1.setRotation(sf::degrees(angle + 150.f));
    arrowHead1.setFillColor(uiColor(220, 40, 40, 220));
    window.draw(arrowHead1);

    sf::RectangleShape arrowHead2({arrowHeadLength, arrowThickness});
    arrowHead2.setOrigin({0.f, arrowThickness / 2.f});
    arrowHead2.setPosition({endArrowX, endArrowY});
    arrowHead2.setRotation(sf::degrees(angle - 150.f));
    arrowHead2.setFillColor(uiColor(220, 40, 40, 220));
    window.draw(arrowHead2);
}

std::string ChessUI::moveToText(const MoveHistoryEntry& move) const {
    auto squareName = [](int row, int col) {
        std::string square;
        square += static_cast<char>('a' + col);
        square += static_cast<char>('8' - row);
        return square;
    };

    char pieceLetter = ' ';

    int piece = std::abs(move.piece);

    if (piece == 2) pieceLetter = 'N';
    if (piece == 3) pieceLetter = 'B';
    if (piece == 4) pieceLetter = 'R';
    if (piece == 5) pieceLetter = 'Q';
    if (piece == 6) pieceLetter = 'K';

    std::string text;

    if (pieceLetter != ' ')
        text += pieceLetter;

    text += squareName(move.fromRow, move.fromCol);

    text += move.capturedPiece != 0 ? "x" : "-";

    text += squareName(move.toRow, move.toCol);

    if (move.promotionPiece != 0) {
        const int promotedType = std::abs(move.promotionPiece);
        if (promotedType == 2) text += "=N";
        else if (promotedType == 3) text += "=B";
        else if (promotedType == 4) text += "=R";
        else if (promotedType == 5) text += "=Q";
    }

    return text;
}

void ChessUI::rebuildLinearHistoryFromNode(int nodeIndex) {
    moveHistory.clear();

    if (nodeIndex < 0) {
        currentHistoryIndex = -1;
        return;
    }

    std::vector<int> path;
    int node = nodeIndex;

    while (node >= 0) {
        path.push_back(node);
        node = historyTree[node].parent;
    }

    std::reverse(path.begin(), path.end());

    for (int index : path)
        moveHistory.push_back(historyTree[index].move);

    currentHistoryIndex = static_cast<int>(moveHistory.size()) - 1;

    const float spacing = 70.f;
    const float visibleHeight = sidePanelHeight - 90.f;

    int maxDepth = -1;

    for (int i = 0; i < static_cast<int>(historyTree.size()); i++) {
        maxDepth = std::max(maxDepth, getHistoryDepth(i));
    }

    const float contentHeight = (maxDepth + 1) * spacing;
    const float maxScroll = std::max(0.f, contentHeight - visibleHeight);

    if (historyScroll > maxScroll)
        historyScroll = maxScroll;
}

void ChessUI::selectPiece(int row, int col) {
    pieceSelected = true;
    selectedRow = row;
    selectedCol = col;

    legalMoves.clear();

    if (!showPossibleMoves)
        return;

    for (int newRow = 0; newRow < 8; newRow++) {
        for (int newCol = 0; newCol < 8; newCol++) {
            if (game.valid_move(selectedRow, selectedCol, newRow, newCol)) {
                legalMoves.push_back({newRow, newCol});
            }
        }
    }
}

void ChessUI::clearSelection() {
    pieceSelected = false;
    selectedRow = -1;
    selectedCol = -1;

    legalMoves.clear();
}

void ChessUI::startGame(int engineSide, GameMode mode) {
    game = Board();
    startingBoard = game;

    this->engineSide = engineSide;
    gameMode = mode;
    gameStarted = true;
    currentScreen = Screen::Game;

    moveHistory.clear();
    historyTree.clear();
    currentHistoryNode = -1;

    legalMoves.clear();
    clearSelection();

    currentHistoryIndex = -1;
    historyScroll = 0.f;
    historyHorizontalScroll = 0.f;
    hoveredHistoryNode = -1;

    dragging = false;
    draggedRow = -1;
    draggedCol = -1;

    moveTimeLeft = matchSeconds;
    clockKeyNode = -2;
    clockKeyTurn = -1;
    clockKeyMovesLeft = -1;
    timeExpired = false;
    timeoutLoser = -1;
    clockTimer.restart();

    engineStalled = false;
    historyPaused = false;
    engineMoveWaiting = true;
    engineMoveClock.restart();
    engineMoveAnimating = false;
    animatedPiece = 0;

    promotionFromRow = -1;
    promotionFromCol = -1;
    promotionToRow = -1;
    promotionToCol = -1;
}
