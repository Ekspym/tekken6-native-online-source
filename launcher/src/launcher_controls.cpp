// Tekken 6 Native Online launcher - Controller and keyboard settings, the controller on the launcher's pages.

#include "launcher.h"

// ================= CONTROLS
// Settings -> CONTROLLER (which controller, vibration; a stick: its device and which of its buttons is which
// controller button) and BUTTON MAPPING (what each controller button does in the game)
void Launcher::buildControls(QVBoxLayout* setup, QVBoxLayout* v)
{
	auto s = settings();
	auto* backend = choiceBox({{T("Gamepad – Xbox, DualShock 4, DualSense, Switch Pro…"), "sdl"},
	                           {T("Joystick / arcade stick (WinMM, its buttons set below)"), "mmjoystick"}},
	                          padBackend());
	setup->addLayout(settingRow(T("Controller"), {backend, hintText(T("Playing on an arcade stick and your inputs don't work? Try the Gamepad setting for now."))}));
	setup->addLayout(settingRow(T("Vibration"), {gameToggle(&m_vibration, "vibration")}));  // (user 2026-10-12)

	// WinMM joystick: the device + one binding per controller button, set by pressing it
	auto* mm = new QWidget;
	auto* mv = new QVBoxLayout(mm);
	mv->setContentsMargins(0, 0, 0, 0);
	mv->setSpacing(10);
	auto* mmBox = new QComboBox;
	styleField(mmBox);
	const auto devs = mmDevices();
	for (const auto& [i, name] : devs)
		mmBox->addItem(QString("%1  %2").arg(i + 1).arg(name), i + 1);
	if (devs.isEmpty())
		mmBox->addItem(T("No joystick found – connect it and open this tab again"), s.value("mm_device", 1).toInt());
	mmBox->setCurrentIndex(qMax(0, mmBox->findData(s.value("mm_device", 1).toInt())));
	connect(mmBox, &QComboBox::currentIndexChanged, this, [this, mmBox] {
		settings().setValue("mm_device", mmBox->currentData().toInt());
		controlsChanged();
	});
	mv->addLayout(settingRow(T("Device"), {mmBox}));
	mv->addSpacing(8);
	mv->addWidget(text(T("Controller buttons"), "h3"));
	mv->addWidget(text(T("Which button of your stick is which controller button: click a field, then press the button / "
	                     "direction on the stick (5 s).")));
	auto* pad = new PadPanel;
	for (const MmTarget& t : kMmTargets)
	{
		auto* b = keyButton(mmBinding(t.key, t.def));
		const QString key = t.key, def = t.def;
		connect(b, &QPushButton::clicked, this, [this, b, key, def, mmBox] { captureMm(b, key, def, mmBox->currentData().toInt() - 1); });
		pad->add(t.key, T(t.label), b);
	}
	mv->addWidget(pad);
	setup->addWidget(mm);
	setup->addStretch();
	mm->setVisible(padBackend() == "mmjoystick");
	connect(backend, &QComboBox::currentIndexChanged, this, [this, backend, mm] {
		settings().setValue("pad_backend", backend->currentData().toString());
		mm->setVisible(padBackend() == "mmjoystick");
		controlsChanged();
	});

	// BUTTON MAPPING: what each controller button does in the game
	v->addWidget(text(T("What each controller button does in the game. Default = what it does in the game anyway (X 1, Y 2, A 3, B 4).")));
	auto* pad2 = new PadPanel;
	QList<QComboBox*> boxes;
	for (const PadButton& pb : kPadButtons)
	{
		auto* box = new QComboBox;
		styleField(box);
		for (const PadAction& a : kPadActions)
			box->addItem(T(a.label), a.value);
		box->setCurrentIndex(qMax(0, box->findData(s.value(QString("pad_%1").arg(pb.key), -1).toInt())));
		const QString key = pb.key;
		connect(box, &QComboBox::currentIndexChanged, this, [this, box, key] {
			settings().setValue("pad_" + key, box->currentData().toInt());
			controlsChanged();
		});
		boxes.push_back(box);
		pad2->add(pb.key, T(pb.label), box);
	}
	{
		auto* dpad = new QLabel(T("D-pad / left stick:\nmovement"), pad2);  // (where the D-pad sits; nothing to map)
		dpad->setObjectName("muted");
		dpad->move(60, 255);
	}
	v->addWidget(pad2);
	v->addSpacing(8);
	auto* reset = button(T("Reset"), "ghost");
	connect(reset, &QPushButton::clicked, this, [boxes] {
		for (QComboBox* b : boxes)
			b->setCurrentIndex(0);
	});
	auto* save = button(T("Save"), "cta");
	connect(save, &QPushButton::clicked, this, [this] { applyControls(); });
	auto* rr = new QHBoxLayout;
	rr->setSpacing(10);
	rr->addWidget(save);
	rr->addWidget(reset);
	rr->addSpacing(4);
	rr->addWidget(text(T("Save applies the changes to the online game and the practice (also when you leave the settings).")));
	rr->addStretch();
	v->addLayout(rr);
	v->addStretch();
}

