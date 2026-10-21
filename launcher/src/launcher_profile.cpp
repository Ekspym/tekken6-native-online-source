// Tekken 6 Native Online launcher - Profile, settings and login pages, match history, blocked players.

#include "launcher.h"

// Profile (the name in the top bar) / Settings (SETTINGS): the title, Log out (profile) + Done, tabs with one page each;
// the open tab is kept in *current, onTab runs when a tab is opened
QFrame* Launcher::tabbedPage(const QString& title, const QStringList& names, int* current, QList<QPushButton*>& tabBtns,
                   QList<QVBoxLayout*>& pages, bool logout, const std::function<void(int)>& onTab)
{
	auto* c = card();
	auto* v = new QVBoxLayout(c);
	v->setContentsMargins(32, 26, 32, 28);
	v->setSpacing(14);
	auto* head = new QHBoxLayout;
	head->addWidget(text(title, "h1"));
	head->addStretch();
	if (logout && m_authed && !m_guest)
	{
		auto* out = button(T("Log out"), "ghost");
		connect(out, &QPushButton::clicked, this, [this] { send({{"t", "logout"}, {"session", m_session}}); });
		head->addWidget(out);
		head->addSpacing(10);
	}
	auto* done = button(T("Done"), "cta");
	connect(done, &QPushButton::clicked, this, [this] { selectMode(m_lastMode); });
	head->addWidget(done);
	v->addLayout(head);
	auto* tabs = new QHBoxLayout;
	tabs->setSpacing(6);
	auto* tabGroup = new QButtonGroup(c);
	auto* stack = new QStackedWidget;
	tabBtns.clear();
	pages.clear();
	*current = qBound(0, *current, int(names.size()) - 1);
	for (int i = 0; i < names.size(); i++)
	{
		auto* b = button(names[i], "tab");
		tabBtns.push_back(b);
		b->setCheckable(true);
		b->setChecked(i == *current);
		tabGroup->addButton(b);
		connect(b, &QPushButton::clicked, this, [stack, i, current, onTab] {
			*current = i;
			stack->setCurrentIndex(i);
			if (onTab)
				onTab(i);
		});
		tabs->addWidget(b);
		auto* page = new QWidget;
		auto* pv = new QVBoxLayout(page);
		pv->setContentsMargins(0, 10, 0, 0);
		pv->setSpacing(14);
		pages.push_back(pv);
		stack->addWidget(page);
	}
	tabs->addStretch();
	v->addLayout(tabs);
	auto* line = new QFrame;
	line->setFixedHeight(1);
	line->setStyleSheet("background: #242430;");
	v->addWidget(line);
	v->addWidget(stack, 1);
	stack->setCurrentIndex(*current);
	return c;
}

// a Sketch Toggle for a bool of this PC that the game reads at its start
QCheckBox* Launcher::gameToggle(bool* value, const char* key)
{
	auto* t = toggle(*value);
	connect(t, &QCheckBox::toggled, this, [this, value, key](bool on) {
		*value = on;
		settings().setValue(key, on);
		controlsChanged();  // (read by the game at its start: parked game / practice start again)
	});
	return t;
}

// a Sketch Dropdown for an int of this PC that the game reads at its start
QComboBox* Launcher::gameChoice(const QList<std::pair<QString, QVariant>>& items, int* value, const char* key)
{
	auto* box = choiceBox(items, *value);
	connect(box, &QComboBox::activated, this, [this, box, value, key] {
		const int v = box->currentData().toInt();
		if (*value == v)
			return;
		*value = v;
		settings().setValue(key, v);
		controlsChanged();
	});
	return box;
}

