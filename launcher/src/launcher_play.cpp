// Tekken 6 Native Online launcher - Play pages: ranked / casual search, leaderboard, lobby list, the VS page.

#include "launcher.h"

// Sketch screens "1 Ranked" / "2 Player": title + subtitle left, CHARACTER / SIDE / REGION / MAX PING dropdowns right,
// the rules as a list, FIND ... MATCH (Primary) + PRACTICE & SEARCH (Default); the state of the game / search in gold
QWidget* Launcher::buildMode(bool ranked)
{
	auto* c = card();
	auto* v = new QVBoxLayout(c);
	v->setContentsMargins(0, 0, 0, 0);
	v->setSpacing(0);
	auto* top = new QHBoxLayout;
	top->setSpacing(12);
	auto* head = new QVBoxLayout;
	head->setSpacing(4);
	head->addSpacing(5);
	head->addWidget(text(ranked ? T("Ranked Match") : T("Casual Match"), "title"));
	head->addWidget(text(ranked ? T("Play a first-to-2 set against a player of similar MMR") : T("Play unranked matches with a random player"), "sub"));
	top->addLayout(head);
	top->addStretch();
	auto* charBox = new CharCombo(ranked);
	const QList<int> order = charsByName();
	for (const int i : order)
		charBox->addItem(kChars[i], i);
	charBox->setMinimumWidth(ranked ? 280 : 250);
	connect(charBox, &QComboBox::activated, this, [this, charBox] { setChar(charBox->currentData().toInt()); });
	top->addWidget(dropdown(T("Character"), charBox), 0, Qt::AlignTop);
	// the side: casual only (ranked sides are random, user 2026-10-18: "ranked si nema moznost vybrat stranu")
	QComboBox* sideBox = nullptr;
	if (!ranked)
	{
		sideBox = choiceBox({{T("Random"), 0}, {T("Left (P1)"), 1}, {T("Right (P2)"), 2}}, m_side, 150);
		sideBox->setToolTip(T("Used when it does not collide with the opponent's choice."));
		connect(sideBox, &QComboBox::activated, this, [this, sideBox] { setSide(sideBox->currentData().toInt()); });
		top->addWidget(dropdown(T("Preferred side"), sideBox), 0, Qt::AlignTop);
	}
	auto* regionBox = new QComboBox;
	for (const char* r : {"EU", "USA", "SA", "ME", "SEA", "ASIA", "OCE", "AF"})
		regionBox->addItem(r, r);
	regionBox->setMinimumWidth(96);
	regionBox->setToolTip(T("Matchmaking pairs inside your region first and takes another region after ~15 s of waiting."));
	connect(regionBox, &QComboBox::activated, this, [this, regionBox] { setRegion(regionBox->currentData().toString()); });
	top->addWidget(dropdown(T("Region"), regionBox), 0, Qt::AlignTop);
	auto* pingBox = new QComboBox;
	for (int ms : {60, 100, 150, 200, 0})
		pingBox->addItem(ms ? QString::number(ms) : T("Any"), ms);
	pingBox->setMinimumWidth(100);
	pingBox->setToolTip(T("Matchmaking skips opponents with a higher ping."));
	connect(pingBox, &QComboBox::activated, this, [this, pingBox] { setMaxPing(pingBox->currentData().toInt()); });
	top->addWidget(dropdown(T("Max ping (ms)"), pingBox), 0, Qt::AlignTop);
	if (ranked)
	{
		// Strict matchmaking right where the ranked search starts (user 2026-10-18), the same switch as Settings -> ONLINE
		auto* sw = new QWidget;
		auto* sv = new QVBoxLayout(sw);
		sv->setContentsMargins(0, 0, 0, 0);
		sv->setSpacing(4);
		auto* l = new QLabel(T("Strict MM").toUpper());
		l->setObjectName("ddLabel");
		sv->addWidget(l);
		sv->addSpacing(9);  // (the toggle in the middle of the 44 px fields next to it)
		sv->addWidget(strictToggle());
		sw->setToolTip(T("Never against a player more than 150 MMR above or below you. Finding a match can take longer."));
		top->addWidget(sw, 0, Qt::AlignTop);
	}
	m_syncers.push_back([this, charBox, sideBox, regionBox, pingBox] {
		const QSignalBlocker b1(charBox), b3(regionBox), b4(pingBox);
		charBox->setCurrentIndex(qMax(0, charBox->findData(m_char)));
		charBox->setRank(m_guest ? QPixmap() : rankIcon(m_rank, 56), m_elo);
		if (sideBox)
		{
			const QSignalBlocker b2(sideBox);
			sideBox->setCurrentIndex(qMax(0, sideBox->findData(m_side)));
		}
		regionBox->setCurrentIndex(qMax(0, regionBox->findData(m_region)));
		int pi = pingBox->findData(m_maxPing);
		if (pi < 0)
		{
			pingBox->addItem(QString::number(m_maxPing), m_maxPing);  // (a value set elsewhere stays shown)
			pi = pingBox->count() - 1;
		}
		pingBox->setCurrentIndex(pi);
	});
	v->addLayout(top);
	v->addSpacing(32);
	v->addWidget(text(ranked ? T("- 3 rounds\n- Leaving between matches loses the set\n- Leaving during a match results in double penalty")
	                         : T("- 3 rounds\n- Unlimited rematches"),
	                  "body"));
	v->addSpacing(28);
	auto* status = text(QString(), "status");
	status->setWordWrap(true);
	status->setVisible(false);
	v->addWidget(status);
	auto* detail = text(QString(), "muted");
	detail->setVisible(false);
	v->addWidget(detail);
	v->addSpacing(12);
	auto* row = new QHBoxLayout;
	row->setSpacing(12);
	auto* find = button(ranked ? T("Find ranked match") : T("Find casual match"), "cta", ranked ? "trophy" : "account_circle");
	connect(find, &QPushButton::clicked, this, [this, ranked] {
		if (m_queued)
			send({{"t", "quick_cancel"}});
		else if (ranked)
			send({{"t", "quick"}, {"ranked", true}});
		else
			send({{"t", "quick"}});
	});
	row->addWidget(find);
	row->addWidget(practiceButton(ranked ? 2 : 1));
	row->addStretch();
	v->addLayout(row);
	v->addStretch();
	if (ranked)
	{
		m_rankedInfo = status;
		m_rankedPrep = detail;
		m_rankedBtn = find;
	}
	else
	{
		m_matchInfo = status;
		m_matchSub = detail;
		m_quickBtn = find;
	}
	return c;
}