QGridLayout* Launcher::pairGrid()
{
	auto* g = new QGridLayout;
	g->setHorizontalSpacing(12);
	g->setVerticalSpacing(8);
	for (int c = 1; c < kPairsPerRow; c++)
		g->setColumnMinimumWidth(c * 3 - 1, 24);  // (a gap column between the pairs)
	g->setColumnStretch(kPairsPerRow * 3, 1);
	return g;
}

void Launcher::addPair(QGridLayout* g, int n, const QString& label, QWidget* field)
{
	auto* l = text(label);
	l->setMinimumWidth(110);
	g->addWidget(l, n / kPairsPerRow, (n % kPairsPerRow) * 3);
	g->addWidget(field, n / kPairsPerRow, (n % kPairsPerRow) * 3 + 1);
}

QString Launcher::padBackend() const
{
	return settings().value("pad_backend", "mmjoystick").toString() == "sdl" ? "sdl" : "mmjoystick";
}

QString Launcher::mmBinding(const QString& key, const QString& def)
{
	return settings().value("mm_" + key, def).toString();
}

// first press on the joystick within 5 s: a button, a POV direction or an axis half past 75 %
void Launcher::captureMm(QPushButton* b, const QString& key, const QString& def, int dev)
{
	if (m_capture)
		return;
	JOYINFOEX base{sizeof(base), JOY_RETURNALL};
	JOYCAPSW caps{};
	if (dev < 0 || joyGetPosEx(dev, &base) != JOYERR_NOERROR || joyGetDevCapsW(dev, &caps, sizeof(caps)) != JOYERR_NOERROR)
	{
		b->setText(T("no joystick"));
		QTimer::singleShot(1500, b, [b, key, def] { b->setText(mmBinding(key, def)); });
		return;
	}
	b->setText(T("press…"));
	m_capture = new QTimer(this);
	auto* t = m_capture;
	auto start = std::make_shared<QElapsedTimer>();
	start->start();
	const QPointer<QPushButton> bp(b);  // (the window may be built again while waiting)
	connect(t, &QTimer::timeout, this, [this, t, bp, key, def, dev, base, caps, start] {
		JOYINFOEX ji{sizeof(ji), JOY_RETURNALL};
		QString got;
		if (joyGetPosEx(dev, &ji) == JOYERR_NOERROR)
		{
			const DWORD fresh = ji.dwButtons & ~base.dwButtons;
			for (int i = 0; i < 32 && got.isEmpty(); i++)
				if (fresh & (1u << i))
					got = QString("Button %1").arg(i + 1);
			if (got.isEmpty() && ji.dwPOV != JOY_POVCENTERED && ji.dwPOV != base.dwPOV)
				got = ji.dwPOV == JOY_POVFORWARD ? "POV Up" : ji.dwPOV == JOY_POVBACKWARD ? "POV Down"
				    : ji.dwPOV == JOY_POVLEFT    ? "POV Left" : ji.dwPOV == JOY_POVRIGHT ? "POV Right" : QString();
			const struct { DWORD v, b, lo, hi; const char* name; } axes[] = {
				{ji.dwXpos, base.dwXpos, caps.wXmin, caps.wXmax, "X"}, {ji.dwYpos, base.dwYpos, caps.wYmin, caps.wYmax, "Y"},
				{ji.dwZpos, base.dwZpos, caps.wZmin, caps.wZmax, "Z"}, {ji.dwRpos, base.dwRpos, caps.wRmin, caps.wRmax, "R"},
				{ji.dwUpos, base.dwUpos, caps.wUmin, caps.wUmax, "U"}, {ji.dwVpos, base.dwVpos, caps.wVmin, caps.wVmax, "V"}};
			for (const auto& a : axes)
			{
				if (!got.isEmpty() || a.hi <= a.lo)
					continue;
				const double range = double(a.hi - a.lo), d = (double(a.v) - double(a.b)) / range;
				if (d > 0.4 && double(a.v - a.lo) / range > 0.75)
					got = QString("%1+").arg(a.name);
				else if (d < -0.4 && double(a.v - a.lo) / range < 0.25)
					got = QString("%1-").arg(a.name);
			}
		}
		if (got.isEmpty() && start->elapsed() < 5000)
			return;
		t->stop();
		t->deleteLater();
		m_capture = nullptr;
		if (!got.isEmpty())
		{
			settings().setValue("mm_" + key, got);
			controlsChanged();
		}
		if (bp)
			bp->setText(mmBinding(key, def));
	});
	t->start(20);
}

