#pragma once
// Tekken 6 Native Online launcher (Xbox 360 port, ReXGlue).
//
// Modes (tabs of the top bar): RANKED (queue by Elo, rated), CASUAL (quick match, not rated), LOBBY (public list,
// private lobbies with a code). The launcher always talks to our lobby server (server/t6_lobby_server.py; address
// kServer, T6_LOBBY_SERVER overrides it for development). In a lobby both press Ready; the server starts the match,
// the launchers punch a UDP path through the NATs (rendezvous on the server's UDP port, then direct packets from the
// game's port) and start tekken6.exe with the netplay environment of src/t6_netplay.cpp:
//   T6_NET=1, T6_NET_HOLD=1, T6_NET_STATUS (state file the game writes, 'state error <text>' = no match), T6_NET_CMD (start / quit),
//   T6_PLAY (menu path tools\menu_vs.bin), T6_FIX_SEED.
// The game's window is visible only from the loading screen of a match to its win pose: the parked game, the stage
// select it leaves after 'start' and the result menu (REMATCH / CHARACTER SELECT) stay hidden, the launcher shows those.
// The game is started together with the launcher (hidden window): it runs through the menus and parks before the fight
// loading ('state ready'). The match itself is one line in T6_NET_CMD:
//   start player=N port=P remote=IP:PORT delay=D seed=S char1=K char2=K stage=N rounds=3 input=live
// Rematch: both press it in the result menu -> the server starts a new match in the lobby -> 'rematch' in T6_NET_CMD,
// the running session goes on (same characters and stage). Without a rematch (or after a disconnect) the game is
// closed and the next one is started in the background.
// Same boot state on both sides: the game never runs on the player's own save. Before every start the reference profile
// (profile/, development: user/) is copied into a clean runs/online_N/user; the hash of the game files + that profile
// goes to the server ('build' of hello), which pairs only equal builds. The state hash the game reports at the hold
// point ('statehash <hex>' of the status file) goes with 'ready'; the server refuses a match of different states.
// T6_LAUNCHER_SHOW_GAME=1: the parked game's window is not hidden; T6_LAUNCHER_NOFOCUS=1 (tests): the game's window is
// shown without taking the focus.
// Account: the player registers / logs in with a name and a password (login page; the server keeps Elo, the rank
// that follows from it and the launcher settings: character, delay, keyboard). The launcher stores only the session token
// of the server (launcher_native.ini) and logs in with it at the next start. With "tls://host:port" as the server
// address the connection is TLS and only the pinned certificate launcher/tls/t6_cert.pem is accepted; a plain address
// is for development (the server takes passwords without TLS from the loopback address only).
// Profile (top right): account, character, input delay.
// --auto ...: without a stored session the launcher plays as a guest (server --guests; tests).
// --no-game: the game is not started (tests of the launcher's own pages).
// The package has NO code or data of the game: the player builds tekken6.exe from his own Xbox 360 copy (ISO or extracted
// folder) in the profile ("Game files"): builder\python\python.exe -u -X utf8 builder\build_native.py <path> prints
// 'STEP n/N text', 'PROGRESS 0-100', at the end 'DONE <exe>' or 'ERROR <text>' (log native\build.log, exit 0/1);
// '--check <path>' only checks the copy ('OK <title> <version>' / 'ERROR <text>'). Output: native\tekken6.exe (+ dlls),
// the game data in native\game. builder\build.version changes with the game code: the launcher then asks for a rebuild.
// Update: latest GitHub release (kReleaseApi, T6_UPDATE_API for local tests) with the patch zip kPatchAsset, checked
// against GitHub's SHA-256 digest; game\, native\, runs\, user\ and the settings files are never replaced.
// --update-test: install the update found, result in update_result.txt, no dialogs. --build-from <path>: build
// the game from that copy at start (tests).
// --instance 2: second launcher on the same PC (own settings file, user2 folder, game port + 2) for local tests.
// --auto quick: joins the quick match queue as soon as it is connected (end-to-end tests without clicks).

#include "characters.h"
#include "common.h"
#include "controls.h"
#include "files.h"
#include "http.h"
#include "processes.h"
#include "server.h"
#include "style.h"
#include "system.h"
#include "upnp.h"
#include "versions.h"
#include "widgets.h"

class Launcher : public QMainWindow
{
public:
	Launcher();
	~Launcher() override;

	void shutdown();

	void startUpnp();
	QString upnpLine() const;