QWidget* Launcher::buildRanked() { return buildMode(true); }

QWidget* Launcher::buildMatch() { return buildMode(false); }

void Launcher::setChar(int i)
{
	m_char = i;
	settings().setValue("char_index", i);
	refreshCostume();
	sendProfile();
	runSync();
}

// Strict matchmaking (user 2026-10-16): ranked never pairs this player with one more than 150 MMR above or below;
// one switch on the ranked page and one in Settings -> ONLINE, kept in step by m_syncers
QCheckBox* Launcher::strictToggle()
{
	auto* t = toggle(m_strict);
	connect(t, &QCheckBox::toggled, this, [this](bool on) {
		if (m_strict == on)
			return;
		m_strict = on;
		settings().setValue("strict_mm", on);
		sendProfile();
		runSync();
	});
	m_syncers.push_back([this, t] {
		const QSignalBlocker b(t);
		t->setChecked(m_strict);
	});
	return t;
}

void Launcher::setSide(int side)
{
	m_side = side;
	settings().setValue("side", side);
	sendProfile();
	runSync();
}

void Launcher::setRegion(const QString& reg)
{
	m_region = reg;
	settings().setValue("region", m_region);
	sendProfile();
	runSync();
}

void Launcher::setMaxPing(int ms)
{
	m_maxPing = ms;
	settings().setValue("max_ping", ms);
	syncSettings();
	runSync();
}

// LEADERBOARD tab: every account by Elo (the server sends the first 100), the own row in gold
// the icon-only Refresh of the lobby list and the leaderboard
QPushButton* Launcher::refreshButton(const std::function<void()>& fn)
{
	auto* b = button(QString(), "iconBtn");
	b->setIcon(QIcon(tinted("refresh", Qt::white, 22)));
	b->setIconSize(QSize(22, 22));
	b->setToolTip(T("Refresh"));
	connect(b, &QPushButton::clicked, this, fn);
	return b;
}

QWidget* Launcher::buildBoard()
{
	auto* c = card();
	auto* v = new QVBoxLayout(c);
	v->setContentsMargins(32, 28, 32, 30);
	v->setSpacing(12);
	auto* head = new QHBoxLayout;
	head->addWidget(text(T("Leaderboard"), "h1"));
	head->addStretch();
	// region filter: ALL or the region the players set (Profile -> MATCHMAKING SETTINGS); the server ranks inside it
	auto* reg = new QComboBox;
	styleField(reg);
	reg->setMinimumWidth(160);
	reg->addItem(T("All regions"), "ALL");
	for (const char* r : {"EU", "USA", "SA", "ME", "SEA", "ASIA", "OCE", "AF"})
		reg->addItem(r, r);
	reg->setCurrentIndex(qMax(0, reg->findData(m_boardRegion)));
	connect(reg, &QComboBox::currentIndexChanged, this, [this, reg] {
		m_boardRegion = reg->currentData().toString();
		settings().setValue("board_region", m_boardRegion);
		m_board = QJsonArray();
		m_boardCount->clear();
		refreshBoard();
		requestBoard();
	});
	// under the filter: how many players there are at all, not only the 100 of the list (user 2026-10-13)
	auto* regCol = new QVBoxLayout;
	regCol->setSpacing(4);
	regCol->addWidget(reg);
	m_boardCount = text(QString(), "gold");
	m_boardCount->setAlignment(Qt::AlignRight);
	regCol->addWidget(m_boardCount);
	head->addLayout(regCol);
	// the board only on request (2.2.2, user 2026-10-16): the server computes it when asked and it is older than a minute;
	// Refresh as on the lobby page (user 2026-10-18), the label under it says when the board was made
	auto* refCol = new QVBoxLayout;
	refCol->setSpacing(4);
	refCol->addWidget(refreshButton([this] { requestBoard(); }), 0, Qt::AlignRight);
	m_boardUpdated = text(QString(), "small");
	m_boardUpdated->setAlignment(Qt::AlignRight);
	refCol->addWidget(m_boardUpdated);
	head->addSpacing(12);
	head->addLayout(refCol);
	v->addLayout(head);
	v->addWidget(text(T("Players by the MMR of their best character. The MMR is per character and moves only in ranked matches and ranked lobbies.")));
	auto* board = new QWidget;
	m_boardBox = new QVBoxLayout(board);
	m_boardBox->setContentsMargins(0, 0, 8, 0);
	m_boardBox->setSpacing(6);
	auto* boardScroll = new QScrollArea;
	boardScroll->setWidgetResizable(true);
	boardScroll->setWidget(board);
	v->addWidget(boardScroll, 1);
	return c;
}

