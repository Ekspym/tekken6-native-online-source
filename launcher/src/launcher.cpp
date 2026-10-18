// Tekken 6 Native Online launcher - The main window: construction, pages, navigation, shutdown, UPnP.

#include "launcher.h"

Launcher::Launcher()
{
	auto s = settings();
	m_char = s.value("char_index", 16).toInt();  // index into kChars (LEE)
	if (charHidden(m_char))
		m_char = 16;
	m_char = testChar(m_char);
	m_costumes = QJsonDocument::fromJson(s.value("costumes").toByteArray()).object();
	m_customs = QJsonDocument::fromJson(s.value("custom_looks").toByteArray()).object();
	m_customOff = QJsonDocument::fromJson(s.value("custom_looks_off").toByteArray()).object();
	m_delay = s.value("delay", 1).toInt();
	m_keys = qBound(0, s.value("keys", 1).toInt(), 3);
	m_gamePort = s.value("game_port", 7000 + (g_instance - 1) * 2).toInt();
	m_name = s.value("name", qEnvironmentVariable("USERNAME") + (g_instance > 1 ? QString::number(g_instance) : QString())).toString();
	m_session = s.value("session").toString();
	if (qEnvironmentVariableIsSet("T6_LAUNCHER_TAB"))  // (screenshots: the open tab of the settings page; tabbedPage bounds it)
		m_settingsTab = qEnvironmentVariableIntValue("T6_LAUNCHER_TAB");
#ifdef T6_DEV
	if (QCoreApplication::arguments().contains("stats"))  // (--page stats: tests)
		m_settingsTab = kStatsTab;
#endif
	m_maxPing = s.value("max_ping", 150).toInt();
	m_boardRegion = s.value("board_region", "ALL").toString();
	m_onlyOpen = s.value("only_open_lobbies", false).toBool();
	m_allRegions = s.value("lobby_all_regions", true).toBool();
	// (new key, user 2026-10-16: off for everyone at first - private lobbies are only asked for when ticked)
	m_showPrivate = s.value("show_private_lobbies_222", false).toBool();
	m_blur = s.value("blur", false).toBool();  // off by default (user 2026-10-12)
	m_vibration = s.value("vibration", false).toBool();  // off by default (user 2026-10-12)
	m_music = s.value("stage_music", true).toBool();  // stage music (user 2026-10-15: off for players' own music)
	m_matchSound = s.value("match_sound_mode", 1).toInt();
	m_matchSoundFile = s.value("match_sound_file").toString();
	// v2.1.5: everyone starts in Borderless window once (user 2026-10-15: players on Fullscreen - tearing, frames
	// not in step with the display - had a bad experience and lag against opponents without it); a later choice
	// in the display settings stays
	if (!s.value("display_borderless_215", false).toBool())
	{
		s.setValue("fullscreen", true);
		s.setValue("borderless", true);
		s.setValue("display_borderless_215", true);
	}
	m_fullscreen = s.value("fullscreen", true).toBool() && !qEnvironmentVariableIsSet("T6_LAUNCHER_WINDOWED");  // (tests: windowed)
	m_borderless = m_fullscreen && s.value("borderless", true).toBool();
	m_resScale = qBound(1, s.value("res_scale", 1).toInt(), 3);
	m_render3d = qBound(0, s.value("render_3d", 0).toInt(), 2);
	m_smallShadows = s.value("small_shadows", false).toBool();
	m_gpu = s.value("gpu").toString();  // adapter name; empty = automatic (high performance)
	m_side = qBound(0, s.value("side", 0).toInt(), 2);
	m_region = s.value("region", guessRegion()).toString();
	m_regionLock = s.value("region_lock", false).toBool();
	m_strict = s.value("strict_mm", false).toBool();
	m_clientId = s.value("client_id").toString();
	if (m_clientId.isEmpty())
	{
		m_clientId = QUuid::createUuid().toString(QUuid::WithoutBraces);
		s.setValue("client_id", m_clientId);
	}
#ifdef T6_DEV
	setWindowTitle(QString("Tekken 6 Online DEV") + (g_instance == 1 ? QString() : QString(" (%1)").arg(g_instance)));
#else
	setWindowTitle((testChannel() ? QString("Tekken 6 Online TEST") : QString("Tekken 6 Online")) +
	               (g_instance == 1 ? QString() : QString(" (%1)").arg(g_instance)));
#endif
	setMinimumSize(1120, 640);
	m_build = buildHash();
	build();
	// big enough at the start for the whole top bar with its labels (user 2026-10-18: "v zakladu dost veliky aby bylo
	// vse videt" - the player can make it smaller, the nav then shows icons); 5:3, never above the screen
	{
		setNavLabels(true);  // (measured with the labels)
		const int w = qMax(1360, m_bar->layout()->sizeHint().width() + 260);  // (+ room for a long name and the MMR)
		QSize start(w, w * 3 / 5);
		if (const QScreen* sc = QGuiApplication::primaryScreen())
			start = start.boundedTo(sc->availableGeometry().size() * 0.92);
		resize(start.expandedTo(minimumSize()));
	}
	connectServer();
	prelaunch();
	checkForUpdate();
	// and again every 15 minutes while the launcher runs (user 2026-10-11)
	connect(&m_updateTimer, &QTimer::timeout, this, [this] {
		if (m_updateUrl.isEmpty() && !m_updating)
			checkForUpdate();
	});
	m_updateTimer.start(2 * 60 * 1000);  // a new release shows within 2 minutes (GitHub: 30 of 60 requests per hour)
	startUpnp();
	// --build-from <path> (tests): build the game from that copy without the file dialog
	if (const QStringList args = QCoreApplication::arguments(); args.indexOf("--build-from") > 0)
		startBuild(args.value(args.indexOf("--build-from") + 1));
}

