#pragma once
#include "../board.hpp"
#include <SFML/Graphics.hpp>
#include <SFML/Audio.hpp>
#include <map>
#include <utility>
#include <vector>
#include <string>
#include <optional>
#include <functional>

struct MoveHistoryEntry {
    int piece;

    int fromRow;
    int fromCol;

    int toRow;
    int toCol;

    int capturedPiece;

    int promotionPiece = 0;

    Board boardAfterMove;
};

class ChessUI {

public:
    struct MatchSummary {
        std::string white;
        std::string black;
        std::string result;
        std::string date;
    };

    using AuthHandler = std::function<bool(
        const std::string&, const std::string&, bool)>;
    using MatchHistoryProvider = std::function<std::vector<MatchSummary>()>;

    explicit ChessUI(int engineSide = 0, int searchDepth = 2);
    void run();

    void setAuthHandler(AuthHandler handler) { authHandler = std::move(handler); }
    void setLogoutHandler(std::function<void()> handler) { logoutHandler = std::move(handler); }
    void setMatchHistory(std::vector<MatchSummary> matches) {
        matchSummaries = std::move(matches);
        matchHistoryMessage.clear();
        matchHistoryScroll = 0.f;
    }
    void setMatchHistoryProvider(MatchHistoryProvider provider) {
        matchHistoryProvider = std::move(provider);
        matchHistoryMessage.clear();
        matchHistoryScroll = 0.f;
    }

private:
    Board game;
    sf::RenderWindow window;

    enum class Screen {
        StartScreen, MainMenu, ChooseSide, Game, Promotion,
        Settings, EndGame, Account, MatchHistory
    };

    enum class GameMode {
        PlayerVsPlayer,
        PlayerVsEngine,
        EngineVsEngine
    };

    enum class HistoryStyle {
        Pictogram,
        Algebraic
    };

    struct HistoryNode {
        MoveHistoryEntry move;
        int parent = -1;
        std::vector<int> children;
    };

    Screen currentScreen = Screen::StartScreen;
    Screen previousScreen = Screen::MainMenu;
    Screen accountReturnScreen = Screen::MainMenu;
    Screen matchHistoryReturnScreen = Screen::MainMenu;
    GameMode gameMode = GameMode::PlayerVsPlayer;
    HistoryStyle historyStyle = HistoryStyle::Pictogram;

    sf::Clock engineMoveClock;
    sf::Clock engineAnimationClock;
    bool engineMoveWaiting = false;
    bool engineMoveAnimating = false;
    int animatedFromRow = -1;
    int animatedFromCol = -1;
    int animatedToRow = -1;
    int animatedToCol = -1;
    int animatedPiece = 0;
    bool slidePieces = true;
    bool fullscreen = false;
    const float engineMoveDelaySeconds = 0.5f;
    const float engineSlideSeconds = 0.3f;

    int promotionFromRow = -1;
    int promotionFromCol = -1;
    int promotionToRow = -1;
    int promotionToCol = -1;

    bool dragPieces = true;
    bool showPossibleMoves = true;
    bool pieceSelected = false;
    int selectedRow = -1;
    int selectedCol = -1;

    std::vector<HistoryNode> historyTree;
    int currentHistoryNode = -1;
    int hoveredHistoryNode = -1;
    float historyHorizontalScroll = 0.f;

    int pieceTheme = 0;
    int settingsTab = 0;
    bool gameStarted = false;

    bool manualBoardFlip = false;
    bool showCoordinates = true;
    bool soundEffects = true;
    bool historyPaused = false;
    bool accountSignUpMode = false;
    bool accountPasswordField = false;
    bool accountSignedIn = false;
    bool profilePicturePickerOpen = false;
    std::string accountName;
    std::string accountInput;
    std::string accountPassword;
    std::string accountMessage;
    std::string profilePicturePath;
    std::vector<std::string> profilePictureOptions;
    std::vector<sf::Texture> profilePictureOptionTextures;
    float profilePicturePickerScroll = 0.f;
    sf::Texture profilePictureTexture;
    AuthHandler authHandler;
    std::function<void()> logoutHandler;
    std::vector<MatchSummary> matchSummaries;
    std::string matchHistoryMessage;
    MatchHistoryProvider matchHistoryProvider;
    float matchHistoryScroll = 0.f;
    sf::View logicalView;