// Sketch screen "3 Lobbies": "Public Lobbies" + Hide Full / Show All Regions toggles + Refresh, the rows (refreshList);
// right of a thin line: Create Lobby (Ranked / Private toggles) and Join with Code
QWidget* Launcher::buildLobby()
{
	auto* w = new QWidget;
	auto* h = new QHBoxLayout(w);
	h->setContentsMargins(0, 0, 0, 0);
	h->setSpacing(40);
	auto* list = card();
	auto* lv = new QVBoxLayout(list);
	lv->setContentsMargins(0, 0, 0, 0);
	lv->setSpacing(12);
	auto* lh = new QHBoxLayout;
	lh->setSpacing(28);
	lh->addWidget(text(T("Public Lobbies"), "title"));
	lh->addStretch();
	// filter: only lobbies that can be joined (the full / playing ones stay in the list otherwise, as FULL)
	auto* onlyOpen = new QCheckBox(T("Hide Full"));
	onlyOpen->setChecked(m_onlyOpen);
	connect(onlyOpen, &QCheckBox::toggled, this, [this](bool on) {
		m_onlyOpen = on;
		settings().setValue("only_open_lobbies", on);
		refreshList();
	});
	lh->addWidget(onlyOpen);
	// filter (design 2026-10-18): off = only lobbies of hosts in this player's region (Settings / the pages' REGION)
	auto* allRegions = new QCheckBox(T("Show All Regions"));
	allRegions->setChecked(m_allRegions);
	connect(allRegions, &QCheckBox::toggled, this, [this](bool on) {
		m_allRegions = on;
		settings().setValue("lobby_all_regions", on);
		refreshList();
	});
	lh->addWidget(allRegions);
	// the list comes only on request (user 2026-10-16, server traffic): when the page opens and on Refresh, like the
	// leaderboard; the tool tip of the button says when it came
	lh->addWidget(refreshButton([this] { requestLobbies(); }));
	lv->addLayout(lh);
	m_lobbyUpdated = text(QString(), "small");
	m_lobbyUpdated->setAlignment(Qt::AlignRight);
	lv->addWidget(m_lobbyUpdated);
	auto* scroll = new QScrollArea;
	scroll->setWidgetResizable(true);
	auto* inner = new QWidget;
	m_listBox = new QVBoxLayout(inner);
	m_listBox->setContentsMargins(0, 0, 4, 0);
	m_listBox->setSpacing(4);
	scroll->setWidget(inner);
	lv->addWidget(scroll, 1);
	h->addWidget(list, 1);

	auto* side = new QFrame;
	side->setObjectName("side");
	side->setFixedWidth(286);
	auto* sv = new QVBoxLayout(side);
	sv->setContentsMargins(38, 0, 0, 0);
	sv->setSpacing(16);
	sv->addWidget(text(T("Create Lobby"), "title"));
	// ranked lobby: the matches count for the Elo like the ranked queue; without it the lobby is for fun
	auto* toggles = new QHBoxLayout;
	toggles->setSpacing(28);
	m_rankedLobby = new QCheckBox(T("Ranked"));
	m_rankedLobby->setToolTip(T("Ranked – play for MMR"));
	toggles->addWidget(m_rankedLobby);
	m_private = new QCheckBox(T("Private"));
	m_private->setToolTip(T("Private – code only"));
	toggles->addWidget(m_private);
	toggles->addStretch();
	sv->addLayout(toggles);
	m_createBtn = button(T("Create lobby"), "cta");
	m_createBtn->setFixedWidth(200);
	connect(m_createBtn, &QPushButton::clicked, this, [this] {
		send({{"t", "create"}, {"public", !m_private->isChecked()}, {"ranked", m_rankedLobby->isChecked()}});
	});
	sv->addWidget(m_createBtn);
	sv->addSpacing(48);
	sv->addWidget(text(T("Join with Code"), "title"));
	m_codeEdit = new QLineEdit;
	m_codeEdit->setPlaceholderText(T("Enter Code…"));
	m_codeEdit->setMaxLength(5);
	m_codeEdit->setFixedWidth(200);
	sv->addWidget(m_codeEdit);
	m_joinBtn = button(T("Join"), "ghost");
	m_joinBtn->setFixedWidth(200);
	connect(m_joinBtn, &QPushButton::clicked, this, [this] { send({{"t", "join"}, {"code", m_codeEdit->text().trimmed().toUpper()}}); });
	connect(m_codeEdit, &QLineEdit::returnPressed, m_joinBtn, &QPushButton::click);
	sv->addWidget(m_joinBtn);
	sv->addStretch();
	h->addWidget(side, 0);
	return w;
}