Launcher::~Launcher()
{
	try
	{
		shutdown();
	}
	catch (...)  // (a destructor does not throw: the process ends right after it)
	{
		OutputDebugStringW(L"Tekken 6 Online launcher: exception while closing\n");
	}
}

// the end of the launcher (destructor): no callbacks any more, the game ended, the UPnP mapping removed
void Launcher::shutdown()
{
	m_closing = true;
	// Nothing may call back into this window while it goes away. The server socket's destructor closed the
	// connection and its 'disconnected' ran serverLost() -> refreshAll() on widgets that were already destroyed:
	// heap corruption 0xc0000374 at every exit of the launcher, also at the exit before an update (crash dumps
	// 2026-10-12, stack: main -> QSslSocket destructor -> serverLost -> refreshAll -> RtlFreeHeap).
	for (QObject* o : findChildren<QObject*>())
		o->disconnect(this);
	if (m_tcp)
		m_tcp->abort();
	stopPractice();
	if (m_game)
	{
		command("quit");
		killGame("the launcher closes");
		m_game->waitForFinished(2000);
	}
	// (a worker still in its SSDP / HTTP waits is not waited for - closing the launcher right after its start took
	// up to ~10 s, review 2026-10-18; it ends with the process, its result is dropped)
	if (m_upnpThread.joinable())
	{
		if (m_upnpFinished && *m_upnpFinished)
			m_upnpThread.join();
		else
			m_upnpThread.detach();
	}
	if (m_upnpDone && m_upnp.mapped)
		upnpUnmap(m_upnp);
}

// UPnP: the game's UDP port forwarded to this PC by the router (worker thread: SSDP + HTTP take a few seconds)
void Launcher::startUpnp()
{
	if (g_instance > 1 || qEnvironmentVariableIsSet("T6_NO_UPNP"))
		return;  // (the second launcher of the local tests; T6_NO_UPNP=1: off)
	const int port = m_gamePort;
	auto finished = std::make_shared<std::atomic<bool>>(false);
	m_upnpFinished = finished;
	QPointer<Launcher> self(this);
	// (the worker touches nothing of this object: its result comes back on the UI thread, if the launcher still exists)
	m_upnpThread = std::thread([self, port, finished] {
		const Upnp u = upnpMap(port);
		*finished = true;
		QMetaObject::invokeMethod(qApp, [self, u] {
			if (!self)
				return;
			self->m_upnp = u;
			self->m_upnpDone = true;
			self->appendGameLog(self->upnpLine());
		}, Qt::QueuedConnection);
	});
}