    std::vector<std::string> pieceThemeFolders;
    std::vector<sf::Texture> pieceThemeKings;

    int boardTheme = 0;
    std::vector<std::string> boardThemeFolders;
    std::vector<sf::Texture> boardWhitePreviews;
    std::vector<sf::Texture> boardBlackPreviews;
    sf::Texture whiteTileTexture;
    sf::Texture blackTileTexture;

    sf::Texture startBackgroundTexture;
    sf::Texture selectionFrameTexture;
    sf::Clock selectionFrameClock;
    int selectionFrame = 1;
    const int selectionFrameCount = 96;

    sf::Font font;
    std::map<int, sf::Texture> pieceTextures;
    std::vector<std::pair<int, int>> legalMoves;
    std::vector<MoveHistoryEntry> moveHistory;
    float historyScroll = 0.f;
    bool dragging = false;
    int draggedRow = -1;
    int draggedCol = -1;
    int engineSearchDepth;
    bool engineStalled = false;
    Board startingBoard;
    int currentHistoryIndex = -1;

    sf::Vector2f mousePosition;

    sf::SoundBuffer moveSoundBuffer;
    sf::SoundBuffer captureSoundBuffer;
    sf::SoundBuffer uiClickSoundBuffer;
    sf::SoundBuffer victorySoundBuffer;
    std::optional<sf::Sound> activeSound;

    const int windowWidth = 1200;
    const int windowHeight = 800;

    const int boardSize = 640;
    const int squareSize = boardSize / 8;

    const int boardX = 50;
    const int boardY = 80;

    const int sidePanelX = 740;
    const int sidePanelY = 80;
    const int sidePanelWidth = 400;
    const int sidePanelHeight = 640;

    void drawHistoryPreview();
    void drawMoveHistory();
    void drawLegalMoves();
    void loadPieceTextures();
    void handleEvents();
    void updateEngine();
    bool applyMove(int fromRow, int fromCol, int toRow, int toCol, int promotionPiece = 0);
    void updateResponsiveView(unsigned int width, unsigned int height);
    void toggleFullscreen();
    void drawLastMoveHighlights();
    void drawProfileButton();
    void drawAccountScreen();
    void drawProfilePicturePicker();
    void drawMatchHistoryScreen();
    void submitAccountForm();
    void loadProfilePictureOptions();
    void draw();
    void drawPieces();
    void drawBoard();
    void drawSidePanel();
    void drawText();
    void drawStartScreen();
    void drawMainMenu();
    void updateSelectionVideo();
    void drawChooseSide();
    void drawSettings();
    void drawGameplaySettings();
    void drawDesignSettings();
    void drawEndScreen();
    void drawPromotionDialog();
    void drawCapturedPieces();
    void drawCoordinates();
    void refreshPieceThemes();
    void refreshBoardThemes();
    void loadBoardTextures();
    void drawCoverTexture(const sf::Texture& texture);
    bool isBoardFlipped() const;
    int displayRow(int row) const;
    int displayCol(int col) const;
    std::vector<sf::Vector2f> getHistoryNodePositions() const;
    int getHistoryDepth(int nodeIndex) const;
    void drawMoveArrow(const MoveHistoryEntry& move);
    void drawBoardPosition(Board& board);
    std::string moveToText(const MoveHistoryEntry& move) const;
    void rebuildLinearHistoryFromNode(int nodeIndex);
    void selectPiece(int row, int col);
    void clearSelection();
    void startGame(int engineSide, GameMode mode);
    void playSound(const sf::SoundBuffer& buffer);
};