// the player's own things: character + costume + look, match history, blocked players
QWidget* Launcher::buildProfile()
{
	QList<QVBoxLayout*> pages;
	auto* c = tabbedPage(T("Profile"), {"PROFILE", "MATCH HISTORY", "BLOCKED"}, &m_profileTab, m_profileTabs, pages, true, [this](int i) {
		if (i == kBlockedTab && m_online)
			send({{"t", "blocks"}});
	});
	QVBoxLayout *pProfile = pages[0], *pHistory = pages[1], *pBlocked = pages[kBlockedTab];
	// BLOCKED tab (user 2026-10-16): the players blocked after a game (Block player); the server keeps the list
	pBlocked->addWidget(text(T("Players you blocked. Ranked and player match never pair you with them.")));
	m_blockedBox = new QVBoxLayout;
	m_blockedBox->setSpacing(4);
	pBlocked->addLayout(m_blockedBox);
	pBlocked->addStretch();
	refreshBlocked();
	if (m_profileTab == kBlockedTab && m_online)
		send({{"t", "blocks"}});
	// MATCH HISTORY tab: the last kHistoryMax results of this account (kept on this PC only)
	pHistory->addWidget(text(T("Your last %1 matches (kept on this PC only).").arg(kHistoryMax)));
	m_historyBox = new QVBoxLayout;
	m_historyBox->setSpacing(4);
	pHistory->addLayout(m_historyBox);
	pHistory->addStretch();
	refreshHistory();
	pProfile->addLayout(settingRow(T("Account"), {text(m_name, "h3")}));
	m_profileStats = hintText(QString());
	pProfile->addLayout(settingRow(T("Stats"), {m_profileStats}));
	auto* chars = charGrid(m_char, [this](int i) { setChar(i); });
	m_syncers.push_back([this, chars] {
		for (auto* b : chars->findChildren<QPushButton*>())
			b->setChecked(b->property("char").toInt() == m_char);
	});
	pProfile->addLayout(settingRow(T("Character"), {}));
	pProfile->addWidget(chars);
	// costume of the selected character (kept per character; refreshCostume fills the list)
	m_costumeBox = choiceBox({}, 0, 180);
	connect(m_costumeBox, &QComboBox::activated, this, [this] {
		if (const int k = m_costumeBox->currentData().toInt())
			m_costumes[QString::number(m_char)] = k;
		else
			m_costumes.remove(QString::number(m_char));
		settings().setValue("costumes", QJsonDocument(m_costumes).toJson(QJsonDocument::Compact));
		refreshCostume();
		sendProfile();
	});
	m_costumeNote = hintText(QString());
	pProfile->addLayout(settingRow(T("Costume"), {m_costumeBox, m_costumeNote}));
	// custom look of this character + costume, made in the game's own Customize menu (user 2026-10-15): the game opens
	// with the character's Customize screen, all items unlocked and 765,000,000 G; leaving Customize saves it
	if (kCustomLooks)
	{
		m_customizeBtn = button(T("Customize look"), "ghost");
		connect(m_customizeBtn, &QPushButton::clicked, this, [this] { startCustomize(); });
		// Normal look / Use my custom look: switches the look off and on again; the look itself is kept (user 2026-10-16:
		// "kdyz kliknu na normal look ... nemuzu vratit zpatky" - it was deleted, and Customize saves only a changed look)
		m_customResetBtn = button(T("Normal look"), "ghost");
		connect(m_customResetBtn, &QPushButton::clicked, this, [this] {
			const QString key = QString("%1_%2").arg(m_char).arg(costume(m_char));
			if (m_customOff.contains(key))
				m_customOff.remove(key);
			else
				m_customOff[key] = true;
			settings().setValue("custom_looks_off", QJsonDocument(m_customOff).toJson(QJsonDocument::Compact));
			refreshCostume();
			sendProfile();
		});
		m_customNote = hintText(QString());
		pProfile->addLayout(settingRow(T("Look"), {m_customizeBtn, m_customResetBtn, m_customNote}));
	}
	refreshCostume();
	pProfile->addStretch();
	return c;
}

