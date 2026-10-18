// Tekken 6 Native Online launcher - The game process: start, environment, windows, status file, commands, display options.

#include "launcher.h"

// the match is over because the opponent left: game closed (no forfeit of this side), message on the VS page
// A line of the launcher in the game's log (m_gameLog): why the launcher ended the game, how the game process ended
void Launcher::appendGameLog(const QString& line)
{
	if (m_gameLog.isEmpty())
		return;
	QFile f(m_gameLog);
	if (f.open(QIODevice::Append))
		f.write(QString("[%1] [launcher] %2\n").arg(QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz"), line).toUtf8());
}

void Launcher::killGame(const char* why)
{
	if (!m_game)
		return;
	m_killReason = QString::fromUtf8(why);
	appendGameLog(QString("ends the game: %1").arg(m_killReason));
	m_game->kill();
}

// --d3d12_adapter of the chosen adapter (by name: the indexes can move); -1 = automatic
int Launcher::gpuIndex() const
{
	for (const Gpu& g : gpuList())
		if (!m_gpu.isEmpty() && g.name == m_gpu)
			return g.index;
	return -1;
}

void Launcher::refreshGpu()
{
	if (!m_gpuLabel)
		return;
	m_gpuLabel->setText(m_gpuInUse.isEmpty() ? (m_game ? T("In use: (starting…)") : QString()) : T("In use: %1").arg(m_gpuInUse));
}

// a setting the game only reads at its start (graphics adapter): a parked game starts again, never inside a match
void Launcher::restartParkedGame()
{
	if (!m_game || m_inMatch || m_pendingStart)
	{
		refreshGpu();
		return;
	}
	m_restartGame = true;
	command("quit");
	killGame("restart of the parked game");
}

// ================= the game: started ahead, parked before the fight loading
// The environment of a game start: this process's, without any match / mode variable of a test run or of an
// earlier start (each start sets its own)
QProcessEnvironment Launcher::gameEnvironment()
{
	auto env = QProcessEnvironment::systemEnvironment();
	for (const char* v : {"T6_NET", "T6_NET_HOLD", "T6_NET_PLAYER", "T6_NET_REMOTE", "T6_NET_LOCAL_PORT", "T6_NET_SEED", "T6_NET_STATUS",
	                      "T6_NET_CMD", "T6_CHAR_P1", "T6_CHAR_P2", "T6_STAGE", "T6_ROUNDS", "T6_SNAP_EXCLUDE_FILE", "T6_PRACTICE"})
		env.remove(v);
	return env;
}

// The player's settings every game start passes (online game, practice, Customize): controls, display, and with
// lookAndSound the motion blur / vibration / stage music options (not in the Customize session)
void Launcher::addGameEnv(QProcessEnvironment& env, bool lookAndSound) const
{
	env.insert("T6_PAD_MAP", padMap());
	env.insert("TEKKEN6_INTERNAL_RESOLUTION", m_render3d == 2 ? "640x360" : m_render3d == 1 ? "768x432" : "1024x576");
	env.insert("TEKKEN6_OFFSCREEN_RESOLUTION", m_smallShadows ? "512x512" : "1024x1024");
	env.insert("TEKKEN6_FRAME_PROBE_ATTEMPTS", "0");  // (no frame probe captures in players' games, review 2026-10-18)
	env.insert("T6_KEYMAP", keyMap());
	env.insert("REX_GPU_VSYNC", "1");
	if (!lookAndSound)
		return;
	if (!m_blur)
		env.insert("T6_NO_BLUR", "1");
	if (!m_vibration)
		env.insert("T6_NO_VIBRATION", "1");
	if (!m_music)
		env.insert("T6_NO_MUSIC", "1");
}

// the game's command line of a start: its data, this start's profile / cache / log, display and input options
QStringList Launcher::gameArgs(const QString& user, const QString& runDir) const
{
	return QStringList{"--game_data_root=" + QDir::toNativeSeparators(gameDataRoot()),
	                   "--user_data_root=" + QDir::toNativeSeparators(user),
	                   "--cache_root=" + QDir::toNativeSeparators(QDir(runDir).filePath("cache")),
	                   "--log_file=" + QDir::toNativeSeparators(QDir(runDir).filePath("tekken6.log")),
	                   "--fullscreen=" + QString(m_fullscreen ? "true" : "false"), tearingArg(), "--async_shader_compilation=true", renderTargetArg(),
	                   QString("--draw_resolution_scale_x=%1").arg(m_resScale), QString("--draw_resolution_scale_y=%1").arg(m_resScale),
	                   "--d3d12_adapter=" + QString::number(gpuIndex())} + inputArgs();
}

void Launcher::prelaunch()
{
	if (m_game || m_closing || m_updating || m_buildChecking || (m_builder && m_builder->state() != QProcess::NotRunning) ||
	    QCoreApplication::arguments().contains("--no-game"))
		return;
	if (!QFile::exists(gameExe()) || (m_builder && m_builder->state() != QProcess::NotRunning) || autoUpdateGame() ||
	    !gameUsable())
	{
		refreshAll();
		return;
	}
	if (const QString miss = cpuMissing(); !miss.isEmpty())
	{
		m_buildError = T("this processor is too old for the game (it lacks %1)").arg(miss);
		m_bootError = true;
		refreshAll();
		return;
	}
	const QString runDir = rootFile(QString("runs/online_%1").arg(g_instance));
	QDir().mkpath(runDir);
	m_statusFile = QDir(runDir).filePath("status.txt");
	m_cmdFile = QDir(runDir).filePath("cmd.txt");
	QFile::remove(m_statusFile);
	QFile::remove(m_cmdFile);
	m_gameReady = false;
	m_stateHash.clear();
	m_heapHash.clear();

	auto env = gameEnvironment();  // (the match settings come with the 'start' command)
	env.insert("T6_NET", "1");
	env.insert("T6_NET_HOLD", "1");
	// Scripted way through the menus up to the versus loading (tools\menu_vs.bin, the same inputs on both sides) and a
	// fixed seed: without them the game stays in the menu and the two instances start from different random states
	env.insert("T6_PLAY", QDir::toNativeSeparators(rootFile("tools/menu_vs.bin")));
	env.insert("T6_FIX_SEED", "1");
	env.insert("T6_VIRTUAL_TB", "1");
	env.insert("T6_SOUND_SEPARATE", "1");
	const QString excl = QFile::exists(rootFile("tools/netplay_exclude.txt")) ? rootFile("tools/netplay_exclude.txt") : rootFile("runs/exclude_jobpool.txt");
	env.insert("T6_SNAP_EXCLUDE_FILE", QDir::toNativeSeparators(excl));
	env.insert("T6_NET_STATUS", QDir::toNativeSeparators(m_statusFile));
	env.insert("T6_NET_CMD", QDir::toNativeSeparators(m_cmdFile));
	// hold_pages.txt (hash of every page at the hold point) next to the log: explains a state hash difference
	env.insert("T6_NET_DUMP_DIR", QDir::toNativeSeparators(runDir));
#ifdef T6_DEV
	// sound lifetimes of the developer's own matches (user 2026-10-15: hit sounds cut online) -> runs\online_N\snd_life.csv
	env.insert("T6_SND_LIFE", QDir::toNativeSeparators(QDir(runDir).filePath("snd_life.csv")));
#endif
	addGameEnv(env, true);

	m_game = new QProcess(this);
	m_game->setProcessEnvironment(env);
	m_game->setWorkingDirectory(QFileInfo(gameExe()).absolutePath());
	m_game->setProgram(gameExe());
	const QString user = QDir(runDir).filePath("user");
	freshProfile(user);
	addShaderCache(runDir);
	m_game->setArguments(gameArgs(user, runDir));
	m_gpuInUse.clear();
	m_gameLog = QDir(runDir).filePath("tekken6.log");
	m_gameLogFrom = QFileInfo(m_gameLog).exists() ? QFileInfo(m_gameLog).size() : 0;
	if (m_upnpDone)
		appendGameLog(upnpLine());
	if (g_leftoverGames)
		appendGameLog(QString("ended %1 game process(es) left by an earlier launcher at start").arg(std::exchange(g_leftoverGames, 0)));
	refreshGpu();
	if (!showParked())
		m_game->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* a) {
			a->startupInfo->dwFlags |= STARTF_USESHOWWINDOW;
			a->startupInfo->wShowWindow = SW_HIDE;
		});
	connect(m_game, &QProcess::finished, this, [this](int code) {
		const bool inMatch = m_inMatch, failed = m_startFailed, oppGone = m_oppGone;
		// into the game's own log (it goes with "Send the game's log"): a crash shows as "ended by itself" with its code
		appendGameLog(m_killReason.isEmpty()
		                  ? QString("the game process ended by itself, exit code 0x%1%2").arg(uint(code), 8, 16, QChar('0')).arg(inMatch ? " - IN A MATCH" : "")
		                  : QString("the game process ended (killed by the launcher: %1)").arg(m_killReason));
		m_killReason.clear();
		// T6_LAUNCHER_LOG=<file> (tests): how the game ended (exit status 1 = killed / crashed)
		testLog(QString("game finished: code %1 status %2 inMatch %3 started %4 failed %5 oppGone %6 disconnected %7 hadResult %8")
		            .arg(code).arg(int(m_game->exitStatus())).arg(inMatch).arg(m_started).arg(failed).arg(oppGone).arg(m_disconnected).arg(m_hadResult));
		m_oppGone = false;
		// the game of a running match is gone (Quit game, window closed, crash) before a result and with the opponent
		// still there: this side gave the match up
		if (inMatch && m_playingSent && !m_hadResult && !m_disconnected && !failed)
			send({{"t", "forfeit"}, {"match_id", m_match["match_id"].toString()}});
		m_game->deleteLater();
		m_game = nullptr;
		m_inMatch = m_gameReady = m_pendingStart = m_startFailed = m_rematchWanted = m_awaitRematch = m_disconnected = m_showGame = false;
		if (m_closing)
			return;
		const bool restart = m_restartGame;
		m_restartGame = false;
		if (!inMatch)
		{
			// closed while it was booting / parked: once more, then give up. A restart on purpose (controls, GPU,
			// display changed) is neither a failure nor the end of a match (v0.1.8: it sent 'leave' and showed GAME
			// ENDED on every change in CONTROLS)
			if (!restart && ++m_bootFails >= 2)
			{
				m_bootError = true;
				m_poll.stop();
				refreshAll();
				return;
			}
		}
		else if (m_rematch)
		{
			m_hadResult = m_resultShown = m_reported = false;
			m_resultText.clear();
			m_matchErr = false;
		}
		else
		{
			send({{"t", "leave"}});
			if (oppGone)
				setMatchText(T("OPPONENT DISCONNECTED"), T("The opponent left the match or lost the connection."), true);
			else if (failed && !m_startError.isEmpty())
				setMatchText(T("MATCH DID NOT START"), m_startError, true);
			else if (failed)
				setMatchText(T("MATCH DID NOT START"),
				             T("The game did not get into the match, log: runs\\online_%1\\tekken6.log").arg(g_instance), true);
			else
				setMatchText(T("GAME ENDED"),
				             code ? T("Code %1, log: runs\\online_%2\\tekken6.log").arg(code).arg(g_instance) : QString(),
				             code != 0);
		}
		prelaunch();
		refreshAll();
	});
	m_game->start();
	endWithLauncher(m_game->processId());
	m_poll.disconnect();
	connect(&m_poll, &QTimer::timeout, this, [this] { pollStatus(); });
	m_poll.start(100);
}

