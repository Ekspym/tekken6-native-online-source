// Tekken 6 Native Online launcher - Practice.

#include "launcher.h"

// ================= PRACTICE: a second instance of the game in Practice (own character) while the player waits for a
// match; the parked online game stays as it is. It cannot be left (no START / BACK in it) and ends when a match starts.
// Practice on the RANKED page searches a ranked match, on PLAYER MATCH a player match, by itself once the practice
// shows and the online game is ready (user 2026-10-12); a search the player cancels is not started again
// search: 2 ranked, 1 player match, 0 none (lobby: the match starts when both are Ready)
// PRACTICE page: the practice alone, with a character of the player's choice on each side (no search starts). The
// choice is this page's own - the character of the profile (online matches) stays.
QWidget* Launcher::buildPractice()
{
	auto* c = card();
	auto* v = new QVBoxLayout(c);
	v->setContentsMargins(32, 28, 32, 30);
	v->addWidget(text(T("Practice"), "h1"));
	v->addWidget(text(T("Train alone against a standing opponent. Choose both characters and the stage, then start.")));
	v->addSpacing(18);
	auto s = settings();
	m_soloChar = s.value("practice_char", -1).toInt();
	m_soloOpp = s.value("practice_opp", -1).toInt();
	const QList<int> order = charsByName();
	const auto picker = [&](const QString& label, int* value, const char* key) {
		auto* col = new QVBoxLayout;
		col->addWidget(text(label, "gold"));
		auto* box = new QComboBox;
		for (const int i : order)
			box->addItem(kChars[i], i);
		const int cur = *value >= 0 && *value < kCharCount && !charHidden(*value) ? *value : m_char;
		box->setCurrentIndex(qMax(0, box->findData(cur)));
		*value = box->currentData().toInt();
		connect(box, &QComboBox::activated, this, [this, box, value, key] {
			*value = box->currentData().toInt();
			settings().setValue(key, *value);
			if (m_practice && m_practiceSolo)  // the game takes the characters at its start
			{
				stopPractice();
				m_practiceSolo = true;
				startPractice();
			}
		});
		col->addWidget(box);
		return col;
	};
	auto* row = new QHBoxLayout;
	row->addLayout(picker(T("Your character"), &m_soloChar, "practice_char"));
	row->addSpacing(24);
	row->addLayout(picker(T("Opponent"), &m_soloOpp, "practice_opp"));
	row->addSpacing(24);
	{
		auto* col = new QVBoxLayout;
		col->addWidget(text(T("Stage"), "gold"));
		auto* box = new QComboBox;
		box->addItem(T("Random"), -1);
		for (const auto& [id, name] : kStages)
			box->addItem(name, id);
		m_soloStage = s.value("practice_stage", -1).toInt();
		box->setCurrentIndex(qMax(0, box->findData(m_soloStage)));
		m_soloStage = box->currentData().toInt();
		connect(box, &QComboBox::activated, this, [this, box] {
			m_soloStage = box->currentData().toInt();
			settings().setValue("practice_stage", m_soloStage);
			if (m_practice && m_practiceSolo)  // the game takes the stage at its start
			{
				stopPractice();
				m_practiceSolo = true;
				startPractice();
			}
		});
		col->addWidget(box);
		row->addLayout(col);
	}
	row->addStretch();
	v->addLayout(row);
	v->addSpacing(22);
	auto* go = new QHBoxLayout;
	m_soloBtn = button(T("Start practice"), "cta");
	m_soloBtn->setMinimumWidth(260);
	connect(m_soloBtn, &QPushButton::clicked, this, [this] {
		if (m_practice)
			stopPractice();
		else
		{
			m_practiceSolo = true;
			m_practiceSearch = 0;
			startPractice();
		}
	});
	go->addWidget(m_soloBtn);
	go->addSpacing(18);
	// frame data in the practice (drawn by the game, T6_FRAME_DATA): startup of your attack and the advantage on hit
	auto* fd = new QCheckBox(T("Show frame data (startup, advantage on hit and on block)"));
	fd->setChecked(settings().value("practice_frame_data", true).toBool());
	connect(fd, &QCheckBox::toggled, this, [this](bool on) {
		settings().setValue("practice_frame_data", on);
		if (m_practice)  // the game reads it at its start
		{
			const bool solo = m_practiceSolo;
			stopPractice();
			m_practiceSolo = solo;
			startPractice();
		}
	});
	go->addWidget(fd);
	go->addStretch();
	v->addLayout(go);
	v->addSpacing(10);
	v->addWidget(text(T("The practice ends when an online match starts. To practice while you wait for a match, use \"Practice & search\" on the RANKED or CASUAL page.")));
	v->addStretch();
	return c;
}