// everything set on this PC or for the matchmaking: game picture and sound, online, controls, the game's build
QWidget* Launcher::buildSettings()
{
	QList<QVBoxLayout*> pages;
#ifdef T6_DEV
	// Developer's own launcher (user 2026-10-13: "MUSIM TO MIT JEN JA"; never packaged - build_dev, make_package.py
	// refuses it): one more tab with the server's statistics. The server only answers the accounts of its --admin.
	const QStringList names = {"GAME", "ONLINE", "CONTROLLER", "BUTTON MAPPING", "KEYBOARD MAPPING", "BUILD GAME", "STATS"};
#else
	const QStringList names = {"GAME", "ONLINE", "CONTROLLER", "BUTTON MAPPING", "KEYBOARD MAPPING", "BUILD GAME"};
#endif
	auto* c = tabbedPage(T("Settings"), names, &m_settingsTab, m_settingsTabs, pages, false);
	QVBoxLayout *pGame = pages[0], *pOnline = pages[1], *pController = pages[2], *pMapping = pages[3], *pKeyboard = pages[4], *pBuild = pages[5];
#ifdef T6_DEV
	{
		QVBoxLayout* pStats = pages[kStatsTab];
		m_statsLabel = text(T("Waiting for the server…"));
		m_statsLabel->setTextFormat(Qt::RichText);
		m_statsLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
		m_statsLabel->setAlignment(Qt::AlignTop | Qt::AlignLeft);
		auto* sc = new QScrollArea;
		sc->setWidgetResizable(true);
		sc->setWidget(m_statsLabel);
		pStats->addWidget(sc, 1);
		m_statsTimer.setInterval(5000);
		connect(&m_statsTimer, &QTimer::timeout, this, [this] {
			if (m_online)
				send({{"t", "stats"}});
			if (m_statsAt.isValid() && m_statsAt.elapsed() > 12000)
				m_statsLabel->setText(m_admin ? T("No answer from the server.")
				                              : T("No statistics: this account is not an admin of the server, or the server is older."));
		});
		m_statsTimer.start();
		m_statsAt.start();
	}
#endif

	// ---- GAME: this PC only
	// graphics adapter of the game (notebooks: integrated + dedicated GPU)
	auto* gpuBox = new QComboBox;
	gpuBox->addItem(T("Automatic (%1)").arg(autoGpu()), QString());
	for (const Gpu& g : gpuList())
		gpuBox->addItem(g.name, g.name);
	gpuBox->setCurrentIndex(qMax(0, gpuBox->findData(m_gpu)));
	styleField(gpuBox);
	connect(gpuBox, &QComboBox::currentIndexChanged, this, [this, gpuBox] {
		m_gpu = gpuBox->currentData().toString();
		settings().setValue("gpu", m_gpu);
		restartParkedGame();
	});
	m_gpuLabel = hintText(QString());
	pGame->addLayout(settingRow(T("Graphics"), {gpuBox, m_gpuLabel}));
	// How the game shows. Three ways (user 2026-10-14: some players have much worse FPS and stutter in fullscreen):
	//   Fullscreen        - the window covers the screen and every frame goes to the screen the moment it is done,
	//                       tearing allowed (the SDK's default: lowest delay, variable refresh rate displays)
	//   Borderless window - the same window, but the frames are shown whole, in step with the display, as Windows
	//                       does for a normal window (--d3d12_allow_variable_refresh_rate_and_tearing=false)
	//   Window            - a normal window
	// (the SDK's "fullscreen" IS a borderless window over the desktop - SDL3 desktop fullscreen, no mode switch)
	{
		auto* box = choiceBox({{T("Fullscreen"), 0}, {T("Borderless window"), 1}, {T("Window"), 2}}, displayMode(), 230);
		connect(box, &QComboBox::activated, this, [this, box] {
			const int mode = box->currentData().toInt();
			if (displayMode() == mode)
				return;
			m_fullscreen = mode != 2;
			m_borderless = mode == 1;
			settings().setValue("fullscreen", m_fullscreen);
			settings().setValue("borderless", m_borderless);
			showDisplayNote();
			controlsChanged();  // the game reads it at its start: parked game / practice start again
		});
		// (user 2026-10-16: players whose frames wait for a 60 Hz display in Borderless - D3D fence wait 5-11 ms - are
		//  better off in Fullscreen until the present fix of a later patch)
		pGame->addLayout(settingRow(T("Display"), {box, hintText(T("Slowdowns or uneven FPS in Borderless window (often on 60 Hz monitors)? "
		                                                         "Try Fullscreen. Tearing in Fullscreen? Try Borderless window."))}));
	}
	// Resolution the game draws in (user 2026-10-13). The game's own picture is 1280x720; the SDK can draw it 2 or 3
	// times as large in each direction (draw_resolution_scale), sharper on a big screen, at the cost of the graphics
	// card. It does not change the game's state: a 2x game against a 1x game stayed in sync (res2).
	pGame->addLayout(settingRow(T("Picture quality"),
	                            {gameChoice({{T("Standard"), 1}, {T("High"), 2}, {T("Ultra"), 3}}, &m_resScale, "res_scale"),
	                             hintText(T("Standard = original picture. High = sharp on Full HD and 1440p screens. Ultra = for 4K screens. "
	                                        "Higher needs a stronger graphics card."))}));
	// Size of the game's own 3D picture (2026-10-17, from mahmoud-mess/tekken6-recomp): smaller = less work for the
	// graphics card on a weak PC; smaller shadow map (the 1024 x 1024 offscreen target at 512) the same
	pGame->addLayout(settingRow(T("3D resolution"),
	                            {gameChoice({{T("Original"), 0}, {T("Lower"), 1}, {T("Lowest"), 2}}, &m_render3d, "render_3d"),
	                             hintText(T("For a weak graphics card: Original = 1024 x 576, Lower = 768 x 432, Lowest = 640 x 360 (softer picture)."))}));
	pGame->addLayout(settingRow(T("Smaller shadows"), {gameToggle(&m_smallShadows, "small_shadows")}));
	// the game's motion blur behind fast moves (user 2026-10-12)
	pGame->addLayout(settingRow(T("Motion blur"), {gameToggle(&m_blur, "blur")}));
	pGame->addLayout(settingRow(T("Stage music"), {gameToggle(&m_music, "stage_music")}));
	// sound when a match is found (launcher only, no restart of the game): the built-in gong, the player's own file
	// (user 2026-10-14: "moznost nahrat vlastni zvuk") or none; choosing a mode or a file plays it once
	{
		auto* box = choiceBox({{T("Gong"), 1}, {T("Own sound"), 2}, {T("Off"), 0}}, m_matchSound, 170);
		connect(box, &QComboBox::activated, this, [this, box] {
			m_matchSound = box->currentData().toInt();
			settings().setValue("match_sound_mode", m_matchSound);
			playMatchSound();
		});
		auto* fileLabel = hintText(QString());
		const auto showFile = [this, fileLabel] {
			fileLabel->setText(m_matchSoundFile.isEmpty() ? T("No own sound chosen") : QFileInfo(m_matchSoundFile).fileName());
		};
		showFile();
		auto* pick = button(T("Choose file..."), "ghost");
		connect(pick, &QPushButton::clicked, this, [this, box, showFile] {
			const QString f = QFileDialog::getOpenFileName(this, T("Match found sound"), m_matchSoundFile.isEmpty() ? QString() : QFileInfo(m_matchSoundFile).absolutePath(),
			                                               T("Sound (*.wav *.mp3 *.wma *.m4a *.aac);;All files (*)"));
			if (f.isEmpty())
				return;
			m_matchSoundFile = f;
			m_matchSound = 2;
			settings().setValue("match_sound_file", f);
			settings().setValue("match_sound_mode", 2);
			box->setCurrentIndex(box->findData(2));
			showFile();
			playMatchSound();
		});
		auto* play = button(T("Play"), "ghost");
		connect(play, &QPushButton::clicked, this, [this] { playMatchFoundSound(m_matchSound == 2 ? m_matchSoundFile : QString()); });
		pGame->addLayout(settingRow(T("Match found sound"), {box, pick, play, fileLabel}));
	}
	pGame->addStretch();

	// ---- ONLINE
	// Delay 0 (user 2026-10-12): the own character answers at once, but the opponent can never get the input in time -
	// he predicts every one of them and rolls back at every change. Player matches only: a ranked match uses 1.
	{
		auto* delayNote = hintText(QString());
		const auto note = [delayNote](int d) {
			delayNote->setText(d == 0 ? T("⚠ Delay 0: your opponent gets a rollback at every input of yours – use it on a low ping only. Ranked matches use delay 1.")
			                          : T("Frames of delay before rollback. 1 is right for most connections."));
			delayNote->setObjectName(d == 0 ? "err" : "muted");
			delayNote->style()->unpolish(delayNote);
			delayNote->style()->polish(delayNote);
		};
		auto* box = choiceBox({{"0", 0}, {"1", 1}, {"2", 2}, {"3", 3}}, m_delay, 100);
		connect(box, &QComboBox::activated, this, [this, box, note] {
			m_delay = box->currentData().toInt();
			settings().setValue("delay", m_delay);
			syncSettings();
			note(m_delay);
			refreshAll();
		});
		note(m_delay);
		pOnline->addLayout(settingRow(T("Input delay"), {box, delayNote}));
	}
	// highest ping to an opponent the matchmaking (ranked, player match) may pair this player with; measured between
	// the two launchers after the NAT punch
	{
		auto* box = choiceBox({{"60 ms", 60}, {"100 ms", 100}, {"150 ms", 150}, {"200 ms", 200}, {T("Any"), 0}}, m_maxPing, 130);
		connect(box, &QComboBox::activated, this, [this, box] { setMaxPing(box->currentData().toInt()); });
		m_syncers.push_back([this, box] {
			const QSignalBlocker b(box);
			box->setCurrentIndex(qMax(0, box->findData(m_maxPing)));
		});
		pOnline->addLayout(settingRow(T("Max ping"), {box, hintText(T("Matchmaking skips opponents with a higher ping."))}));
	}
	// region (guessed from the time zone of the PC): ranked and player match pair inside the region first and take
	// another region only after ~15 s of waiting - never with the region lock
	{
		QList<std::pair<QString, QVariant>> regions;
		for (const char* reg : {"EU", "USA", "SA", "ME", "SEA", "ASIA", "OCE", "AF"})
			regions.push_back({reg, reg});
		auto* box = choiceBox(regions, m_region, 110);
		connect(box, &QComboBox::activated, this, [this, box] { setRegion(box->currentData().toString()); });
		m_syncers.push_back([this, box] {
			const QSignalBlocker b(box);
			box->setCurrentIndex(qMax(0, box->findData(m_region)));
		});
		pOnline->addLayout(settingRow(T("Region"), {box, hintText(T("Matchmaking pairs inside your region first and takes another region after ~15 s of waiting."))}));
		auto* lock = toggle(m_regionLock);
		connect(lock, &QCheckBox::toggled, this, [this](bool on) {
			m_regionLock = on;
			settings().setValue("region_lock", on);
			sendProfile();
		});
		pOnline->addLayout(settingRow(T("Region lock"), {lock, hintText(T("My region only."))}));
	}
	// Strict matchmaking (user 2026-10-16): ranked never pairs this player with one more than 150 MMR above or below
	{
		auto* strict = strictToggle();
		pOnline->addLayout(settingRow(T("Strict matchmaking"),
		                              {strict, hintText(T("Ranked: never against a player more than 150 MMR above or below you. Finding a match can take longer."))}));
	}
	// (user 2026-10-20: "button ... Super secret option for stable connection a kdyz na to klikne tak mu to napise now
	// you cant match with wifi players") - a click turns it on / off; the server then never pairs this player with one
	// on Wi-Fi in ranked / player matches
	{
		auto* secret = button(T("Super secret option for stable connection"), "ghost");
		connect(secret, &QPushButton::clicked, this, [this] {
			m_noWifi = !m_noWifi;
			settings().setValue("no_wifi_mm", m_noWifi);
			sendProfile();
			notify(QMessageBox::Information, m_noWifi ? T("Now you can't match with Wi-Fi players.") : T("You can match with Wi-Fi players again."));
		});
		pOnline->addLayout(settingRow(T("Stable connection"), {secret}));
	}
	// preferred side (player 1 = left, player 2 = right) in player matches and lobbies for fun; ranked sides are random
	{
		auto* box = choiceBox({{T("Random"), 0}, {T("Left (P1)"), 1}, {T("Right (P2)"), 2}}, m_side, 170);
		connect(box, &QComboBox::activated, this, [this, box] { setSide(box->currentData().toInt()); });
		m_syncers.push_back([this, box] {
			const QSignalBlocker b(box);
			box->setCurrentIndex(qMax(0, box->findData(m_side)));
		});
		pOnline->addLayout(settingRow(T("Preferred side"), {box, hintText(T("Used when it does not collide with the opponent's choice. Ranked sides are random."))}));
	}
	// the game's log of this PC for the developers: the server keeps one per player (a new one replaces it) and takes
	// one per hour (user 2026-10-13)
	{
		auto* b = button(T("Send the game's log"), "ghost");
		connect(b, &QPushButton::clicked, this, [this] { sendLog(); });
		m_logLabel = hintText(T("For the developers, after a problem in a match. Once an hour."));
		pOnline->addLayout(settingRow(T("Log"), {b, m_logLabel}));
	}
	pOnline->addStretch();

	// ---- CONTROLLER + BUTTON MAPPING
	buildControls(pController, pMapping);

	// ---- KEYBOARD MAPPING: the keyboard of the game (next to the controller, only while the game's window is in front): 'keys=' of the start line
	{
		auto* keysHint = hintText(QString());
		auto* kbGrid = new QWidget;  // Custom: one key per action, set by pressing it
		const auto hint = [keysHint, kbGrid](int k) {
			keysHint->setText(k == 1   ? T("Move W A S D  ·  LP U  ·  RP I  ·  LK J  ·  RK K  ·  START Enter  ·  SELECT Backspace")
			                  : k == 2 ? T("Move arrows  ·  LP Num 4  ·  RP Num 5  ·  LK Num 1  ·  RK Num 2  ·  START Num +  ·  SELECT Num -")
			                  : k == 3 ? T("Your own keys below: click a field, then press the key.")
			                           : T("Controller only"));
			kbGrid->setVisible(k == 3);
		};
		auto* box = choiceBox({{"WASD + UIJK", 1}, {T("Arrows + numpad"), 2}, {T("Custom"), 3}, {T("Off"), 0}}, m_keys, 210);
		connect(box, &QComboBox::activated, this, [this, box, hint] {
			m_keys = box->currentData().toInt();
			settings().setValue("keys", m_keys);
			syncSettings();
			hint(m_keys);
			controlsChanged();  // (the parked game reads the keys at its start)
		});
		auto* kgv = pairGrid();
		kgv->setContentsMargins(0, 0, 0, 0);
		kbGrid->setLayout(kgv);
		int n = 0;
		for (const KbTarget& t : kKbTargets)
		{
			auto* b = keyButton(vkName(kbKey(t.key, t.def)));
			const QString key = t.key;
			const int def = t.def;
			connect(b, &QPushButton::clicked, this, [this, b, key, def] { captureKey(b, key, def); });
			addPair(kgv, n++, T(t.label), b);
		}
		pKeyboard->addLayout(settingRow(T("Keyboard"), {box, keysHint}));
		pKeyboard->addWidget(kbGrid);  // (before hint() shows it: see refreshList's region)
		hint(m_keys);
		pKeyboard->addStretch();
	}


	// ---- BUILD GAME: built on this PC from the player's own Xbox 360 copy (the package holds no game code or data)
	pBuild->addWidget(text(T("The game is built on this PC from your own Tekken 6 Xbox 360 copy (an ISO file or a folder with "
	                         "the extracted disc). The package contains no game code or data.")));
	m_gameState = hintText(QString());
	m_buildBtn = button(QString(), "ghost");
	connect(m_buildBtn, &QPushButton::clicked, this, [this] { chooseCopy(); });
	auto* logBtn = button(T("Log"), "ghost");
	connect(logBtn, &QPushButton::clicked, this, [] { QDesktopServices::openUrl(QUrl::fromLocalFile(rootFile("native/build.log"))); });
	pBuild->addLayout(settingRow(T("Game files"), {m_buildBtn, logBtn, m_gameState}));
	m_buildBar = new QProgressBar;
	m_buildBar->setRange(0, 100);
	m_buildBar->setTextVisible(false);
	m_buildBar->setFixedHeight(6);
	pBuild->addWidget(m_buildBar);
	pBuild->addStretch();
	refreshGameFiles();
	return c;
}