bool Launcher::showParked() { return qEnvironmentVariableIsSet("T6_LAUNCHER_SHOW_GAME"); }

// The launcher in front of everything. Windows refuses SetForegroundWindow to a process that is not in front (the game
// was): the input of the foreground thread is attached for the call.
void Launcher::toFront()
{
	showNormal();
	raise();
	const HWND me = reinterpret_cast<HWND>(winId());
	const HWND fg = GetForegroundWindow();
	const DWORD fgThread = fg ? GetWindowThreadProcessId(fg, nullptr) : 0, myThread = GetCurrentThreadId();
	const bool attach = fgThread && fgThread != myThread && AttachThreadInput(myThread, fgThread, TRUE);
	SetForegroundWindow(me);
	SetActiveWindow(me);
	if (attach)
		AttachThreadInput(myThread, fgThread, FALSE);
	activateWindow();
}

// top-level windows of the game process: hidden while it is parked, shown with the match
void Launcher::showGameWindows(bool show)
{
	if (m_game && m_game->processId())
		showWindowsOf(static_cast<DWORD>(m_game->processId()), show, m_fullscreen);
}

// Fullscreen = borderless window over the whole monitor it is on, done here when the window shows (the SDK's own
// --fullscreen did not take for a window started hidden)
void Launcher::makeFullscreen(HWND w)
{
	const LONG_PTR style = GetWindowLongPtrW(w, GWL_STYLE);
	if ((style & WS_POPUP) && !(style & WS_CAPTION))
		return;
	MONITORINFO mi{sizeof(mi)};
	if (!GetMonitorInfoW(MonitorFromWindow(w, MONITOR_DEFAULTTOPRIMARY), &mi))
		return;
	SetWindowLongPtrW(w, GWL_STYLE, (style & ~(WS_CAPTION | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU)) | WS_POPUP);
	SetWindowLongPtrW(w, GWL_EXSTYLE, GetWindowLongPtrW(w, GWL_EXSTYLE) & ~(WS_EX_DLGMODALFRAME | WS_EX_CLIENTEDGE | WS_EX_STATICEDGE | WS_EX_WINDOWEDGE));
	const RECT& r = mi.rcMonitor;
	SetWindowPos(w, HWND_TOP, r.left, r.top, r.right - r.left, r.bottom - r.top, SWP_FRAMECHANGED | SWP_NOOWNERZORDER);
}