	bool behindCgnat() const;

private:
	enum Page { PageRanked, PageMatch, PageLobby, PageVs, PageProfile, PageLogin, PageBoard, PagePractice, PageSettings };

	void build();
	void selectMode(Page p);

	QWidget* buildMode(bool ranked);
	QWidget* buildRanked();
	QWidget* buildMatch();

	void runSync();
	void setChar(int i);

	QCheckBox* strictToggle();
	void setSide(int side);
	void setRegion(const QString& reg);
	void setMaxPing(int ms);

	QPushButton* refreshButton(const std::function<void()>& fn);
	QWidget* buildBoard();

	QWidget* buildLobby();
	QWidget* buildVs();

	QFrame* tabbedPage(const QString& title, const QStringList& names, int* current, QList<QPushButton*>& tabBtns,
	                   QList<QVBoxLayout*>& pages, bool logout, const std::function<void(int)>& onTab = {});

	QCheckBox* gameToggle(bool* value, const char* key);

	QComboBox* gameChoice(const QList<std::pair<QString, QVariant>>& items, int* value, const char* key);

	QWidget* buildProfile();

	QWidget* buildSettings();
	QWidget* buildLogin();
	void sendLogin(const char* kind);

	QString rankName() const;

	void syncSettings();
	void applySettings(const QJsonObject& st);

	bool gameBuilt() const;
	bool rebuildNeeded() const;

	bool gameUsable() const;

	bool autoUpdateGame();
	void refreshGameFiles();
	QString builderPython() const;
	void chooseCopy();

	void startBuild(const QString& path);
	void runBuilder(const QStringList& args);

	static void builderLog(const QString& line);
	void onBuildLine(const QString& line);
	void buildFinished(int code, QProcess::ExitStatus st);

	void refreshBuildHash();

	void checkForUpdate();
	void showUpdate();

	void showPatchNotes();

	void installUpdate();

	static QString assetDigest(const std::wstring& api, const QString& url);

	void rebuildUi();

	void notify(QMessageBox::Icon icon, const QString& text);
	void finishUpdate(const QByteArray& zip, QString error);
	void showPage(Page p);

	void requestLobbies(bool force = false);
	void refreshAll();
	void requestBoard();
	void refreshBoard();
	void refreshList();
	void refreshVs();
	void readyAction();

	void parkGame();
	void leaveAction();

	void connectServer();
	void serverLost();
	void send(const QJsonObject& o);

	void sendLog();

	QJsonObject profileMsg() const;
	QJsonObject helloMsg() const;
	void sendProfile();
	void onMessage(const QJsonObject& m);

	int matchDelay() const;
	void setReady();
	void setMatchText(const QString& result, const QString& detail, bool err = false);

	void startMatch(const QJsonObject& m);
	void punchTick();

	void startProbe(const QJsonObject& m);
	void probeTick();

	void closeProbe();
	void onPeer(const QJsonObject& m);
	void onUdp();

	void endRematchStart();
	void closeUdp();
	QString natText() const;
	void punchFailed(const QString& why);

	void autoQuick();

	void appendGameLog(const QString& line);
	void killGame(const char* why);
	void endOpponentGone();

	QString historyKey() const;
	void addHistory(int winner);

	QHBoxLayout* historyRow(const QJsonObject& e);

	void showHistoryOf(const QString& name, const QJsonArray& list);
	void refreshBlocked();
	void refreshHistory();

	void buildControls(QVBoxLayout* setup, QVBoxLayout* v);

	// label + field pairs, three per row (KEYBOARD MAPPING Custom)
	static constexpr int kPairsPerRow = 3;
	static QGridLayout* pairGrid();
	static void addPair(QGridLayout* g, int n, const QString& label, QWidget* field);
	QString padBackend() const;
	static QString mmBinding(const QString& key, const QString& def);

	void captureMm(QPushButton* b, const QString& key, const QString& def, int dev);

	void captureKey(QPushButton* b, const QString& key, int def);

	QString writeMmConfig() const;

	QString padMap() const;

	QStringList inputArgs() const;

	void controlsChanged();
	void applyControls();

	QWidget* buildPractice();

	static void testLog(const QString& line);