QWidget* Launcher::buildLogin()
{
	// Two states (user 2026-10-14: the login form showed at every start although the launcher remembers the account -
	// "bud se musi prihlasit a nebo tam to okno nema byt"): with a stored login the page only says that the launcher
	// is connecting / signing in; the form shows when the player really has to log in (no stored login, logged out,
	// the stored login was refused). refreshAll switches between them.
	auto* c = card();
	auto* outer = new QVBoxLayout(c);
	outer->setContentsMargins(32, 28, 32, 30);
	m_loginWait = text("", "h1");
	outer->addWidget(m_loginWait);
	m_loginWaitSub = text("");
	outer->addWidget(m_loginWaitSub);
	m_loginForm = new QWidget;
	outer->addWidget(m_loginForm);
	outer->addStretch();
	auto* v = new QVBoxLayout(m_loginForm);
	v->setContentsMargins(0, 0, 0, 0);
	v->setSpacing(12);
	v->addWidget(text(T("Account"), "h1"));
	v->addWidget(text(T("Log in or create a new account. Your MMR, rank and settings are kept on the server.")));
	v->addSpacing(8);
	m_loginName = new QLineEdit(settings().value("account").toString());
	m_loginName->setPlaceholderText(T("Name (3–15 characters: letters, digits, _ . -)"));
	m_loginName->setMaxLength(15);
	m_loginName->setFixedWidth(420);
	v->addWidget(m_loginName);
	m_loginPass = new QLineEdit;
	m_loginPass->setEchoMode(QLineEdit::Password);
	m_loginPass->setPlaceholderText(T("Password (at least 6 characters)"));
	m_loginPass->setMaxLength(128);
	m_loginPass->setFixedWidth(420);
	v->addWidget(m_loginPass);
	m_loginErr = text("", "err");
	v->addWidget(m_loginErr);
	auto* row = new QHBoxLayout;
	m_loginBtn = button(T("Log in"), "cta");
	m_loginBtn->setMinimumWidth(200);
	connect(m_loginBtn, &QPushButton::clicked, this, [this] { sendLogin("login"); });
	connect(m_loginPass, &QLineEdit::returnPressed, m_loginBtn, &QPushButton::click);
	row->addWidget(m_loginBtn);
	row->addSpacing(10);
	m_registerBtn = button(T("Create account"), "ghost");
	connect(m_registerBtn, &QPushButton::clicked, this, [this] { sendLogin("register"); });
	row->addWidget(m_registerBtn);
	row->addStretch();
	v->addLayout(row);
	v->addStretch();
	return c;
}