QWidget* Launcher::buildVs()
{
	auto* c = card();
	auto* v = new QVBoxLayout(c);
	v->setContentsMargins(28, 24, 28, 26);
	v->setSpacing(12);
	m_vs = new VsPanel;
	v->addWidget(m_vs, 1);
	auto* codeRow = new QHBoxLayout;
	codeRow->addStretch();
	m_vsCodeLabel = text(T("Lobby code"), "muted");
	codeRow->addWidget(m_vsCodeLabel);
	codeRow->addSpacing(10);
	m_vsCode = text("", "code");
	m_vsCode->setTextInteractionFlags(Qt::TextSelectableByMouse);
	codeRow->addWidget(m_vsCode);
	codeRow->addStretch();
	v->addLayout(codeRow);
	m_vsInfo = text("");
	m_vsInfo->setAlignment(Qt::AlignCenter);
	v->addWidget(m_vsInfo);
	// rank up after a ranked set (user 2026-10-16), big under the result; the game shows its own at the final KO
	m_rankBanner = new QWidget;
	auto* rb = new QHBoxLayout(m_rankBanner);
	rb->setContentsMargins(0, 4, 0, 4);
	rb->setSpacing(14);
	rb->addStretch();
	m_rankIconL = new QLabel;
	rb->addWidget(m_rankIconL);
	m_rankTextL = new QLabel;
	rb->addWidget(m_rankTextL);
	rb->addStretch();
	m_rankBanner->setVisible(false);
	v->addWidget(m_rankBanner);
	// created lobby (not ranked): the host picks the stage, the guest sees it (user 2026-10-13). Ids = the game's
	// stage ids; names confirmed by the user against screenshots of every id (runs\stages\stages_sheet_*.png,
	// 2026-10-13). 18 stages (12 Central Tower removed 2026-10-15). Not offered and never given by the server: 14, 15, 24 (14 = a part of a stage one gets to
	// by a wall / floor break, user; 15 and 24 a black screen), 21-23 and 25-27 (the game hangs in the loading). The
	// names missing from the game's list (Azazel's Temple, Prophet's Garden, Hidden Retreat 2, Mishima Estate, Wild
	// Kingdom) are such second parts of stages, not stages to start on.
	auto* stageRow = new QHBoxLayout;
	stageRow->addStretch();
	m_stageLabel = text(T("Stage"), "gold");
	stageRow->addWidget(m_stageLabel);
	stageRow->addSpacing(10);
	m_stageBox = new QComboBox;
	m_stageBox->setMinimumWidth(220);
	m_stageBox->addItem(T("Random"), -1);
	for (const auto& [id, name] : kStages)
		m_stageBox->addItem(name, id);
	connect(m_stageBox, &QComboBox::activated, this, [this] { send({{"t", "stage"}, {"stage", m_stageBox->currentData().toInt()}}); });
	stageRow->addWidget(m_stageBox);
	stageRow->addStretch();
	v->addLayout(stageRow);
	auto* row = new QHBoxLayout;
	row->addStretch();
	m_leaveBtn = button(T("Leave"), "ghost");
	connect(m_leaveBtn, &QPushButton::clicked, this, [this] { leaveAction(); });
	row->addWidget(m_leaveBtn);
	row->addSpacing(10);
	// after a game: block the opponent (user 2026-10-16) - no queue pairs the two again; Profile -> BLOCKED undoes it.
	// The first click asks (the text changes), the second blocks and leaves like Leave / Back to ranked.
	m_blockBtn = button(T("Block player"), "ghost");
	m_blockBtn->setVisible(false);
	connect(m_blockBtn, &QPushButton::clicked, this, [this] {
		if (!m_blockArmed)
		{
			m_blockArmed = true;
			m_blockArmAt.start();
			refreshVs();
			QTimer::singleShot(4000, this, [this] {
				if (m_blockArmed && m_blockArmAt.elapsed() >= 3900)
				{
					m_blockArmed = false;
					refreshVs();
				}
			});
			return;
		}
		m_blockArmed = false;
		send({{"t", "block"}});
		if (m_inMatch && m_setOver)
			readyAction();  // (= Back to ranked)
		else
			leaveAction();
	});
	row->addWidget(m_blockBtn);
	row->addSpacing(10);
	m_readyBtn = button(T("Ready"), "cta");
	m_readyBtn->setMinimumWidth(240);
	connect(m_readyBtn, &QPushButton::clicked, this, [this] { readyAction(); });
	row->addWidget(m_readyBtn);
	// ranked set: the loser of the last game may take the next one to another (random) stage (user 2026-10-12)
	m_randBtn = button(T("Next game – random stage"), "ghost");
	m_randBtn->setVisible(false);
	connect(m_randBtn, &QPushButton::clicked, this, [this] {
		m_randomStage = true;
		readyAction();
	});
	row->addSpacing(10);
	row->addWidget(m_randBtn);
	row->addSpacing(10);
	row->addWidget(practiceButton(0));  // practice while waiting in the lobby
	row->addStretch();
	v->addLayout(row);
	return c;
}

// the lobby list once (2.2.2: no 'watch' pushes any more); at most one request per 2 s unless forced (filter change)
void Launcher::requestLobbies(bool force)
{
	if (!m_authed || (!force && m_lobbyAsked.isValid() && m_lobbyAsked.elapsed() < 2000))
		return;
	m_lobbyAsked.start();
	send({{"t", "list"}, {"private", m_showPrivate}});
}

void Launcher::requestBoard()
{
	send({{"t", "leaderboard"}, {"region", m_boardRegion}});
}