QString Launcher::upnpLine() const
{
	if (!m_upnpDone)
		return "UPnP: no answer yet";
	const bool cgnat = !m_upnp.externalIp.isEmpty() && privateIpv4(m_upnp.externalIp);
	return m_upnp.mapped ? QString("UPnP: UDP %1 forwarded to %2 by the router (its external address %3%4)")
	                           .arg(m_upnp.port).arg(m_upnp.localIp, m_upnp.externalIp, cgnat ? QString(" - private: CGNAT / second router") : QString())
	                     : QString("UPnP: no forwarding (%1)%2").arg(m_upnp.error,
	                           m_upnp.externalIp.isEmpty() ? QString() : QString(", router's external address %1").arg(m_upnp.externalIp));
}

// the router's external address is private or not the one the server sees: CGNAT of the provider or a second router
bool Launcher::behindCgnat() const
{
	return m_upnpDone && !m_upnp.externalIp.isEmpty() &&
	       (privateIpv4(m_upnp.externalIp) || (!m_natIp.isEmpty() && m_upnp.externalIp != m_natIp));
}

// ================= UI
void Launcher::build()
{
	// the whole window is built again (login, logout): pointers into the old one must not survive
	m_practiceBtns.clear();
	m_practiceLobbyBtns.clear();
	m_syncers.clear();
	m_soloBtn = nullptr;
	m_loginForm = nullptr;
	m_rankedPrep = nullptr;
	// (verified 2026-10-14 with T6_TEST_PAD: two test launchers played a match and its rematch from the "button")
	if (!m_padTimer.isActive())
	{
		connect(&m_padTimer, &QTimer::timeout, this, [this] { padPoll(); });
		m_padTimer.start(50);
	}
	m_gpuLabel = nullptr;
	m_historyBox = nullptr;
	m_blockedBox = nullptr;
	auto* rootW = new BgWidget;
	rootW->setObjectName("root");
	auto* v = new QVBoxLayout(rootW);
	v->setContentsMargins(0, 0, 0, 0);
	v->setSpacing(0);

	// top bar (Sketch "TK6ONLINE" Top Bar, 2026-10-18): logo left, nav with icons + white underline, rank badge + name right
	auto* barW = new QWidget;
	barW->setObjectName("bar");
	barW->setAttribute(Qt::WA_StyledBackground);
	barW->setStyleSheet("QWidget#bar { background: rgba(0,0,0,0.25); border-bottom: 1px solid rgba(255,255,255,0.2); }");
	barW->setFixedHeight(87);
	auto* bar = new QHBoxLayout(barW);
	bar->setSizeConstraint(QLayout::SetNoConstraint);  // (no minimum width of the bar: see the size policy below)
	bar->setContentsMargins(40, 0, 40, 0);
	bar->setSpacing(0);
	{
		// "Logo Type" of the design (Bebas Neue outlines, ONLINE in #BF1802 with an orange glow), exported at 2x
		// (art of the official build, official.qrc; without it the name as text)
		QPixmap logo(":/art/logo.png");
		logo.setDevicePixelRatio(2.0);
		auto* lg = new QLabel;
		if (logo.isNull())
		{
			lg->setText("TEKKEN 6 <span style='color:#BF1802'>ONLINE</span>");
			lg->setObjectName("title");
		}
		else
			lg->setPixmap(logo);
		bar->addWidget(lg);
	}
	bar->addSpacing(58);
	// (no QButtonGroup: selectMode / showPage set the checks, SETTINGS is checked on the profile page instead of the mode)
	const QList<std::tuple<QString, Page, QString>> navs = {{"RANKED", PageRanked, "trophy"}, {"CASUAL", PageMatch, "account_circle"},
		{"LOBBY", PageLobby, "group"}, {"PRACTICE", PagePractice, "swords"}, {"LEADERBOARD", PageBoard, "lists"}};
	for (const auto& [name, page, icon] : navs)
	{
		auto* b = button(name, "nav");
		b->setCheckable(true);
		b->setIcon(navIcon(icon));
		b->setIconSize(QSize(16, 16));
		connect(b, &QPushButton::clicked, this, [this, page] { selectMode(page); });
		bar->addWidget(b);
		m_nav[page] = b;
	}
	m_settingsNav = button("SETTINGS", "nav");
	m_settingsNav->setCheckable(true);
	m_settingsNav->setIcon(navIcon("settings"));
	m_settingsNav->setIconSize(QSize(16, 16));
	// the settings page (the profile opens from the name on the right)
	connect(m_settingsNav, &QPushButton::clicked, this, [this] { showPage(PageSettings); });
	bar->addWidget(m_settingsNav);
	bar->addStretch();
	m_profileBtn = button("", "pill");
	connect(m_profileBtn, &QPushButton::clicked, this, [this] { showPage(PageProfile); });
	{
		// rank badge + one rich text label (the MMR in gold, user 2026-10-18); clicks go through to the button
		auto* pl = new QHBoxLayout(m_profileBtn);
		pl->setContentsMargins(0, 0, 0, 0);
		pl->setSpacing(12);
		m_pillBadge = new QLabel;
		m_pillText = new QLabel;
		m_pillText->setObjectName("pillText");
		QFont f = m_pillText->font();
		f.setCapitalization(QFont::AllUppercase);
		m_pillText->setFont(f);
		for (QLabel* l : {m_pillBadge, m_pillText})
		{
			l->setAttribute(Qt::WA_TransparentForMouseEvents);
			pl->addWidget(l);
		}
	}
	bar->addWidget(m_profileBtn);
	v->addWidget(barW);
	m_bar = barW;
	// the bar never makes the window wider: fitNav shows the nav as icons when its labels do not fit
	barW->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
	barW->installEventFilter(this);

	// the pages fill the window; only the long ones (profile, leaderboard list, lobby list) scroll inside themselves
	auto* content = new QWidget;
	m_content = content;
	auto* cv = new QVBoxLayout(content);
	cv->setContentsMargins(40, 22, 40, 20);
	cv->setSpacing(16);


	m_pages = new QStackedWidget;
	m_pages->addWidget(buildRanked());
	m_pages->addWidget(buildMatch());
	m_pages->addWidget(buildLobby());
	m_pages->addWidget(buildVs());
	auto* profileScroll = new QScrollArea;
	profileScroll->setWidgetResizable(true);
	profileScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	profileScroll->setWidget(buildProfile());
	m_pages->addWidget(profileScroll);
	m_pages->addWidget(buildLogin());
	m_pages->addWidget(buildBoard());
	m_pages->addWidget(buildPractice());
	auto* settingsScroll = new QScrollArea;
	settingsScroll->setWidgetResizable(true);
	settingsScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	settingsScroll->setWidget(buildSettings());
	m_pages->addWidget(settingsScroll);
	refreshBoard();
	cv->addWidget(m_pages, 1);

	auto* foot = new QHBoxLayout;
	// (the online count left in the bottom bar: the design's top bar has no room for it)
	m_status = text("", "small");
	foot->addWidget(m_status);
	foot->addSpacing(14);
	foot->addWidget(text(QString("Tekken 6 Online %1%2").arg(kVersion, testChannel() ? "  ·  TEST VERSION" : ""), "small"));
	foot->addStretch();
	// the server's notice in the middle of the bottom bar, on every page (applyNotice; was the v2.1.5 display notice)
	m_displayNote = text(QString(), "err");
	m_displayNote->setWordWrap(false);
	m_displayNote->setAlignment(Qt::AlignCenter);
	foot->addWidget(m_displayNote);
	showDisplayNote();
	{
		auto* tick = new QTimer(m_displayNote);  // (the countdown; dies with the label when the pages are built again)
		connect(tick, &QTimer::timeout, this, [this] { showDisplayNote(); });
		tick->start(10000);
	}
	foot->addStretch();
	// bottom right, in gold: an update is visible at once
	m_updateBtn = button(QString(), "ghostSmall");
	connect(m_updateBtn, &QPushButton::clicked, this, [this] {
		if (m_updateUrl.isEmpty())
			showPatchNotes();
		else
			installUpdate();
	});
	foot->addWidget(m_updateBtn);
	showUpdate();
	cv->addLayout(foot);
	v->addWidget(content, 1);
	setCentralWidget(rootW);
	buildPoll();
	selectMode(m_lastMode);
	const QStringList args = QCoreApplication::arguments();
	// T6_LAUNCHER_DEMO (screenshots of pages that need a match, with --no-game): "result" = the result of a ranked set
	// won with a promotion (Block player, RANK UP), "error" = a start that found no connection to the opponent
	if (const QString demo = qEnvironmentVariable("T6_LAUNCHER_DEMO"); demo == "result" || demo == "error" || demo == "confirm" || demo == "slow")
		QTimer::singleShot(7000, this, [this, demo] {  // (after the login, which builds the window again)
			if (demo == "confirm")  // (a found ranked match, this player is player 2 - the card must not show it)
			{
				const QJsonObject opp{{"name", "Opponent"}, {"char", 3}, {"elo", 590}, {"rank", 15}, {"rank_name", "Master"}, {"region", "EU"}};
				const QJsonObject me{{"name", m_name}, {"char", m_char}, {"elo", 600}, {"rank", 15}, {"rank_name", "Master"}, {"region", "EU"}};
				m_lobby = QJsonObject{{"queue", true}, {"ranked", true}, {"host", me}, {"guest", opp}};
				m_match = QJsonObject{{"p1", opp}, {"p2", me}, {"ranked", true}, {"match_id", "demo"}};
				m_role = 2;
				m_inLobby = m_hideOpp = m_punching = m_confirming = true;
				m_confirmPing = 42;
				m_confirmUntil = 15000;
				m_confirmClock.start();
				showPage(PageVs);
				refreshVs();
				return;
			}
			if (demo == "error")
			{
				setMatchText(T("MATCH DID NOT START"), T("No connection to the opponent's game (their firewall or router blocks it)."), true);
				showPage(PageVs);
				refreshVs();
				return;
			}
			const QJsonObject me{{"name", m_name}, {"char", m_char}, {"elo", 613}, {"rank", 16}, {"rank_name", "Rogue"}, {"region", "EU"}};
			const QJsonObject opp{{"name", "Opponent"}, {"char", 3}, {"elo", 590}, {"rank", 15}, {"rank_name", "Master"}, {"region", "EU"}};
			m_match = QJsonObject{{"p1", me}, {"p2", opp}, {"ranked", true}, {"match_id", "demo"}};
			m_role = 1;
			m_inMatch = m_resultShown = m_hadResult = m_setOver = true;
			m_ratedText = T("Set %1 – %2").arg(2).arg(1) + "   ·   MMR +18 (613)";
			m_rankDir = 1;
			m_rankNew = 16;
			if (demo == "slow")  // (the server ended the match: the opponent's game lagged)
			{
				m_rankDir = 0;
				m_slowEnd = m_matchErr = true;
				m_ratedText = T("%1's game lagged behind yours for most of the fight.%2 No rematch.")
				                  .arg("Opponent", T(" Ping %1 ms should mean about %2 frames behind, it was %3.").arg(48).arg(2.9, 0, 'f', 1).arg(7.4, 0, 'f', 1));
			}
			showPage(PageVs);
			refreshVs();
		});
	if (qEnvironmentVariableIsSet("T6_LAUNCHER_SCROLL"))  // (screenshots: the profile scrolled to the custom look buttons)
		QTimer::singleShot(7000, this, [this] {
			for (auto* sa : findChildren<QScrollArea*>())
				if (m_customResetBtn && sa->isAncestorOf(m_customResetBtn))
					sa->ensureWidgetVisible(m_customResetBtn, 50, 200);
		});
	if (const int i = args.indexOf("--page"); i >= 0 && i + 1 < args.size())
	{
		const QString& pg = args[i + 1];
		if (pg == "lobby") selectMode(PageLobby);
		if (pg == "ranked") selectMode(PageRanked);
		if (pg == "profile") showPage(PageProfile);
		if (pg == "settings" || pg == "stats") showPage(PageSettings);
		if (pg == "board") selectMode(PageBoard);
		if (pg == "practice") selectMode(PagePractice);
		// tests: --page practice --start-practice presses "Start practice" once the game can be used
		if (pg == "practice" && args.contains("--start-practice"))
		{
			auto* t = new QTimer(this);
			connect(t, &QTimer::timeout, this, [this, t] {
				static bool pressed = false;  // (the window is built again at the login: one press in all)
				if (pressed || m_practice || !m_soloBtn || !m_soloBtn->isEnabled() || m_pages->currentIndex() != PagePractice)
					return;
				t->stop();
				pressed = true;
				m_soloBtn->click();
			});
			t->start(1000);
		}
		// tests: --page profile --start-customize presses "Customize look" once the game can be used
		if (pg == "profile" && args.contains("--start-customize"))
		{
			auto* t = new QTimer(this);
			connect(t, &QTimer::timeout, this, [this, t] {
				static bool pressed = false;
				if (pressed || m_customize || !m_customizeBtn || !m_customizeBtn->isEnabled())
					return;
				t->stop();
				pressed = true;
				m_customizeBtn->click();
			});
			t->start(1000);
		}
	}
}