void Launcher::sendLogin(const char* kind)
{
	const QString name = m_loginName->text().trimmed(), pass = m_loginPass->text();
	if (name.isEmpty() || pass.isEmpty())
	{
		m_loginErr->setText(T("Enter a name and a password."));
		return;
	}
	m_loginErr->setText(m_connected ? QString() : T("Not connected to the server."));
	send({{"t", kind}, {"name", name}, {"password", pass}});
}

// Tekken rank of the account (the server derives it from the Elo)
QString Launcher::rankName() const
{
	return m_rankNames.at(m_rank).toString("Master");
}

// the launcher settings travel with the account
void Launcher::syncSettings()
{
	if (m_authed && !m_guest)
		send({{"t", "settings"}, {"data", QJsonObject{{"char_index", m_char}, {"costumes", m_costumes}, {"delay", m_delay}, {"keys", m_keys}, {"max_ping", m_maxPing}, {"side", m_side}, {"region", m_region}, {"region_lock", m_regionLock}, {"strict", m_strict}, {"no_wifi", m_noWifi}}}});
}

void Launcher::applySettings(const QJsonObject& st)
{
	if (st.isEmpty())
	{
		syncSettings();  // new account: it takes what this launcher has
		return;
	}
	auto s = settings();
	m_char = testChar(qBound(0, st["char_index"].toInt(m_char), kCharCount - 1));
	if (charHidden(m_char))
		m_char = 16;
	if (st.contains("costumes"))
	{
		m_costumes = st["costumes"].toObject();
		s.setValue("costumes", QJsonDocument(m_costumes).toJson(QJsonDocument::Compact));
		refreshCostume();
	}
	m_delay = qBound(0, st["delay"].toInt(m_delay), 3);
	m_keys = qBound(0, st["keys"].toInt(m_keys), 3);
	m_maxPing = qBound(0, st["max_ping"].toInt(m_maxPing), 1000);
	s.setValue("max_ping", m_maxPing);
	m_side = qBound(0, st["side"].toInt(m_side), 2);
	s.setValue("side", m_side);
	m_region = st["region"].toString(m_region);
	m_regionLock = st["region_lock"].toBool(m_regionLock);
	m_strict = st["strict"].toBool(m_strict);
	m_noWifi = st["no_wifi"].toBool(m_noWifi);
	s.setValue("no_wifi_mm", m_noWifi);
	s.setValue("region", m_region);
	s.setValue("region_lock", m_regionLock);
	s.setValue("strict_mm", m_strict);
	s.setValue("char_index", m_char);
	s.setValue("delay", m_delay);
	s.setValue("keys", m_keys);
}