void Launcher::showWindowsOf(DWORD gamePid, bool show, bool fullscreen)
{
	struct Ctx { DWORD pid; bool show; bool full; } ctx{gamePid, show, fullscreen};
	EnumWindows([](HWND w, LPARAM l) -> BOOL {
		const Ctx& c = *reinterpret_cast<Ctx*>(l);
		DWORD pid = 0;
		GetWindowThreadProcessId(w, &pid);
		if (pid != c.pid || GetWindow(w, GW_OWNER) || !GetWindowTextLengthW(w))
			return TRUE;
		if (!c.show)
		{
			if (IsWindowVisible(w))
				ShowWindow(w, SW_HIDE);
		}
		else if (!IsWindowVisible(w))
		{
			if (c.full)
				makeFullscreen(w);
			if (qEnvironmentVariableIsSet("T6_LAUNCHER_NOFOCUS"))
				ShowWindow(w, SW_SHOWNOACTIVATE);
			else
			{
				ShowWindow(w, SW_SHOW);
				SetForegroundWindow(w);
			}
		}
		return TRUE;
	}, reinterpret_cast<LPARAM>(&ctx));
}

// the game stands at the hold point
void Launcher::gameReady()
{
	m_parking = false;
	m_gameReady = true;
	m_bootFails = 0;
	if (m_rematchPark)
	{
		// parked for a rematch: the punch / ping of the match the server sent is already running (startMatch); the
		// start line goes out now when the server's 'go' is here, else with the 'go'
		m_rematchPark = false;
		if (m_goPending && m_punching)
		{
			m_goPending = false;
			launchGame();
		}
		refreshAll();
		return;
	}
	if (m_pendingStart)
		sendStart();
	else if (m_rematch)
	{
		m_rematch = false;
		m_matchDetail.clear();
		setReady();
	}
	autoQuick();
	refreshAll();
}