void Launcher::selectMode(Page p)
{
	m_lastMode = p;
	if (p == PageBoard)
		requestBoard();
	showPage(p);
}

// a choice made on a page or in the profile: every control showing it follows (m_syncers)
void Launcher::runSync()
{
	for (const auto& f : m_syncers)
		f();
}

// The pages built again (account after a reconnect). Never while a modal dialog runs its own event loop: a button
// of the old pages opened it and its click handler is still on the stack (review 2026-10-18: delete centralWidget()
// inside QFileDialog / QInputDialog = use after free when the dialog returns). Waits until the dialog is closed.
void Launcher::rebuildUi()
{
	if (QApplication::activeModalWidget() || QApplication::activePopupWidget())
	{
		if (!m_rebuildPending)
		{
			m_rebuildPending = true;
			QTimer::singleShot(250, this, [this] {
				m_rebuildPending = false;
				if (m_authed && !m_inMatch && !m_punching)
					rebuildUi();
			});
		}
		return;
	}
	delete centralWidget();
	build();
}

// A message box that does not block: shown from the server message handler, whose socket must go on being read
// (review 2026-10-18: a modal box there held every later server message - a found match's 15 s confirm ran out)
void Launcher::notify(QMessageBox::Icon icon, const QString& text)
{
	auto* box = new QMessageBox(icon, "Tekken 6 Online", text, QMessageBox::Ok, this);
	box->setAttribute(Qt::WA_DeleteOnClose);
	box->setWindowModality(Qt::NonModal);
	box->show();
}