// the online game is starting in the background and nothing can be searched / started yet
bool Launcher::preparing() const
{
	return gameBuilt() && gameUsable() && !m_autoBuild && !m_bootError && !m_gameReady && !m_inMatch && !m_punching;
}

QPushButton* Launcher::practiceButton(int search)
{
	auto* b = button(T("Practice && search"), "ghost", "accessibility_new");
	connect(b, &QPushButton::clicked, this, [this, search] {
		if (m_practice)
			stopPractice();
		else
		{
			m_practiceSolo = false;
			m_practiceSearch = search == 2 && m_guest ? 1 : search;
			startPractice();
		}
	});
	if (search == 0)
		m_practiceLobbyBtns.push_back(b);
	m_practiceBtns.push_back(b);
	return b;
}

void Launcher::refreshPractice()
{
	for (QPushButton* b : m_practiceBtns)
	{
		// the game loads hidden (~40 s): the button says so, a click still cancels it
		b->setText(m_practice ? (m_practiceShown ? T("Stop practice") : T("Loading practice…"))
		           : preparing() ? T("Preparing the game…") : T("Practice && search"));
		b->setEnabled(m_practice || (gameUsable() && m_gameReady && !m_inMatch && !m_punching && !m_autoBuild && !m_customize));
	}
	if (m_soloBtn)
	{
		m_soloBtn->setText(m_practice ? (m_practiceShown ? T("Stop practice") : T("Loading practice…"))
		                   : preparing() ? T("Preparing the game…") : T("Start practice"));
		m_soloBtn->setEnabled(m_practice || (gameUsable() && m_gameReady && !m_inMatch && !m_punching && !m_autoBuild));
	}
	// the lobby's button (VS page): only while waiting in a lobby, not during / after a match
	for (QPushButton* b : m_practiceLobbyBtns)
		b->setVisible(m_inLobby && !m_inMatch && !m_punching);
}