// ================= MATCH HISTORY: the last kHistoryMax results of this account, kept only on this PC (launcher_native.ini,
// key history_<account>); nobody else needs them, so the server stores nothing (user 2026-10-12)
QString Launcher::historyKey() const
{
	return "history_" + (m_guest || m_name.isEmpty() ? QString("guest") : m_name.toLower());
}

void Launcher::addHistory(int winner)
{
	const int me = m_role == 2 ? 2 : 1;
	const QJsonObject mine = m_match[me == 1 ? "p1" : "p2"].toObject(), theirs = m_match[me == 1 ? "p2" : "p1"].toObject();
	QJsonArray list = QJsonDocument::fromJson(settings().value(historyKey()).toByteArray()).array();
	list.prepend(QJsonObject{{"t", QDateTime::currentSecsSinceEpoch()}, {"opp", theirs["name"].toString()},
	                         {"me", mine["char"].toInt()}, {"them", theirs["char"].toInt()},
	                         {"res", winner == 0 ? "D" : winner == me ? "W" : "L"}, {"ranked", m_match["ranked"].toBool()}});
	while (list.size() > kHistoryMax)
		list.removeLast();
	settings().setValue(historyKey(), QJsonDocument(list).toJson(QJsonDocument::Compact));
	refreshHistory();
}