// next key press (anywhere in the launcher) becomes the key of this action; Escape keeps the old one
void Launcher::captureKey(QPushButton* b, const QString& key, int def)
{
	if (m_keyCapture)
		return;
	b->setText(T("press a key…"));
	m_keyCapture = new KeyGrab(this, [this, b = QPointer<QPushButton>(b), key, def](int vk) {
		if (vk && vk != VK_ESCAPE)
		{
			settings().setValue("kb_" + key, vk);
			controlsChanged();
		}
		if (b)
			b->setText(vkName(kbKey(key, def)));
		m_keyCapture->deleteLater();
		m_keyCapture = nullptr;
	});
	qApp->installEventFilter(m_keyCapture);
}

// controls file of the WinMM backend (all targets, the stick axes as the SDK default)
QString Launcher::writeMmConfig() const
{
	QString path = rootFile("controls_mmjoystick.ini");
	QFile f(path);
	if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
		return QString();
	QTextStream o(&f);
	o << "; written by the launcher (Settings -> CONTROLLER)\n";
	o << "Device = " << settings().value("mm_device", 1).toInt() << "\n";
	for (const MmTarget& t : kMmTargets)
		o << t.key << " = " << mmBinding(t.key, t.def) << "\n";
	o << "Guide = Button 13\nLeft Stick Left = X-\nLeft Stick Right = X+\nLeft Stick Down = Y+\nLeft Stick Up = Y-\n"
	     "Right Stick Left = Z-\nRight Stick Right = Z+\nRight Stick Down = R+\nRight Stick Up = R-\n";
	return path;
}

// T6_PAD_MAP of the game from the layout
QString Launcher::padMap() const
{
	QStringList parts;
	for (const PadButton& pb : kPadButtons)
	{
		const int v = settings().value(QString("pad_%1").arg(pb.key), -1).toInt();
		if (v >= 0)
			parts << QString("%1=%2").arg(pb.key).arg(v, 0, 16);
	}
	return parts.join(',');
}

// arguments / environment of a game instance (online and practice) for the controls
QStringList Launcher::inputArgs() const
{
	QStringList a{"--input_backend=" + padBackend()};
	if (padBackend() == "mmjoystick")
		if (const QString cfg = writeMmConfig(); !cfg.isEmpty())
			a << "--mmjoystick_config=" + QDir::toNativeSeparators(cfg);
	return a;
}