	// ================= Rematch from the controller (user 2026-10-13): after a match the player still holds the pad,
	// the mouse is far. While the result shows and Rematch / Next game can be pressed, the confirm button of the
	// controller presses it: A of an XInput pad; the button set as "A / Cross" on the joystick / arcade stick (CONTROLS);
	// button 2 (Cross) of another pad when no XInput pad is there. It counts only after 0.7 s of the result and after
	// the button was up once - the presses of the fight's last moment must not ask for a rematch.
	// cancel (user 2026-10-16): B of an XInput pad, the button set as "B / Circle" on the joystick, button 3 (Circle) of
	// another pad - cancels a search / declines a found match
	// stage (user 2026-10-16): Y / Triangle = "Rematch / Next game – random stage" of the loser
	enum PadKind { PadConfirm, PadCancel, PadStage };
	static bool padConfirmDown();
	static bool padCancelDown();
	static bool padStageDown();
	static bool padDown(PadKind kind);

	void padPoll();

	bool preparing() const;
	QPushButton* practiceButton(int search);
	void refreshPractice();
	void startPractice();

	void pollPractice();

	void practiceSearch();
	void stopPractice();

	int gpuIndex() const;
	void refreshGpu();

	void restartParkedGame();

	static QProcessEnvironment gameEnvironment();

	void addGameEnv(QProcessEnvironment& env, bool lookAndSound) const;

	QStringList gameArgs(const QString& user, const QString& runDir) const;
	void prelaunch();
	static bool showParked();

	void toFront();

	void showGameWindows(bool show);

	static void makeFullscreen(HWND w);
	static void showWindowsOf(DWORD gamePid, bool show, bool fullscreen);

	void gameReady();

	void launchGame();
	void sendStart();

	void pollStatus();

	void sendPlaying();
	void command(const char* c);

	// ================= state
	QJsonObject m_costumes;  // kChars index -> 1 / 2 when the P2 / P3 costume is chosen (absent = P1)
	QComboBox* m_costumeBox = nullptr;
	QLabel* m_costumeNote = nullptr;
	int costume(int index) const;
	void refreshCostume();

	// ================= custom look (the game's Customize menu)
	QJsonObject m_customs;  // "<kChars index>_<costume>" -> the 80-byte record as 160 hex digits
	QJsonObject m_customOff;  // the same keys -> true: the look is switched off (Normal look), kept for "Use my custom look"
	QPushButton *m_customizeBtn = nullptr, *m_customResetBtn = nullptr;
	QLabel* m_customNote = nullptr;
	QProcess* m_customize = nullptr;
	QTimer m_customizePoll;
	QString m_customizeOut;
	bool m_customizeShown = false;
	int m_customizeChar = -1;

	QString customRecord(int index, int k) const;
	QString storedRecord(int index, int k) const;

	static bool validRecord(const QString& rec);

	void startCustomize();