// one line of a match history (own: this PC's list; another player's: from the server)
QHBoxLayout* Launcher::historyRow(const QJsonObject& e)
{
	const QString res = e["res"].toString();
	auto* row = new QHBoxLayout;
	row->setSpacing(12);
	auto* r = new QLabel(res == "W" ? T("WIN") : res == "L" ? T("LOSS") : T("DRAW"));
	r->setFixedWidth(60);
	r->setStyleSheet(QString("color:%1; font-weight:700;").arg(res == "W" ? "#3FB950" : res == "L" ? "#D33030" : "#8C8DA0"));
	row->addWidget(r);
	auto* when = text(QDateTime::fromSecsSinceEpoch(e["t"].toInteger()).toString("dd.MM. HH:mm"));
	when->setFixedWidth(100);
	row->addWidget(when);
	auto* chars = new QLabel(QString("%1  vs  %2").arg(charName(e["me"].toInt()), charName(e["them"].toInt())));
	chars->setFixedWidth(260);
	row->addWidget(chars);
	row->addWidget(text(T("vs %1").arg(e["opp"].toString())), 1);
	if (!e["ranked"].isNull())  // (server rows from before the column: unknown)
		row->addWidget(e["ranked"].toBool() ? sticker("RANKED", "stickerGold") : sticker(T("FOR FUN"), "stickerBlue"));
	return row;
}