void Launcher::startPractice()
{
	// (not while the online game is still starting: two games loading at once)
	if (m_practice || m_customize || !gameUsable() || !m_gameReady || m_inMatch || m_punching)
		return;
	if (m_controlsDirty)
	{
		m_controlsDirty = false;
		restartParkedGame();  // the practice below starts with the new settings anyway
	}
	const QString runDir = rootFile(QString("runs/practice_%1").arg(g_instance));
	QDir().mkpath(runDir);
	auto env = gameEnvironment();
	env.insert("T6_PRACTICE", "1");
	m_practiceStatus = QDir(runDir).filePath("status.txt");
	QFile::remove(m_practiceStatus);
	env.insert("T6_NET_STATUS", QDir::toNativeSeparators(m_practiceStatus));
	// the menu path of the online game up to the Offline menu; the game goes on to PRACTICE by itself
	env.insert("T6_PLAY", QDir::toNativeSeparators(rootFile("tools/menu_vs.bin")));
	env.insert("T6_PLAY_UNTIL", "1630");
	env.insert("T6_FIX_SEED", "1");
	env.insert("T6_VIRTUAL_TB", "1");
	{
		// the player's costume; the dummy in P1
		const int c1 = m_practiceSolo ? m_soloChar : m_char, c2 = m_practiceSolo ? m_soloOpp : m_char;
		const auto [code1, code2] = matchCodes(c1, costume(c1), c2, 0);
		env.insert("T6_CHAR_P1", QString::number(code1));
		env.insert("T6_CHAR_P2", QString::number(code2));
		if (const QString rec = customRecord(c1, costume(c1)); validRecord(rec))  // the player's custom look
			env.insert("T6_CUSTOM_P1", rec);
	}
	if (settings().value("practice_frame_data", true).toBool())
		env.insert("T6_FRAME_DATA", "1");
	if (m_practiceSolo)  // (Random: one of the 18 - the game's own practice stage is always the same)
		env.insert("T6_STAGE", QString::number(m_soloStage >= 0 ? m_soloStage
		                                       : kStages[QRandomGenerator::global()->bounded(int(std::size(kStages)))].first));
	env.insert("T6_NET_KEYS", QString::number(m_keys));
	addGameEnv(env, true);
	const QString user = QDir(runDir).filePath("user");
	freshProfile(user);
	addShaderCache(runDir);
	m_practice = new QProcess(this);
	m_practice->setProcessEnvironment(env);
	m_practice->setWorkingDirectory(QFileInfo(gameExe()).absolutePath());
	m_practice->setProgram(gameExe());
	m_practice->setArguments(gameArgs(user, runDir));
	// boot and menus stay hidden like the online game: the window shows with the loading of the practice battle
	m_practice->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* a) {
		a->startupInfo->dwFlags |= STARTF_USESHOWWINDOW;
		a->startupInfo->wShowWindow = SW_HIDE;
	});
	m_practiceShown = false;
	connect(m_practice, &QProcess::finished, this, [this](int code) {
		if (!m_practice)
			return;
		testLog(QString("practice finished by itself: code %1 status %2").arg(code).arg(int(m_practice->exitStatus())));
		m_practicePoll.stop();
		m_practice->deleteLater();
		m_practice = nullptr;
		refreshPractice();
	});
	m_practicePoll.disconnect();
	connect(&m_practicePoll, &QTimer::timeout, this, [this] { pollPractice(); });
	m_practicePoll.start(100);
	m_practice->start();
	endWithLauncher(m_practice->processId());
	refreshPractice();
}

// hidden until the battle loads (mode 14) or runs; then shown once, in front
void Launcher::pollPractice()
{
	if (!m_practice || !m_practice->processId() || m_practiceShown)
		return;
	const DWORD pid = static_cast<DWORD>(m_practice->processId());
	QFile f(m_practiceStatus);
	QString state, mode;
	if (f.open(QIODevice::ReadOnly | QIODevice::Text))
		for (const QString& line : QString::fromUtf8(f.readAll()).split('\n'))
		{
			if (line.startsWith("state "))
				state = line.mid(6).trimmed();
			else if (line.startsWith("mode "))
				mode = line.mid(5).trimmed();
		}
	const bool battle = mode == "14" || (!state.isEmpty() && state != "loading" && state != "other" && state != "menu");
	if (!battle)
	{
		showWindowsOf(pid, false, m_fullscreen);
		return;
	}
	m_practiceShown = true;
	m_practicePoll.stop();
	showWindowsOf(pid, true, m_fullscreen);
	refreshPractice();
	practiceSearch();
}

// the search the practice button asked for, once the practice is up and the online game stands ready
void Launcher::practiceSearch()
{
	if (!m_practiceSearch || !m_practice || !m_practiceShown)
		return;
	const bool idle = m_online && !m_inLobby && !m_inMatch && !m_punching && m_gameReady && !m_queued;
	if (!idle)
		return;
	send({{"t", "quick"}, {"ranked", m_practiceSearch == 2}});
	m_practiceSearch = 0;
}

void Launcher::stopPractice()
{
	m_practiceSearch = 0;
	m_practicePoll.stop();
	if (!m_practice)
		return;
	QProcess* p = m_practice;
	m_practice = nullptr;
	testLog("practice stopped by the launcher");
	p->disconnect();
	p->kill();
	p->waitForFinished(3000);
	p->deleteLater();
	refreshPractice();
}