	void finishCustomize();
	int m_char = 15, m_delay = 1, m_keys = 1, m_gamePort = 7000, m_role = 0, m_elo = 0, m_games = 0, m_wins = 0;
	QString m_name, m_clientId, m_statusFile, m_cmdFile, m_resultText, m_matchDetail;
	// account: m_connected = the server answered, m_authed = logged in (m_online then), m_session = the server's login token
	QString m_session, m_ratedText;
	bool m_connected = false, m_authed = false, m_guest = false, m_playingSent = false, m_queuedRanked = false;
	QLabel *m_rankedInfo = nullptr, *m_chipElo = nullptr;
	QPushButton* m_rankedBtn = nullptr;
	// ping between the two launchers (punch phase), max ping of the matchmaking (0 = any), leaderboard
	int m_pingMs = -1, m_pingCount = 0, m_maxPing = 150, m_boardPos = 0;
	int m_side = 0;           // preferred side: 0 any, 1, 2
	int m_settingsTab = 0;    // open tab of the settings page
	int m_profileTab = 0;     // open tab of the profile page
	QString m_region;         // EU / USA / SA / ME / SEA / ASIA / OCE / AF
	bool m_regionLock = false; // matchmaking never outside the region
	bool m_strict = false;     // Strict matchmaking: no ranked opponent more than 150 MMR apart (server STRICT_RANGE)
	bool m_pingReported = false, m_setRunning = false;
	QElapsedTimer m_pingClock;
	QJsonArray m_board;
	QVBoxLayout* m_boardBox = nullptr;
	QCheckBox* m_rankedLobby = nullptr;
	QLabel* m_rankIcon = nullptr;
	// game files (build from the player's copy) and the update
	QProcess* m_builder = nullptr;
	QString m_buildStep, m_buildError, m_buildPath, m_buildDone;
	bool m_buildChecking = false, m_updating = false, m_autoBuild = false, m_autoBuildFailed = false;
	QLabel* m_gameState = nullptr;
	// graphics adapter: chosen (name, empty = automatic) and the one the running game logged
	QString m_gpu, m_gpuInUse, m_gameLog;
	qint64 m_gameLogFrom = 0;
	QLabel* m_gpuLabel = nullptr;
	bool m_restartGame = false;
	QProcess* m_practice = nullptr;
	QString m_practiceStatus;
	QTimer m_practicePoll;
	QWidget* m_loginForm = nullptr;  // login page: the form (hidden while a stored login signs in by itself)
	QLabel *m_loginWait = nullptr, *m_loginWaitSub = nullptr;
	QElapsedTimer m_autoLeave;    // a decided set / an opponent who left: until the launcher leaves the lobby by itself
	QElapsedTimer m_searchAgain;  // back on the mode's page after a match: the controller may press Search
	int m_padTarget = 0;          // what the controller's confirm button presses now (padPoll)
	bool m_oppLeft = false;       // the opponent of the match on the result page left the lobby
	QElapsedTimer m_prepSince;  // since when the game is starting (shown in the buttons)
	qint64 m_prepShown = -1;
	QLabel* m_rankedPrep = nullptr;
	QTimer m_padTimer;  // controller's confirm button on the result page (padPoll)
	QElapsedTimer m_padSince;
	bool m_padArmed = false;
	int m_padCancelTarget = 0;  // what the controller's cancel button presses now (padPoll)
	QElapsedTimer m_padCancelSince;
	bool m_padCancelArmed = false;
	bool m_padStageOn = false;  // the Y button presses the random stage rematch now (padPoll)
	QElapsedTimer m_padStageSince;
	bool m_padStageArmed = false;
	bool m_practiceShown = false;
	int m_practiceSearch = 0;
	bool m_practiceSolo = false;  // started from the PRACTICE page: its own two characters, no search
	int m_soloChar = -1, m_soloOpp = -1;  // kChars indexes of the PRACTICE page
	int m_soloStage = -1;  // stage id of the PRACTICE page, -1 random
	QPushButton* m_soloBtn = nullptr;
	QList<QPushButton*> m_practiceLobbyBtns;  // search to start once the practice is up: 0 none, 1 player match, 2 ranked
	QTimer* m_capture = nullptr;
	bool m_oppGone = false;
	bool m_blur = false, m_vibration = false;  // game options of this PC (T6_NO_BLUR / T6_NO_VIBRATION)
	bool m_music = true;  // stage music (T6_NO_MUSIC when off)
	int m_matchSound = 1;  // when a match is found (+ taskbar flash): 0 off, 1 built-in gong, 2 the player's own file
	QString m_matchSoundFile;
	void playMatchSound();
	bool m_udpBound = false, m_goPending = false;  // rematch: punch while the game parks
	bool m_rematchPark = false, m_rematchFlow = false, m_randomStage = false, m_lostGame = false;
	bool m_controlsDirty = false;
	bool m_onlyOpen = false;  // lobby list filter
	bool m_allRegions = true;  // lobby list filter: off = hosts of m_region only
	bool m_showPrivate = false;  // lobby list filter: private lobbies asked for and shown
	int m_onlineCount = 0;  // players online (server 'online' / account), 0 = not known
	KeyGrab* m_keyCapture = nullptr;
	bool m_setOver = false;
	bool m_lagFree = false;  // the last game lagged (lag_warn): leaving costs nothing until the next game starts
	bool m_slowEnd = false;  // the set ended because a game ran below 60 FPS (slow_end)
	bool m_confirming = false, m_accepted = false, m_oppAccepted = false, m_hideOpp = false;  // matchmaking acceptance
	int m_confirmPing = 0;
	qint64 m_confirmUntil = 0;
	QElapsedTimer m_confirmClock;
	QTimer* m_confirmTimer = nullptr;
	// ranked: seconds left to press Next game (the server's next_seconds), -1 = no countdown
	int m_nextSeconds = 0;
	QElapsedTimer m_nextClock;
	QTimer* m_nextTimer = nullptr;
	int nextLeft() const;
	QString m_failNote;    // why this side's connection failed (punchFailed), until the server's 'cancel' of that match
	QString m_searchNote;  // shown on the search pages after a declined / timed out match   // ranked set decided: the lobby is closed, "Back to ranked" instead of Rematch
	QVBoxLayout* m_historyBox = nullptr;  // the game parks for a rematch; the match start goes on when it is ready
	QString m_boardRegion = "ALL";
	bool m_fullscreen = true;
	bool m_borderless = false;  // with m_fullscreen: frames shown whole, in step with the display (no tearing)