void Launcher::refreshBoard()
{
	if (!m_boardBox)
		return;
	clearLayout(m_boardBox);
	if (m_board.isEmpty())
	{
		m_boardBox->addWidget(text(T("Loading…")));
		return;
	}
	for (const auto& v : m_board)
	{
		const QJsonObject e = v.toObject();
		const bool me = e["pos"].toInt() == m_boardPos;
		auto* row = card("inner");
		auto* r = new QHBoxLayout(row);
		r->setContentsMargins(16, 8, 16, 8);
		r->setSpacing(12);
		const auto cell = [&](const QString& t, int w, const char* color) {
			auto* l = new QLabel(t);
			l->setStyleSheet(QString("color:%1; font-weight:600; font-size:15px;").arg(color));
			if (w)
				l->setFixedWidth(w);
			r->addWidget(l, w ? 0 : 1);
		};
		cell(QString("#%1").arg(e["pos"].toInt()), 50, "#8C8DA0");
		cell(e["name"].toString(), 0, me ? "#EFBC23" : "#E8E8F2");
		cell(e["region"].toString(), 60, "#8C8DA0");  // as the player set it (Profile -> MATCHMAKING SETTINGS)
		cell(e["char"].isNull() ? QString() : charName(e["char"].toInt()), 130, "#E8E8F2");  // the best character (its Elo)
		auto* icon = new QLabel;
		icon->setPixmap(rankIcon(e["rank"].toInt(-1), 40));
		icon->setFixedWidth(120);
		icon->setAlignment(Qt::AlignCenter);
		icon->setToolTip(e["rank_name"].toString());
		r->addWidget(icon);
		cell(QString("MMR %1").arg(e["elo"].toInt()), 100, "#EFBC23");
		cell(T("%1 W / %2 L").arg(e["wins"].toInt()).arg(e["games"].toInt() - e["wins"].toInt()), 120, "#8C8DA0");
		// click: the player's last matches from the server (user 2026-10-12)
		row->setCursor(Qt::PointingHandCursor);
		row->setToolTip(T("Click: match history"));
		const QString name = e["name"].toString();
		row->installEventFilter(new ClickFilter(row, [this, name] { send({{"t", "history"}, {"name", name}}); }));
		m_boardBox->addWidget(row);
	}
	m_boardBox->addStretch();
}