// match start (after the NAT punch): the parked game gets its 'start' line
void Launcher::launchGame()
{
	stopPractice();
	m_rematchFlow = false;  // (m_inMatch from here on)
	closeUdp();  // the game binds the same port (the NAT mapping stays)
	const QJsonObject p1 = m_match["p1"].toObject(), p2 = m_match["p2"].toObject();
	// (both launchers compute the same codes from the server's info; a server before 2026-10-14 has no 'costume': P1)
	const auto [code1, code2] = matchCodes(p1["char"].toInt(), p1["costume"].toInt(), p2["char"].toInt(), p2["costume"].toInt());
	m_resultShown = m_reported = m_started = m_startFailed = m_showGame = false;
	m_nextSeconds = 0;  // (the next game of the set started: no Next game countdown)
	m_startError.clear();
	m_inMatch = true;
	// Rounds to win: 3 (online standard; the game's versus default is 2). T6_ROUNDS / T6_ROUND_TIME in the launcher's
	// environment override it for tests.
	m_startLine = QString("start player=%1 port=%2 remote=%3:%4 delay=%5 seed=%6 char1=%7 char2=%8 rounds=%9")
	                  .arg(m_role).arg(m_gamePort).arg(m_peerAddr.toString()).arg(m_peerPort).arg(matchDelay()).arg(m_match["seed"].toInt())
	                  .arg(code1).arg(code2).arg(qEnvironmentVariable("T6_ROUNDS", "3"));
	// (no 'skipintro' for a rematch - user 2026-10-13: the players skip the intros themselves or watch them)
	// T6_NET_INPUT (tests): scripted input pattern instead of the player's
	m_startLine += QString(" input=%1 keys=%2").arg(qEnvironmentVariable("T6_NET_INPUT", "live")).arg(m_keys);
	// Player names for the name plates of the fight HUD (hex of the Latin-1 bytes: no spaces in the start line)
	m_startLine += QString(" name1=%1 name2=%2").arg(QString::fromLatin1(p1["name"].toString().left(15).toLatin1().toHex()),
	                                                QString::fromLatin1(p2["name"].toString().left(15).toLatin1().toHex()));
	// ranks of both players (the server's index, 0 = Beginner) for the HUD plates. The game takes the fields, but its
	// HUD does not show them yet (2026-10-11: the plate's rank byte is not what the HUD draws)
	m_startLine += QString(" rank1=%1 rank2=%2").arg(p1["rank"].toInt()).arg(p2["rank"].toInt());
	// the game that can win this player the ranked set with a promotion: the game shows its own "Promoted to <rank>"
	// at the KO that wins the match (server 'promo', user 2026-10-16)
	if (m_match.contains("promo"))
		m_startLine += QString(" promo=%1").arg(m_match["promo"].toInt());
	if (m_match["stage"].toInt(-1) >= 0)
		m_startLine += QString(" stage=%1").arg(m_match["stage"].toInt());
	// custom looks of both players (the game's Customize menu, 2026-10-15): both games inject the same two records
	for (const auto& [key, p] : {std::pair{"custom1", p1}, std::pair{"custom2", p2}})
		if (const QString rec = p["custom"].toString(); kCustomLooks && validRecord(rec))
			m_startLine += QString(" %1=%2").arg(key, rec);
	if (qEnvironmentVariableIsSet("T6_ROUND_TIME"))
		m_startLine += " time=" + qEnvironmentVariable("T6_ROUND_TIME");
	m_pendingStart = !m_gameReady;
	if (m_gameReady)
		sendStart();
	else
		prelaunch();  // normally unreachable (Ready and the queue need the parked game)
	m_matchDetail = m_pendingStart ? T("Preparing the game…") : T("Match found – loading…");
	refreshVs();
}