	QLabel* m_displayNote = nullptr;

	// the bottom bar notice comes from the server (2.2.2, user 2026-10-16: "behem 20 minut probehne restart serveru",
	// switched on / off there without a release; it replaced the Borderless window notice of 2.1.5): text + optional
	// countdown to 'at' (epoch seconds, 0 = none)
	QString m_noticeText;
	qint64 m_noticeAt = 0;

	void applyNotice(const QJsonObject& n);
	void showDisplayNote();
	int displayMode() const;

	QString tearingArg() const;

	static bool underWine();
	static QString renderTargetArg();

	static bool onWifi(const QHostAddress& to);
	int m_resScale = 1;  // draw resolution: 1 = 1280x720, 2 = 2560x1440, 3 = 3840x2160
	int m_render3d = 0;  // the game's own 3D picture: 0 = 1024 x 576, 1 = 768 x 432, 2 = 640 x 360 (t6_resolution.cpp)
	bool m_smallShadows = false;  // offscreen target 512 x 512 instead of 1024 x 1024
	QList<QPushButton*> m_practiceBtns;
	QPushButton *m_buildBtn = nullptr, *m_updateBtn = nullptr;
	QString m_patchNotes;  // loaded once per launcher start
	QString m_killReason;  // why the launcher killed the game (empty: the game ended by itself)
	QLabel *m_logLabel = nullptr, *m_boardCount = nullptr, *m_stageLabel = nullptr, *m_boardUpdated = nullptr, *m_lobbyUpdated = nullptr;
	QElapsedTimer m_lobbyAsked;  // the last lobby list request (requestLobbies)
	bool m_admin = false;  // the server says this account is one of its admins
#ifdef T6_DEV
	QLabel* m_statsLabel = nullptr;
	QTimer m_statsTimer;
	QElapsedTimer m_statsAt;
#endif
	QComboBox* m_stageBox = nullptr;
	QProgressBar* m_buildBar = nullptr;
	QString m_updateTag, m_updateUrl, m_updateSha, m_updateStatus;
	std::wstring m_updateApi;  // the release api the update was found with (a second look at the digest)
	QTimer m_updateTimer;
	int m_rank = 15;          // index into m_rankNames
	QJsonObject m_chars;      // "<character index>" -> {wins, losses}
	QJsonArray m_rankNames;
	QLineEdit *m_loginName = nullptr, *m_loginPass = nullptr;
	QLabel *m_loginErr = nullptr, *m_profileStats = nullptr;
	QPushButton *m_loginBtn = nullptr, *m_registerBtn = nullptr;
	bool m_autoQuickDone = false, m_parking = false;
	QElapsedTimer m_parkTimer;
	bool m_online = false, m_inLobby = false, m_queued = false, m_ready = false, m_punching = false, m_peerFound = false,
	     m_heardPeer = false, m_resultShown = false, m_reported = false, m_matchErr = false, m_reconnect = false, m_rematch = false,
	     m_hadResult = false, m_inMatch = false, m_gameReady = false, m_pendingStart = false, m_started = false, m_startFailed = false,
	     m_bootError = false, m_closing = false;
	bool m_rematchWanted = false, m_awaitRematch = false, m_disconnected = false, m_showGame = false;
	bool m_wifi = false, m_peerWifi = false, m_peerLinux = false;  // sent / heard in the punch 'hi' (warning on the match card)
	// Whose connection failed (user 2026-10-14: "prej to obou hracum pise ... nat nebo firewall ale rikaji ze to maji
	// povoleny"): both sides always time out together, so the message has to say which side it is - from what the
	// launcher already knows, no extra port: the public port the server saw this side's punch from ('seen'; none = UDP
	// to the server blocked) against the local game port, and the opponent's public / local port from 'peer' (no
	// 'peer' = the opponent's punch never reached the server). A router that changes the port (strict NAT: mobile,
	// CGNAT, two routers) is the usual reason a punch cannot work; ports kept on both sides = most likely a firewall.
	int m_natPort[1] = {-1};
	QString m_natIp;             // this side's public address as the server saw the punch
	Upnp m_upnp;                 // (set on the UI thread from the UPnP worker's result, then m_upnpDone)
	std::atomic<bool> m_upnpDone{false};
	std::shared_ptr<std::atomic<bool>> m_upnpFinished;  // (the worker is done: the destructor may join it)
	std::thread m_upnpThread;
	bool m_rebuildPending = false;  // (rebuildUi waits for a modal dialog)
	int m_peerPubPort = -1, m_peerLocPort = -1;
	int m_bootFails = 0, m_startSends = 0;
	QString m_startLine, m_startError, m_build, m_stateHash, m_heapHash;
	QElapsedTimer m_startTimer;
	int m_punchTicks = 0, m_afterHeard = 0;
	Page m_lastMode = PageMatch;
	QJsonArray m_lobbies;
	QJsonObject m_lobby, m_match;
	QTcpSocket* m_tcp = nullptr;
	QUdpSocket* m_udp = nullptr;
	// lobby ping probe
	QUdpSocket* m_probeUdp = nullptr;
	QTimer m_probeTimer;
	QElapsedTimer m_probeClock;
	QString m_probeId, m_probeToken;
	QHostAddress m_probeServer, m_probePeer;
	quint16 m_probePort = 7701, m_probePeerPort = 0;
	std::vector<std::pair<QHostAddress, quint16>> m_probeCands;
	bool m_probeHeard = false;
	int m_probeRole = 0, m_probeCount = 0, m_probeTicks = 0, m_lobbyPing = -1;  // -1 none, -2 measuring, -3 failed
	QHostAddress m_rendezvous, m_peerAddr;
	quint16 m_peerPort = 0;
	std::vector<std::pair<QHostAddress, quint16>> m_candidates;
	QProcess* m_game = nullptr;
	QTimer m_punchTimer, m_poll;
	QStackedWidget* m_pages = nullptr;
	QWidget* m_content = nullptr;  // the area under the top bar (pages + bottom bar)