void Launcher::refreshList()
{
	clearLayout(m_listBox);
	QJsonArray shown;
	for (const auto& lv : m_lobbies)
		if ((!m_onlyOpen || lv.toObject()["state"].toString("open") == "open") && (m_showPrivate || !lv.toObject()["private"].toBool()) &&
		    (m_allRegions || lv.toObject()["host"].toObject()["region"].toString() == m_region))
			shown.append(lv);
	if (shown.isEmpty())
	{
		auto* empty = card("inner");
		auto* ev = new QVBoxLayout(empty);
		ev->setContentsMargins(20, 26, 20, 26);
		auto* t = text(!m_online         ? T("Not connected to the server.")
		               : m_lobbies.isEmpty() ? T("No lobbies here yet. Create one on the right.")
		                                     : T("No open lobby right now - all are full. Create one on the right."));
		t->setAlignment(Qt::AlignCenter);
		ev->addWidget(t);
		m_listBox->addWidget(empty);
	}
	for (const auto& lv : shown)
	{
		const QJsonObject l = lv.toObject(), host = l["host"].toObject();
		const bool open = l["state"].toString("open") == "open";
		// Sketch "Lobby" row: Ranked (gold trophy) / Casual (grey) | region (green = yours, red = another) | host |
		// rank badge, MMR, character | seats (green free / red full) | JOIN (green) or FULL (red outline)
		auto* row = card("inner");
		row->setFixedHeight(52);
		auto* r = new QHBoxLayout(row);
		r->setContentsMargins(16, 0, 16, 0);
		r->setSpacing(0);
		const bool ranked = l["ranked"].toBool();
		const QColor kindColor = ranked ? QColor(0xEF, 0xBC, 0x23) : QColor(0xAC, 0xAC, 0xAC);
		auto* kindIcon = new QLabel;
		kindIcon->setPixmap(tinted(ranked ? "trophy" : "account_circle", kindColor, 16));
		r->addWidget(kindIcon);
		r->addSpacing(6);
		auto* kind = new QLabel(ranked ? T("Ranked") : T("Casual"));
		kind->setStyleSheet(QString("color:%1; font-size:14px; font-weight:500; letter-spacing:0.3px;").arg(kindColor.name()));
		kind->setFixedWidth(62);
		r->addWidget(kind);
		r->addSpacing(16);
		const QString reg = host["region"].toString();
		auto* region = new QLabel(reg);
		region->setObjectName(reg == m_region ? "region" : "regionFar");
		region->setAlignment(Qt::AlignCenter);
		// every column the same width in every row (user 2026-10-18: "vsechno ve stejnych sloupcich")
		region->setFixedSize(54, 25);
		r->addWidget(region);
		// (only after it is in the row: a widget without a parent made visible is a window of its own - it flashed up
		//  as a small empty window at every refresh of the list)
		region->setVisible(!reg.isEmpty());
		r->addSpacing(16);
		auto* nm = new QLabel(host["name"].toString());
		nm->setObjectName("lobbyName");
		nm->setFixedWidth(152);
		r->addWidget(nm);
		r->addSpacing(16);
		auto* hostRank = new QLabel;
		hostRank->setPixmap(rankIcon(host["rank"].toInt(-1), 28));
		hostRank->setToolTip(host["rank_name"].toString());
		hostRank->setFixedWidth(64);
		hostRank->setAlignment(Qt::AlignCenter);
		r->addWidget(hostRank);
		r->addSpacing(8);
		auto* elo = new QLabel(QString::number(host["elo"].toInt()));
		elo->setFixedWidth(44);
		elo->setStyleSheet(QString("color:%1; font-size:14px; font-weight:500;").arg(ranked ? "#EFBC23" : "#ACACAC"));
		elo->setToolTip(T("MMR"));
		r->addWidget(elo);
		r->addSpacing(8);
		auto* ch = new QLabel(charName(host["char"].toInt()));
		ch->setObjectName("lobbyChar");
		ch->setFixedWidth(150);
		r->addWidget(ch);
		// the opponent of a full lobby, then PRIVATE: fixed columns too (empty when there is nothing to show)
		const QJsonObject guest = l["guest"].toObject();
		auto* vs = new QLabel(!open && !guest.isEmpty() ? T("vs %1").arg(guest["name"].toString()) : QString());
		vs->setObjectName("lobbyChar");
		r->addWidget(vs, 1);
		const bool priv = l["private"].toBool();  // (a private lobby: joining asks for its code, user 2026-10-15)
		{
			auto* cell = new QWidget;
			cell->setFixedWidth(84);
			auto* cl = new QHBoxLayout(cell);
			cl->setContentsMargins(0, 0, 0, 0);
			if (priv)
				cl->addWidget(sticker(T("PRIVATE"), "sticker"), 0, Qt::AlignCenter);
			r->addWidget(cell);
			r->addSpacing(12);
		}
		// seats: the host + the guest, 12 px people
		auto* seats = new QLabel;
		{
			const QColor sc = open ? QColor(0x0A, 0x95, 0x06) : QColor(0xE8, 0x32, 0x1C);
			QPixmap pm(QSize(26, 12) * 2);
			pm.setDevicePixelRatio(2.0);
			pm.fill(Qt::transparent);
			QPainter sp(&pm);
			sp.drawPixmap(0, 0, tinted("person", sc, 12));
			sp.drawPixmap(14, 0, tinted(open && guest.isEmpty() ? "person_outline" : "person", sc, 12));
			sp.end();
			seats->setPixmap(pm);
			seats->setToolTip(open && guest.isEmpty() ? T("1 / 2 players") : T("2 / 2 players"));
		}
		r->addWidget(seats);
		r->addSpacing(12);
		if (!open)
		{
			// full / playing: no Join
			auto* full = new QLabel(l["state"].toString() == "playing" ? T("PLAYING") : T("FULL"));
			full->setObjectName("full");
			full->setFixedSize(76, 25);
			full->setAlignment(Qt::AlignCenter);
			r->addWidget(full);
			m_listBox->addWidget(row);
			continue;
		}
		auto* b = button(T("Join"), "join");
		b->setFixedSize(76, 25);
		const QString id = l["id"].toString();
		b->setEnabled(m_online && !m_inLobby && !m_inMatch && !m_queued && m_gameReady);
		connect(b, &QPushButton::clicked, this, [this, id, priv, host] {
			if (!priv)
			{
				send({{"t", "join"}, {"id", id}});
				return;
			}
			bool ok = false;
			const QString code = QInputDialog::getText(this, T("Private lobby"),
			                                           T("%1's lobby is private. Enter its code:").arg(host["name"].toString()),
			                                           QLineEdit::Normal, QString(), &ok).trimmed().toUpper();
			if (ok && !code.isEmpty())
				send({{"t", "join"}, {"id", id}, {"code", code}});
		});
		r->addWidget(b);
		m_listBox->addWidget(row);
	}
	m_listBox->addStretch();
}