void Launcher::sendStart()
{
	m_pendingStart = false;
	m_startSends = 1;
	m_startTimer.start();
	command(m_startLine.toUtf8().constData());
}

// status file of the game (T6_NET_STATUS): 'state ...', 'round p1 p2', 'winner n', 'disconnected'
void Launcher::pollStatus()
{
	if (m_gpuInUse.isEmpty() && !m_gameLog.isEmpty())
	{
		// the game logs its adapter at boot: "DXGI adapter: <name> (vendor ..."
		QFile lf(m_gameLog);
		if (lf.open(QIODevice::ReadOnly) && lf.seek(m_gameLogFrom))
		{
			const QString head = QString::fromUtf8(lf.read(256 * 1024));
			if (const int at = head.indexOf("DXGI adapter: "); at >= 0)
			{
				const int from = at + 14, end = head.indexOf(" (vendor", from);
				m_gpuInUse = head.mid(from, end > from ? end - from : head.indexOf('\n', from) - from).trimmed();
				refreshGpu();
			}
		}
	}
	if (!m_inMatch && !showParked())
		showGameWindows(false);
	QFile f(m_statusFile);
	if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
		return;
	QString state, round, winner;
	int slow = -1;  // share of slow fight frames of the game (below 60 FPS), -1 = not known
	int lag = -1, behind = -1, lagPing = -1;  // fight frames with a predicted remote input in %, the peer's frames behind x 10, the ping
	bool disconnected = false;
	for (const QString& line : QString::fromUtf8(f.readAll()).split('\n'))
	{
		const QString l = line.trimmed();
		if (l.startsWith("state "))
			state = l.mid(6);
		else if (l.startsWith("round "))
			round = l.mid(6);
		else if (l.startsWith("winner "))
			winner = l.mid(7);
		else if (l.startsWith("slow "))
			slow = l.mid(5).toInt();
		else if (l.startsWith("lag "))
		{
			const QStringList w = l.mid(4).split(' ');
			lag = w.value(0).toInt();
			behind = w.value(1, "-1").toInt();
			lagPing = w.value(2, "-1").toInt();
		}
		else if (l == "disconnected")
			disconnected = true;
		else if (l.startsWith("statehash ") && !m_inMatch)
			m_stateHash = l.mid(10);
		else if (l.startsWith("heaphash ") && !m_inMatch)
			m_heapHash = l.mid(9);
	}
	if (!m_inMatch)
	{
		if (state == "ready" && !m_gameReady)
			gameReady();
		// a park that does not come back to 'ready' (the game was not in its result menu after all): new game
		else if (m_parking && m_parkTimer.elapsed() > 30000 && m_game)
		{
			m_parking = false;
			killGame("park took over 30 s");
		}
		return;
	}
	if (m_pendingStart)
		return;
	// 'state error <text>': the session did not start (UDP port, no connection to the opponent)
	if (state.startsWith("error") && m_game && !m_startFailed)
	{
		m_startFailed = true;
		// never an IP address on the screen (user 2026-10-16: "v launcheru nesmi byt nikde napsana tvoje nebo cizi
		// IP"): the game's 'no connection to the opponent <ip>:<port>' gets a text of its own, any other address goes
		const QString ge = state.mid(6);
		m_startError = ge.startsWith("no connection to the opponent")
		                   ? T("No connection to the opponent's game (their firewall or router blocks it).")
		                   : QString(ge).replace(QRegularExpression(R"(\d{1,3}(\.\d{1,3}){3}(:\d+)?)"), "").simplified();
		killGame("start failed: game error");
		return;
	}
	if (!m_started)
	{
		// 'ready' -> 'loading' confirms the command (repeated every 3 s until then); the fight's intro has to follow
		const bool taken = !state.isEmpty() && state != "ready";
		if (!taken && m_startTimer.elapsed() > 3000 * m_startSends)
		{
			m_startSends++;
			command(m_startLine.toUtf8().constData());
		}
		// the window appears with the battle's loading screen (mode 14), not with the stage select left behind
		if (taken && state != "loading")
		{
			m_started = m_showGame = true;
			sendPlaying();
		}
		else if (m_startTimer.elapsed() > 90000 && m_game)
		{
			m_startFailed = true;
			killGame("start took over 90 s");
			return;
		}
	}
	// the match is decided (win pose, then the result menu): the game hides, the launcher comes to the front with the
	// result and the Rematch button at once (user 2026-10-12: "po dokonceni hry aby se okamzite objevil launcher")
	if (m_started && m_showGame && (state == "menu" || (!winner.isEmpty() && state == "result")))
	{
		m_showGame = false;
		showGameWindows(false);
		if (!qEnvironmentVariableIsSet("T6_LAUNCHER_NOFOCUS"))
			toFront();
	}
	showGameWindows(m_showGame);
	QString d = !m_started ? T("Match found – loading…") : state;
	if (m_match["state_warning"].toBool())
		d = T("⚠ Different game state – a desync is possible  ·  ") + d;
	if (m_started && !winner.isEmpty() && (state == "menu" || state == "result"))
	{
		const QString name = m_match[winner == "1" ? "p1" : "p2"].toObject()["name"].toString();
		d = winner == "0" ? T("Draw") : T("Winner: %1").arg(name);
	}
	if (!round.isEmpty())
		d += T("  ·  rounds ") + round;
	if (m_pingMs >= 0)
		d += QString("  ·  ping %1 ms").arg(m_pingMs);
	if (disconnected)
		d += T("  ·  the opponent disconnected");
	m_disconnected = disconnected;
	// the opponent is gone in a running match (closed the game, lost the connection): the game would go on alone
	// against a standing opponent - it ends here with a message (user 2026-10-12)
	if (disconnected && m_inMatch && m_game && !m_rematchWanted && winner.isEmpty())
	{
		endOpponentGone();
		return;
	}
	// the opponent is gone while this side waits for the rematch: the session is over, a new game is needed
	if (disconnected && m_rematchWanted && m_game)
	{
		m_rematchWanted = false;
		m_rematch = true;
		killGame("rematch: restart");
		return;
	}
	if (m_awaitRematch)
	{
		// the old result stays in the status file until the game loads the next match
		if (winner.isEmpty() && state != "result" && state != "menu")
		{
			m_awaitRematch = false;
			m_showGame = true;
			sendPlaying();
			showGameWindows(true);
		}
		else if (m_startTimer.elapsed() > 40000 && m_game)
		{
			m_startFailed = true;
			m_startError = T("The rematch did not start.");
			killGame("the rematch did not start");
			return;
		}
		else
			return;
	}
	const bool result = state == "result" || !winner.isEmpty();
	if (result && !m_reported && !winner.isEmpty())
	{
		m_reported = true;
		// (slow: the server ends the set when one side's game ran below 60 FPS in more than SLOW_PCT (20) % of the fight frames)
		send({{"t", "result"}, {"match_id", m_match["match_id"].toString()}, {"winner", winner.toInt()}, {"slow", slow},
		      {"lag", lag}, {"behind", behind}, {"ping", lagPing}});
		m_lostGame = winner.toInt() != 0 && winner.toInt() != m_role;
		addHistory(winner.toInt());
	}
	// A rematch stays possible when the opponent has already closed his game for it (the game then reports a disconnect)
	m_hadResult = m_hadResult || result;
	m_resultShown = m_hadResult;

	// --auto-rematch <n> (tests): press Rematch n times, 5 s after a result
	if (static int left = [] { const QStringList a = QCoreApplication::arguments(); const int i = a.indexOf("--auto-rematch"); return i >= 0 ? a.value(i + 1).toInt() : 0; }();
	    left > 0 && m_hadResult && m_inMatch && !m_rematch && !m_rematchWanted && !m_awaitRematch)
	{
		if (static int ticks = 0; ++ticks >= 80)
		{
			ticks = 0;
			left--;
			// T6_TEST_RANDOM_STAGE=1 (tests): the loser asks for another stage, as with the button
			if (qEnvironmentVariableIsSet("T6_TEST_RANDOM_STAGE") && m_randBtn && m_randBtn->isVisibleTo(this))
			{
				testLog("random stage asked by the loser");
				m_randomStage = true;
			}
			readyAction();
			return;
		}
	}
	// --auto-leave <n> (tests): leave the match n times, 5 s after a result -> the game parks, the next quick match
	// follows (--auto quick) without a game restart
	if (static int left = [] { const QStringList a = QCoreApplication::arguments(); const int i = a.indexOf("--auto-leave"); return i >= 0 ? a.value(i + 1).toInt() : 0; }();
	    left > 0 && m_hadResult && m_inMatch && state == "menu")
	{
		if (static int ticks = 0; ++ticks >= 50)
		{
			ticks = 0;
			left--;
			leaveAction();
			return;
		}
	}
	m_matchDetail = std::move(d);
	refreshVs();
}