// another player's last matches (click on a leaderboard row)
void Launcher::showHistoryOf(const QString& name, const QJsonArray& list)
{
	auto* d = new QDialog(this);
	d->setAttribute(Qt::WA_DeleteOnClose);
	d->setWindowTitle(T("Match history – %1").arg(name));
	d->resize(820, 560);
	auto* v = new QVBoxLayout(d);
	v->setContentsMargins(24, 20, 24, 20);
	v->setSpacing(10);
	v->addWidget(text(T("Match history – %1").arg(name), "gold"));
	if (list.isEmpty())
		v->addWidget(text(T("No matches yet.")));
	auto* inner = new QWidget;
	auto* box = new QVBoxLayout(inner);
	box->setContentsMargins(0, 0, 8, 0);
	box->setSpacing(10);
	for (const auto& e : list)
		box->addLayout(historyRow(e.toObject()));
	box->addStretch();
	auto* scroll = new QScrollArea;
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);
	scroll->setWidget(inner);
	v->addWidget(scroll, 1);
	addCloseButton(d, v);
	d->show();
}

void Launcher::refreshBlocked()
{
	if (!m_blockedBox)
		return;
	clearLayout(m_blockedBox);
	if (m_blocks.isEmpty())
	{
		m_blockedBox->addWidget(text(m_online ? T("Nobody is blocked.") : T("Not connected to the server.")));
		return;
	}
	for (const auto& v : m_blocks)
	{
		const QJsonObject e = v.toObject();
		auto* row = card("inner");
		auto* r = new QHBoxLayout(row);
		r->setContentsMargins(16, 8, 16, 8);
		r->setSpacing(12);
		auto* name = new QLabel(e["name"].toString());
		name->setStyleSheet("color:#E8E8F2; font-weight:600; font-size:15px;");
		r->addWidget(name, 1);
		auto* when = new QLabel(QDateTime::fromSecsSinceEpoch(static_cast<qint64>(e["time"].toDouble())).toString("yyyy-MM-dd HH:mm"));
		when->setStyleSheet("color:#8C8DA0; font-weight:600; font-size:15px;");
		r->addWidget(when);
		auto* un = button(T("Unblock"), "ghost");
		const int id = e["id"].toInt();
		connect(un, &QPushButton::clicked, this, [this, id] { send({{"t", "unblock"}, {"id", id}}); });
		r->addWidget(un);
		m_blockedBox->addWidget(row);
	}
}

void Launcher::refreshHistory()
{
	if (!m_historyBox)
		return;
	clearLayout(m_historyBox);
	const QJsonArray list = QJsonDocument::fromJson(settings().value(historyKey()).toByteArray()).array();
	if (list.isEmpty())
	{
		m_historyBox->addWidget(text(T("No matches yet.")));
		return;
	}
	for (const auto& v : list)
		m_historyBox->addLayout(historyRow(v.toObject()));
}