// A setting the game reads at its start changed (controls, display): applied once, when the player leaves the
// profile page or starts the practice - not on every pick (user: a timer of 1.5 s was too short)
void Launcher::controlsChanged()
{
	m_controlsDirty = true;
}

void Launcher::applyControls()
{
	if (!m_controlsDirty)
		return;
	m_controlsDirty = false;
	if (m_practice)
	{
		stopPractice();
		startPractice();
	}
	restartParkedGame();
}

bool Launcher::padConfirmDown() { return padDown(PadConfirm); }

bool Launcher::padCancelDown() { return padDown(PadCancel); }

bool Launcher::padStageDown() { return padDown(PadStage); }

bool Launcher::padDown(PadKind kind)
{
	// T6_TEST_PAD=1 (tests): a confirm button that is down for 1.5 s, up for 1.5 s, ...; T6_TEST_PAD_CANCEL=1 /
	// T6_TEST_PAD_STAGE=1 the same for cancel / stage
	const char* const test = kind == PadConfirm ? "T6_TEST_PAD" : kind == PadCancel ? "T6_TEST_PAD_CANCEL" : "T6_TEST_PAD_STAGE";
	if (qEnvironmentVariableIsSet(test))
		return (QDateTime::currentMSecsSinceEpoch() / 1500) % 2 == 1;
	if (kind != PadConfirm && qEnvironmentVariableIsSet("T6_TEST_PAD"))
		return false;
	// XInput, loaded at run time (no import: the launcher starts on a PC without it)
	struct XState { DWORD packet; WORD buttons; BYTE lt, rt; SHORT lx, ly, rx, ry; };
	using GetState = DWORD(WINAPI*)(DWORD, XState*);
	static const GetState xget = [] {
		HMODULE h = LoadLibraryW(L"xinput1_4.dll");
		if (!h)
			h = LoadLibraryW(L"xinput9_1_0.dll");
		return h ? reinterpret_cast<GetState>(GetProcAddress(h, "XInputGetState")) : nullptr;
	}();
	bool xpad = false;
	if (xget)
		for (DWORD i = 0; i < 4; i++)
		{
			XState st{};
			if (xget(i, &st) != ERROR_SUCCESS)
				continue;
			xpad = true;
			if (st.buttons & (kind == PadCancel ? 0x2000 : kind == PadStage ? 0x8000 : 0x1010))  // B / Y / A or START
				return true;
		}
	const bool mm = settings().value("pad_backend", "mmjoystick").toString() != "sdl";
	const auto down = [](UINT dev, int button) {
		JOYINFOEX ji{sizeof(ji), JOY_RETURNBUTTONS};
		return button >= 1 && button <= 32 && joyGetPosEx(dev, &ji) == JOYERR_NOERROR && (ji.dwButtons & (1u << (button - 1)));
	};
	if (mm)
	{
		// the buttons set as "A / Cross" and "Start" (cancel: "B / Circle", stage: "Y / Triangle") on CONTROLS
		const UINT dev = UINT(settings().value("mm_device", 1).toInt()) - 1;
		if (kind != PadConfirm)
		{
			const QString b = kind == PadCancel ? mmBinding("B", "Button 3") : mmBinding("Y", "Button 4");
			return b.startsWith("Button ") && down(dev, b.mid(7).toInt());
		}
		for (const auto& [key, def] : {std::pair<const char*, const char*>{"A", "Button 2"}, {"Start", "Button 10"}})
			if (const QString b = mmBinding(key, def); b.startsWith("Button ") && down(dev, b.mid(7).toInt()))
				return true;
		return false;
	}
	if (xpad)  // (an XInput pad is also a WinMM device, with B as its button 2)
		return false;
	// another pad (DualShock / DualSense: Cross is button 2; pads that report like an Xbox pad: A is button 1)
	// (cancel: Circle = button 3 only - B of an Xbox-like pad is button 2, the confirm of a DualShock; stage: button 4 =
	//  Triangle and Y alike)
	for (UINT i = 0; i < joyGetNumDevs() && i < 16; i++)
		if (kind == PadCancel ? down(i, 3) : kind == PadStage ? down(i, 4) : down(i, 1) || down(i, 2))
			return true;
	return false;
}