// the game is in the match: from here on leaving it is a forfeit (double Elo loss, the opponent gets the win)
void Launcher::sendPlaying()
{
	if (m_playingSent)
		return;
	m_playingSent = true;
	send({{"t", "playing"}, {"match_id", m_match["match_id"].toString()}});
}

void Launcher::command(const char* c)
{
	QFile f(m_cmdFile);
	if (f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
		f.write(QByteArray(c) + "\n");
}

void Launcher::showDisplayNote()
{
	if (!m_displayNote)
		return;
	QString t = m_noticeText;
	if (!t.isEmpty() && m_noticeAt > 0)
	{
		const qint64 left = m_noticeAt - QDateTime::currentSecsSinceEpoch();
		t += "   ·   " + (left > 60 ? T("in %1 min").arg((left + 59) / 60) : left > 0 ? T("in less than a minute") : T("now"));
	}
	m_displayNote->setText(t);
}

int Launcher::displayMode() const
{
	return !m_fullscreen ? 2 : m_borderless ? 1 : 0;
}

// (always passed, so the game's log shows which way it presents)
QString Launcher::tearingArg() const
{
	// never (2.2.5): since the game presents at once from the guest output thread (no achievement toast dialog, input
	// latency -1 frame) tearing would show as torn frames in Fullscreen; a full-screen flip model window without it
	// still gets the display's independent flip, frames whole
	return QString("--d3d12_allow_variable_refresh_rate_and_tearing=false");
}

// Wine / Proton (vkd3d-proton): ROV needs VK_EXT_fragment_shader_interlock, which many Linux drivers lack, so
// RTV there; real Windows keeps ROV (the shipped pipeline storage is ROV only). Not a supported platform.
bool Launcher::underWine()
{
	static const bool wine = GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "wine_get_version") != nullptr;
	return wine;
}

QString Launcher::renderTargetArg()
{
	return QString("--render_target_path_d3d12=") + (underWine() ? "rtv" : "rov");
}