void Launcher::showPage(Page p)
{
	if (p != PageSettings)
		applyControls();
	if (!m_authed)
		p = PageLogin;
	// in a lobby / match the VS screen stays in front (the profile and the settings can still be opened)
	const bool aside = p == PageProfile || p == PageSettings;
	if ((m_inLobby || m_punching || m_inMatch || !m_resultText.isEmpty()) && !aside)
		p = PageVs;
	m_pages->setCurrentIndex(p);
	// top bar: the mode, SETTINGS on the settings page, nothing on the profile (the name opened it)
	for (auto [k, b] : m_nav.asKeyValueRange())
		b->setChecked(!aside && k == m_lastMode);
	m_settingsNav->setChecked(p == PageSettings);
	refreshAll();
	refreshPoll();
	if (p == PageLobby)
		requestLobbies();
}

void Launcher::refreshAll()
{
	if (!m_pages)
		return;
	// top bar: the rank badge, the name and the MMR in gold (user 2026-10-18: "MMR stejne jako jsme to meli predtim", "zlute")
	m_pillText->setText(!m_authed ? T("Not logged in")
	                    : m_guest ? m_name.toHtmlEscaped()
	                              : QString("%1 &nbsp;<span style='color:#EFBC23'>MMR %2</span>").arg(m_name.toHtmlEscaped()).arg(m_elo));
	const QPixmap pillIcon = m_authed && !m_guest ? rankIcon(m_rank, 64) : QPixmap();
	m_pillBadge->setPixmap(pillIcon.isNull() ? QPixmap() : pillIcon.scaled(75, 36, Qt::KeepAspectRatio, Qt::SmoothTransformation));
	m_pillBadge->setVisible(!pillIcon.isNull());
	m_profileBtn->setFixedSize(m_profileBtn->layout()->sizeHint());  // (the button has no text of its own)
	m_profileBtn->setToolTip(m_authed && !m_guest ? rankName() : QString());
	m_profileBtn->setEnabled(m_authed);
	fitNav();  // (the name changes the room the nav has)
	refreshCostume();  // (the Customize look button follows the game's state)
	m_status->setText((m_online ? (m_onlineCount > 0 ? T("● %1 online").arg(m_onlineCount) : T("● online"))
	                            : m_connected ? T("● not logged in") : T("● offline")) +
	                  (preparing() ? T("   ·   preparing the game…") : QString()));
	m_status->setStyleSheet(m_online ? "color:#3ecf8e; font-weight:600;" : m_connected ? "color:#EFBC23; font-weight:600;" : "color:#ff6b6b; font-weight:600;");
	m_loginBtn->setEnabled(m_connected && !m_authed);
	m_registerBtn->setEnabled(m_connected && !m_authed);
	if (m_loginForm)
	{
		// a stored login: no form while the launcher connects and signs in by itself
		const bool waiting = !m_authed && !m_session.isEmpty();
		const QString who = settings().value("account").toString();
		m_loginForm->setVisible(!waiting);
		m_loginWait->setVisible(waiting);
		m_loginWaitSub->setVisible(waiting);
		m_loginWait->setText(m_connected ? (who.isEmpty() ? T("Signing in…") : T("Signing in as %1…").arg(who)) : T("Connecting to the server…"));
		m_loginWaitSub->setText(m_connected ? T("One moment.")
		                                    : T("The launcher logs you in by itself as soon as the server answers. If this stays, check your internet connection."));
	}
	// nothing to go to while not logged in: the page stays on the login anyway
	for (auto [k, b] : m_nav.asKeyValueRange())
		b->setEnabled(m_authed);
	m_settingsNav->setEnabled(m_authed);
	if (m_guest || !m_authed)
		m_profileStats->setText(QString());
	else
	{
		const QJsonObject r = m_chars[QString::number(m_char)].toObject();
		// the Elo is per character (server): m_elo = the selected character's
		m_profileStats->setText(T("%1: MMR %2   ·   %3 W / %4 L      All characters: matches %5   ·   wins %6")
		                            .arg(charName(m_char)).arg(m_elo).arg(r["wins"].toInt()).arg(r["losses"].toInt()).arg(m_games).arg(m_wins));
	}
	const bool idle = m_online && !m_inLobby && !m_inMatch && !m_punching && m_gameReady;
	const bool qRanked = m_queued && m_queuedRanked, qQuick = m_queued && !m_queuedRanked;
	// The game starts in the background when the launcher opens (half a minute, longer at the first start after an
	// update). Until then nothing can be searched or started - and the player has to SEE why (user 2026-10-14):
	// the buttons say it with the seconds, the pages explain it, the lobby buttons carry it as their tool tip.
	const bool prep = preparing();
	if (prep && !m_prepSince.isValid())
		m_prepSince.start();
	if (!prep)
		m_prepSince.invalidate();
	// (user: no counter - just say it: preparing the game, compiling shaders)
	const QString prepBtn = prep ? T("Preparing the game…") : QString();
	const QString prepWhy = T("The game is starting in the background and compiling its shaders. Searching, lobbies and the practice unlock as "
	                          "soon as it is ready - usually within half a minute; the first start after an update takes longer.");
	m_quickBtn->setEnabled(idle && !qRanked);
	// the controller's confirm button searches: "again" after a match, else while the launcher is in front (padPoll)
	const bool padSearch = m_padTarget == 2, padAgain = padSearch && m_searchAgain.isValid();
	m_quickBtn->setText(prep ? prepBtn : qQuick ? T("Cancel search  (B / ○)") : padAgain ? T("Search again  (A / ✕)") : padSearch ? T("Find casual match  (A / ✕)") : T("Find casual match"));
	m_rankedBtn->setEnabled(idle && !qQuick && !m_guest);
	m_rankedBtn->setText(prep ? prepBtn : qRanked ? T("Cancel search  (B / ○)") : padAgain ? T("Search again  (A / ✕)") : padSearch ? T("Find ranked match  (A / ✕)") : T("Find ranked match"));
	for (QPushButton* b : {m_quickBtn, m_rankedBtn, m_createBtn, m_joinBtn})
		if (b)
			b->setToolTip(prep ? prepWhy : QString());
	// the pages' gold state line (empty = nothing to say: the design's page alone) and the grey line under it
	const QString gameState = !gameBuilt()   ? T("Build the game first (Settings → Build game)")
	                          : m_autoBuild    ? T("Updating the game…")
	                          : !gameUsable()  ? T("Build the game again (Settings → Build game)")
	                          : m_bootError    ? T("The game failed to start")
	                          : !m_gameReady   ? T("Preparing the game – compiling shaders…")
	                                           : QString();
	const auto show = [](QLabel* l, const QString& t) {
		l->setText(t);
		l->setVisible(!t.isEmpty());
	};
	show(m_rankedInfo, !gameState.isEmpty() ? gameState
	                   : qRanked            ? T("Searching for an opponent…")
	                   : !m_searchNote.isEmpty() && m_lastMode == PageRanked ? m_searchNote
	                                                                         : QString());
	show(m_rankedPrep, prep ? prepWhy : qRanked ? T("As soon as someone else searches, the server pairs you; you see the ping and accept the match.") : QString());
	show(m_matchInfo, !gameState.isEmpty() ? gameState
	                  : qQuick             ? T("Searching for an opponent…")
	                  : !m_searchNote.isEmpty() && m_lastMode == PageMatch ? m_searchNote
	                                                                       : QString());
	show(m_matchSub, prep ? prepWhy : qQuick ? T("As soon as someone else searches, the server pairs you; you see the ping and accept the match.") : QString());
	runSync();
	m_createBtn->setEnabled(idle && !m_queued);
	refreshPractice();
	practiceSearch();  // (the online game may get ready after the practice showed)
	m_joinBtn->setEnabled(idle && !m_queued);
	refreshList();
	refreshVs();
}