// Every 50 ms: what happens by itself after a match, and the controller's confirm button.
//  - Matches from the search (RANKED, PLAYER MATCH), user 2026-10-14: when they are over the player is taken out
//    of the lobby without a click - a ranked set that is decided (4 s after its result showed, so the result and
//    the rating can be read), and a match after which the opponent left (2 s). The game parks, the page of the
//    mode comes back, and there the controller's confirm button presses "Search again".
//    Lobbies somebody created stay as they are.
//  - The result page: the confirm button presses Rematch / Next game.
void Launcher::padPoll()
{
	if (!m_pages)
		return;
	// --- out of the lobby by itself
	// (m_oppLeft: the server said so - 'lobby' without a guest when the guest left, 'left' with reason host_left when
	//  the host left; in the second case this side is out of the server's lobby already and only the page is left.
	//  Test 2026-10-14: the side that stayed kept the result page with a Rematch button that could do nothing.)
	const bool queueLobby = m_lobby["queue"].toBool();
	const bool oppLeft = m_inMatch && !m_setOver && m_resultShown && m_oppLeft && (queueLobby || !m_inLobby) && m_game &&
	                     !m_confirming && m_pages->currentIndex() == PageVs;
	const bool setDone = m_inMatch && m_setOver && m_resultShown && m_pages->currentIndex() == PageVs;
	if (!(oppLeft || setDone))
		m_autoLeave.invalidate();
	else if (!m_autoLeave.isValid())
		m_autoLeave.start();
	else if (m_autoLeave.elapsed() > (setDone ? (m_slowEnd ? 12000 : 4000) : 2000))  // (below 60 FPS: time to read why)
	{
		m_autoLeave.invalidate();
		testLog(setDone ? "auto: back to ranked after the set" : "auto: out of the lobby, the opponent left");
		m_searchAgain.start();
		if (setDone)
			readyAction();  // = "Back to ranked"
		else
		{
			const bool inLobby = m_inLobby;
			m_searchNote = T("The opponent left.");
			parkGame();  // (sends 'leave'; the server's 'left' brings the page of the mode back)
			if (!inLobby)
				selectMode(m_lastMode);  // (the host left: no lobby on the server any more, no 'left' will come)
		}
		return;
	}
	// --- the cancel button (user 2026-10-16): 1 = Cancel search (launcher in front: the pad is read for every program),
	//     2 = Decline a found match. Never in the practice (B is a button of the fight there). Like confirm: it counts
	//     after it was up for 1 s since the choice appeared.
	{
		const int ct = m_practice ? 0 : (m_confirming && !m_accepted) ? 2 : (m_queued && !m_confirming && isActiveWindow()) ? 1 : 0;
		if (ct != m_padCancelTarget)
		{
			m_padCancelTarget = ct;
			m_padCancelArmed = false;
			m_padCancelSince.start();
		}
		if (ct)
		{
			if (!padCancelDown())
			{
				if (m_padCancelSince.elapsed() > 1000)
					m_padCancelArmed = true;
			}
			else if (m_padCancelArmed)
			{
				m_padCancelArmed = false;
				testLog(ct == 2 ? "pad decline: cancel pressed" : "pad cancel search: cancel pressed");
				if (ct == 2)
					leaveAction();  // (= "Decline")
				else
					send({{"t", "quick_cancel"}});  // (= "Cancel search", ranked and player match alike)
				return;
			}
		}
	}
	// --- Y / Triangle (user 2026-10-16): the loser's "Rematch / Next game – random stage" while it is shown on the result
	//     page; armed like confirm (up for 1 s since it appeared)
	{
		const bool st = m_randBtn && m_randBtn->isVisible() && m_randBtn->isEnabled() && m_inMatch && !m_setOver &&
		                !m_confirming && m_resultShown && m_pages->currentIndex() == PageVs;
		if (st != m_padStageOn)
		{
			m_padStageOn = st;
			m_padStageArmed = false;
			m_padStageSince.start();
		}
		if (st)
		{
			if (!padStageDown())
			{
				if (m_padStageSince.elapsed() > 1000)
					m_padStageArmed = true;
			}
			else if (m_padStageArmed)
			{
				m_padStageArmed = false;
				testLog("pad random stage: Y pressed");
				m_randBtn->click();
				return;
			}
		}
	}
	// --- the confirm button: 1 = Rematch / Next game on the result page, 2 = Search (again) on the mode's page,
	//     3 = Back to ranked / End match when the set is decided (user 2026-10-16: not only by the mouse or the 4 s wait),
	//     4 = Accept a found match
	const bool accept = m_confirming && !m_accepted;
	const bool rematch = m_readyBtn && m_readyBtn->isEnabled() && m_inMatch && !m_setOver && !m_confirming && m_resultShown &&
	                     !oppLeft && m_pages->currentIndex() == PageVs;
	const bool back = setDone && m_readyBtn && m_readyBtn->isEnabled();
	QPushButton* searchBtn = m_pages->currentIndex() == PageRanked ? m_rankedBtn : m_pages->currentIndex() == PageMatch ? m_quickBtn : nullptr;
	if (m_searchAgain.isValid() && (m_searchAgain.elapsed() > 90000 || m_queued || m_practice || m_inLobby))
	{
		m_searchAgain.invalidate();  // (searching again, practice, a lobby, or 90 s passed: the button is the mouse's again)
		refreshAll();
	}
	// Search: after a match (90 s) as before, and any time the launcher window is in front (user 2026-10-16: search for
	// ranked by the controller; the pad is read even when another program has it, hence only in front of the player)
	const bool again = (m_searchAgain.isValid() || isActiveWindow()) && searchBtn && searchBtn->isEnabled() && !m_inMatch &&
	                   !m_queued && !m_inLobby && !m_practice;
	const int target = accept ? 4 : rematch ? 1 : back ? 3 : again ? 2 : 0;
	if (target != m_padTarget)
	{
		m_padTarget = target;
		m_padArmed = false;
		m_padSince.invalidate();
		testLog(QString("pad: %1").arg(target == 1 ? "rematch possible" : target == 2 ? "search again possible" : target == 3 ? "back to ranked possible" : target == 4 ? "accept possible" : "off"));
		if (target != 1)
			refreshAll();  // (the button's text says the controller can press it)
	}
	if (!target)
		return;
	// (T6_TEST_PAD=2, tests: the simulated button only searches again, it does not ask for rematches)
	if (target == 1 && qEnvironmentVariable("T6_TEST_PAD") == "2")
		return;
	if (!m_padSince.isValid())
		m_padSince.start();
	if (!padConfirmDown())
	{
		if (m_padSince.elapsed() > 1000)
			m_padArmed = true;
		return;
	}
	if (m_padArmed)
	{
		m_padArmed = false;
		testLog(target == 1 ? "pad rematch: confirm pressed" : target == 3 ? "pad back to ranked: confirm pressed" : target == 4 ? "pad accept: confirm pressed" : "pad search again: confirm pressed");
		if (target == 3)
		{
			m_autoLeave.invalidate();
			m_searchAgain.start();  // (as the automatic leave: on RANKED the next press searches again)
		}
		if (target == 1 || target == 3 || target == 4)
			readyAction();  // (3: = "Back to ranked" / "End match", like the automatic leave; 4: = "Accept")
		else
		{
			m_searchAgain.invalidate();
			searchBtn->click();
		}
	}
}