void Launcher::refreshVs()
{
	if (!m_vs)
		return;
	QJsonObject p1, p2;
	QString top, code, info;
	// (m_rematchFlow: from the rematch start to the game's window - the match card stays, never the lobby view while
	// the game parks and the next start runs, user 2026-10-12)
	if (m_punching || m_inMatch || m_rematchFlow || !m_resultText.isEmpty())
	{
		p1 = m_match["p1"].toObject();
		p2 = m_match["p2"].toObject();
		top = !m_resultText.isEmpty() ? m_resultText : m_rematchFlow ? T("Rematch") : m_match["ranked"].toBool() ? T("Ranked match") : T("Match");
		info = m_rematchFlow ? T("The rematch is starting…") : m_matchDetail;
		if (!m_ratedText.isEmpty())
			info += "   ·   " + m_ratedText;
		if (m_peerWifi || m_peerLinux)
			info += "   ·   " + (m_peerWifi && m_peerLinux ? T("Opponent is on Wi-Fi and Linux, the match might lag")
			                        : m_peerWifi             ? T("Opponent is on Wi-Fi, the match might lag")
			                                                 : T("Opponent is on Linux, the match might lag"));
	}
	else if (m_inLobby)
	{
		p1 = m_lobby["host"].toObject();
		p2 = m_lobby["guest"].toObject();
		top = m_lobby["ranked"].toBool() ? T("Ranked match") : m_lobby["public"].toBool() ? T("Public lobby") : T("Private lobby");
		code = m_lobby["code"].toString();
		info = m_rematch    ? T("Rematch: preparing the game…")
		       : p2.isEmpty() ? T("Send the code to a friend or wait for someone from the list.")
		                    : (m_ready ? T("Ready, waiting for the opponent.")
		                               : T("The match starts when both are ready."));
		if (!p2.isEmpty() && !m_rematch && m_lobbyPing != -1)
			info += QString("  ·  ") + (m_lobbyPing >= 0    ? T("ping %1 ms").arg(m_lobbyPing)
			                             : m_lobbyPing == -2 ? T("measuring ping…")
			                                                 : T("ping not measured (no direct connection yet)"));
	}
	// matchmaking: who the opponent is shows only once both accepted (user 2026-10-12: no dodging by name)
	// ... and the sides too (user 2026-10-16: "lidi schvalne neprijimaji P2 matche"): this player on the left, no PLAYER 1
	// / PLAYER 2 sticker, until both accepted ('go'); the game then plays the real sides
	const bool meLeft = m_hideOpp || m_role != 2;
	if (m_hideOpp)
	{
		if (m_role == 2)
			std::swap(p1, p2);
		p2 = QJsonObject{{"hidden", true}};
	}
	// who is ready: the accept of a found match, or Ready / Rematch / Next game in the lobby until the next match starts
	// (the opponent's from the server's lobby info - a server before 2026-10-14 sends none: no frame on that side)
	if (m_confirming || (m_inLobby && !m_punching && !m_rematchFlow && (!m_inMatch || m_resultShown)))
	{
		bool oppReady = m_oppAccepted;
		if (!m_confirming)
		{
			const QJsonObject h = m_lobby["host"].toObject(), g = m_lobby["guest"].toObject();
			oppReady = (h["name"].toString() == m_name ? g : h)["ready"].toBool();
		}
		QJsonObject& me = meLeft ? p1 : p2;
		QJsonObject& opp = meLeft ? p2 : p1;
		if (!me.isEmpty())
			me["ready"] = m_confirming ? m_accepted : m_ready;
		if (!opp.isEmpty())
			opp["ready"] = oppReady;
	}
	if (m_confirming)
	{
		const int left = qMax(0, int((m_confirmUntil - m_confirmClock.elapsed()) / 1000));
		info = m_accepted ? T("Ping %1 ms  ·  accepted, waiting for the opponent…").arg(m_confirmPing)
		                  : T("Opponent found  ·  ping %1 ms  ·  accept within %2 s").arg(m_confirmPing).arg(left);
	}
	m_vs->set(p1, p2, top, m_hideOpp);
	m_vsCode->setText(code);
	m_vsCode->setVisible(!code.isEmpty());
	m_vsCodeLabel->setVisible(!code.isEmpty());
	m_vsInfo->setText(info);
	{
		// (a server before 2.0.3 has no 'stage' in its lobby info: no choice)
		const bool pick = m_inLobby && m_lobby.contains("stage") && !m_lobby["queue"].toBool() && !m_lobby["ranked"].toBool() &&
		                  !m_punching && !m_rematchFlow && !m_confirming && (!m_inMatch || m_resultShown);
		m_stageLabel->setVisible(pick);
		m_stageBox->setVisible(pick);
		m_stageBox->setEnabled(m_lobby["host"].toObject()["name"].toString() == m_name && !m_ready && !m_rematchWanted);
		if (pick && !m_stageBox->view()->isVisible())
			m_stageBox->setCurrentIndex(qMax(0, m_stageBox->findData(m_lobby["stage"].toInt(-1))));
	}
	m_vsInfo->setObjectName(m_matchErr ? "err" : "muted");
	m_vsInfo->style()->unpolish(m_vsInfo);
	m_vsInfo->style()->polish(m_vsInfo);
	// the loser of the last game may take the next one to another (random) stage: ranked sets, and since 2.1.3 player
	// matches and lobbies whose stage is Random (not when the lobby's host picked the stage)
	const bool randomStageLobby = m_setRunning || m_lobby["stage"].toInt(-1) < 0;
	m_randBtn->setText(m_setRunning ? T("Next game – random stage  (Y / △)") : T("Rematch – random stage  (Y / △)"));
	m_randBtn->setVisible(m_inMatch && randomStageLobby && !m_setOver && !m_confirming && m_resultShown && m_lostGame &&
	                      !m_rematchWanted && !m_awaitRematch && !m_rematch);
	if (m_rematchFlow && !m_inMatch)
	{
		m_readyBtn->setText(T("Rematch ✓"));
		m_readyBtn->setEnabled(false);
		m_leaveBtn->setEnabled(false);
	}
	else if (m_confirming)
	{
		m_readyBtn->setText(m_accepted ? T("Accepted ✓") : T("Accept  (A / ✕)"));
		m_readyBtn->setEnabled(!m_accepted);
		m_leaveBtn->setText(T("Decline  (B / ○)"));
		m_leaveBtn->setEnabled(!m_accepted);
	}
	else if (m_inMatch && m_setOver)
	{
		m_readyBtn->setText(m_slowEnd ? T("End match  (A / ✕)") : T("Back to ranked  (A / ✕)"));
		m_readyBtn->setEnabled(m_resultShown);
		m_leaveBtn->setEnabled(false);
	}
	else if (m_inMatch)
	{
		m_readyBtn->setText(T("Rematch"));
		m_readyBtn->setText(m_setRunning ? (m_rematchWanted ? T("Next game ✓") : T("Next game  (A / ✕)")) : m_rematchWanted ? T("Rematch ✓") : T("Rematch  (A / ✕)"));
		if (m_setRunning && nextLeft() >= 0)
			m_readyBtn->setText(m_readyBtn->text() + QString("  ·  %1 s").arg(nextLeft()));
		m_readyBtn->setEnabled(m_resultShown && !m_rematchWanted && !m_awaitRematch);
		m_leaveBtn->setText(m_setRunning ? (m_lagFree ? T("Leave (no penalty)") : T("Leave (lose the set)")) : T("Quit game"));
		m_leaveBtn->setEnabled(true);
	}
	else if (m_punching)
	{
		m_readyBtn->setText(T("Connecting…"));
		m_readyBtn->setEnabled(false);
		m_leaveBtn->setEnabled(false);
	}
	else if (!m_resultText.isEmpty())
	{
		m_readyBtn->setText(T("Back"));
		m_readyBtn->setEnabled(true);
		m_leaveBtn->setEnabled(false);
	}
	else
	{
		m_readyBtn->setText(m_ready ? T("Ready ✓") : T("Ready"));
		m_readyBtn->setEnabled(!p2.isEmpty() && !m_ready && !m_rematch && m_gameReady);
		m_leaveBtn->setText(T("Leave"));
		m_leaveBtn->setEnabled(true);
	}
	// Block player: after a game (the result is shown); in a running ranked set blocking leaves it like Leave
	const bool canBlock = m_inMatch && m_resultShown && !m_confirming;
	if (!canBlock)
		m_blockArmed = false;
	m_blockBtn->setVisible(canBlock);
	m_blockBtn->setText(m_blockArmed ? T("Click again to block")
	                    : m_setRunning && !m_setOver && !m_lagFree ? T("Block player (lose the set)")
	                                                               : T("Block player"));
	m_blockBtn->setToolTip(T("You will not be matched with this player again (ranked and player match). "
	                         "Profile -> BLOCKED to unblock."));
	// rank up of the last rated set
	m_rankBanner->setVisible(m_inMatch && m_rankDir != 0 && m_rankNew >= 0);
	if (m_rankBanner->isVisibleTo(m_rankBanner->parentWidget()))
	{
		m_rankIconL->setPixmap(rankIcon(m_rankNew, 64));
		m_rankTextL->setText(QString("<span style='font-size:22px; font-weight:700; color:%1;'>%2</span>"
		                             "<span style='font-size:22px; font-weight:700; color:#E8E8F2;'>&nbsp;&nbsp;%3</span>")
		                         .arg(m_rankDir > 0 ? "#EFBC23" : "#FF6B5C")
		                         .arg(m_rankDir > 0 ? T("RANK UP") : T("RANK DOWN"))
		                         .arg(m_rankNames.at(m_rankNew).toString().toHtmlEscaped()));
	}
}