// T6_LAUNCHER_LOG=<file> (tests): a line into <file><instance>
void Launcher::testLog(const QString& line)
{
	if (!qEnvironmentVariableIsSet("T6_LAUNCHER_LOG"))
		return;
	QFile lf(qEnvironmentVariable("T6_LAUNCHER_LOG") + QString::number(g_instance));
	if (lf.open(QIODevice::Append | QIODevice::Text))
		lf.write(QString("%1 %2\n").arg(QTime::currentTime().toString("HH:mm:ss.zzz"), line).toUtf8());
}

// a narrow window: the nav shows only its icons (the label in the tooltip) instead of pushing the bar past the window
void Launcher::fitNav()
{
	if (!m_bar)
		return;
	setNavLabels(true);
	if (m_bar->layout()->minimumSize().width() > m_bar->width())
		setNavLabels(false);
}

// the nav with its labels, or icons only (the label in the tooltip)
void Launcher::setNavLabels(bool on)
{
	QList<QPushButton*> navs = m_nav.values();
	navs.append(m_settingsNav);
	for (auto* b : navs)
		if (b->text().isEmpty() == on)
		{
			const QString label = on ? b->toolTip() : b->text();
			b->setText(on ? label : QString());
			b->setToolTip(on ? QString() : label);
		}
}

bool Launcher::eventFilter(QObject* o, QEvent* e)
{
	if (o == m_content && e->type() == QEvent::Resize)
		placePoll();
	if (o == m_bar && e->type() == QEvent::Resize)
		fitNav();
	return QMainWindow::eventFilter(o, e);
}