	// ================= poll panel (user 2026-10-15: "Do you want to keep rage?", Yes / No; accounts with at least 10
	// games played, one vote; a small panel in the corner of the main pages until the player voted)
	QFrame* m_pollBox = nullptr;
	QLabel* m_pollLabel = nullptr;
	QPushButton *m_pollYes = nullptr, *m_pollNo = nullptr;
	QString m_pollId, m_pollQuestion;
	bool m_pollCanVote = false, m_pollThanks = false;
	int m_pollVoted = -1;

	void buildPoll();
	void refreshPoll();

	void placePoll();

	void fitNav();

	void setNavLabels(bool on);
	bool eventFilter(QObject* o, QEvent* e) override;
	QWidget* m_bar = nullptr;  // the top bar (fitNav)
	QMap<Page, QPushButton*> m_nav;
	QPushButton* m_settingsNav = nullptr;
	QList<QPushButton*> m_profileTabs, m_settingsTabs;  // (the tab buttons of the profile / settings page)
	std::vector<std::function<void()>> m_syncers;  // (controls that show a choice, runSync; filled while the pages are built)
	QLabel *m_pillBadge = nullptr, *m_pillText = nullptr;  // (inside m_profileBtn)
	QPushButton *m_profileBtn = nullptr, *m_quickBtn = nullptr, *m_createBtn = nullptr, *m_joinBtn = nullptr, *m_readyBtn = nullptr,
	            *m_leaveBtn = nullptr, *m_randBtn = nullptr, *m_blockBtn = nullptr;
	bool m_blockArmed = false;  // Block player clicked once: the next click blocks (for 4 s)
	QElapsedTimer m_blockArmAt;
	QWidget* m_rankBanner = nullptr;
	QLabel *m_rankIconL = nullptr, *m_rankTextL = nullptr;
	int m_rankDir = 0, m_rankNew = -1;  // the last rated set: +1 promoted / -1 demoted to rank m_rankNew, 0 = no change
	QVBoxLayout* m_blockedBox = nullptr;  // Profile -> BLOCKED
	QJsonArray m_blocks;  // the server's 'blocks' list: {id, name, time}
	QLabel *m_status = nullptr, *m_matchInfo = nullptr, *m_matchSub = nullptr, *m_vsCode = nullptr, *m_vsCodeLabel = nullptr,
	       *m_vsInfo = nullptr, *m_chipChar = nullptr, *m_chipDelay = nullptr;
	QCheckBox* m_private = nullptr;
	QLineEdit* m_codeEdit = nullptr;
	QVBoxLayout* m_listBox = nullptr;
	VsPanel* m_vs = nullptr;
};