void Launcher::readyAction()
{
	if (m_confirming)
	{
		if (!m_accepted)
		{
			m_accepted = true;
			send({{"t", "accept"}, {"match_id", m_match["match_id"].toString()}, {"yes", true}});
			refreshVs();
		}
		return;
	}
	if (m_inMatch && m_setOver)
	{
		parkGame();  // the game waits for the next match; the player searches again on RANKED
		selectMode(m_slowEnd ? m_lastMode : PageRanked);  // (below 60 FPS: back where the match came from)
		return;
	}
	if (m_inMatch)
	{
		// Rematch: this side is ready again; when the opponent does the same, the server starts a new match in the same
		// lobby and the running session plays it (startMatch). After a disconnect the session is gone: the game is
		// closed, the next one started, and this side is ready once it is parked.
		if (m_game && !m_disconnected)
		{
			m_rematchWanted = true;
			setReady();
			return;
		}
		m_rematch = true;
		command("quit");
		if (m_game)
			killGame("rematch after a disconnect");
	}
	else if (!m_resultText.isEmpty())
	{
		m_resultText.clear();
		m_matchErr = false;
		selectMode(m_lastMode);
	}
	else
		setReady();
}

// After a match the game is NOT closed (user 2026-10-11: "hra se nema vypinat a zapinat u kazdeho matche"): 'park'
// makes it leave its result menu by itself and wait at the stage select again ('state ready', ~4 s); the next
// match - any opponent - gets a 'start' line as before. Only from the result menu; a game that is somewhere else
// (disconnect in the middle of a fight, failed start) is closed and started again as before.
void Launcher::parkGame()
{
	command("park");
	showGameWindows(false);
	m_inMatch = m_gameReady = m_pendingStart = m_startFailed = m_rematchWanted = m_awaitRematch = m_disconnected = m_showGame = false;
	m_hadResult = m_resultShown = m_reported = m_rematch = m_setOver = m_slowEnd = m_lagFree = false;
	m_rankDir = 0;
	m_blockArmed = false;
	m_resultText.clear();
	m_matchErr = false;
	m_matchDetail.clear();
	m_autoQuickDone = false;
	m_parkTimer.start();
	m_parking = true;
	m_oppLeft = false;
	if (m_lobby["queue"].toBool())
		m_searchAgain.start();  // back on RANKED / PLAYER MATCH: the controller may press Search again
	send({{"t", "leave"}});
	refreshAll();
}

void Launcher::leaveAction()
{
	if (m_confirming)
	{
		if (!m_accepted)
			send({{"t", "accept"}, {"match_id", m_match["match_id"].toString()}, {"yes", false}});
		return;
	}
	if (m_inMatch && m_game && m_resultShown)
		parkGame();
	else if (m_inMatch)
	{
		command("quit");
		if (m_game)
			killGame("leaving the match");
	}
	else
	{
		m_rematch = false;
		send({{"t", "leave"}});
	}
}
