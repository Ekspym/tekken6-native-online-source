// Tekken 6 Native Online launcher - The connection to the lobby server and its messages, notices, polls.

#include "launcher.h"

// ================= server connection (our lobby server, address fixed in the release)
void Launcher::connectServer()
{
	if (m_tcp)
	{
		m_tcp->abort();
		m_tcp->deleteLater();
	}
	m_online = m_connected = m_authed = false;
	const ServerAddr addr = serverAddr();
	auto* sock = new QSslSocket(this);
	m_tcp = sock;
	const auto hello = [this] {
		send(helloMsg());
	};
	if (addr.tls)
	{
		// our own (self-signed) certificate: the errors are ignored only when the server shows exactly the pinned one
		connect(sock, &QSslSocket::sslErrors, this, [sock](const QList<QSslError>&) {
			if (!pinnedCert().isNull() && sock->peerCertificate() == pinnedCert())
				sock->ignoreSslErrors();
		});
		connect(sock, &QSslSocket::encrypted, this, [sock, hello] {
			if (pinnedCert().isNull() || sock->peerCertificate() != pinnedCert())
				sock->abort();
			else
				hello();
		});
	}
	else
		connect(sock, &QTcpSocket::connected, this, hello);
	connect(m_tcp, &QTcpSocket::readyRead, this, [this] {
		while (m_tcp && m_tcp->canReadLine())
			onMessage(QJsonDocument::fromJson(m_tcp->readLine()).object());
	});
	connect(m_tcp, &QTcpSocket::disconnected, this, [this] { serverLost(); });
	connect(m_tcp, &QTcpSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) { serverLost(); });
	if (addr.tls)
		sock->connectToHostEncrypted(addr.host, addr.port);
	else
		sock->connectToHost(addr.host, addr.port);
}

void Launcher::serverLost()
{
	if (m_closing)
		return;
	const bool was = m_connected;
	m_online = m_connected = m_authed = false;
	m_inLobby = false;
	endRematchStart();
	closeProbe();
	m_lobbyPing = -1;
	m_queued = false;
	m_lobbies = {};
	refreshAll();
	if (!m_reconnect)
	{
		m_reconnect = true;
		QTimer::singleShot(was ? 2000 : 5000, this, [this] {
			m_reconnect = false;
			if (!m_online)
				connectServer();
		});
	}
}

void Launcher::send(const QJsonObject& o)
{
	if (m_tcp && m_tcp->state() == QAbstractSocket::ConnectedState)
		m_tcp->write(QJsonDocument(o).toJson(QJsonDocument::Compact) + "\n");
}

// The game's log (its end: the last matches) to the server, compressed, in lines the server's connection takes
void Launcher::sendLog()
{
	if (!m_online)
	{
		m_logLabel->setText(T("Not connected to the server."));
		return;
	}
	QFile f(rootFile(QString("runs/online_%1/tekken6.log").arg(g_instance)));
	if (!f.open(QIODevice::ReadOnly) || f.size() == 0)
	{
		m_logLabel->setText(T("There is no log yet."));
		return;
	}
	// the player's own words about the problem (optional), added after the log's end (user 2026-10-14)
	bool ok = false;
	const QString note = QInputDialog::getMultiLineText(this, T("Send the game's log"),
	                                                    T("What happened? (optional - e.g. the game closed in round 2, froze, no sound)"),
	                                                    QString(), &ok).trimmed().left(2000);
	if (!ok)
		return;
	const QByteArray noteBytes = note.isEmpty() ? QByteArray()
	                             : QString("[%1] [launcher] PLAYER NOTE (launcher %2): %3\n")
	                                   .arg(QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss"), kVersion, QString(note).replace('\n', " / "))
	                                   .toUtf8();
	constexpr int kLine = 44000, kLines = 64;  // (the server's limits)
	QByteArray b64;
	for (qint64 tail = 6 * 1024 * 1024; tail >= 64 * 1024; tail /= 2)
	{
		f.seek(qMax<qint64>(0, f.size() - tail));
		b64 = qCompress(f.readAll() + noteBytes, 9).toBase64();
		if (b64.size() <= kLine * kLines)
			break;
	}
	const int n = static_cast<int>((b64.size() + kLine - 1) / kLine);
	if (n < 1 || n > kLines)
		return;
	for (int i = 0; i < n; i++)
		send({{"t", "log"}, {"i", i}, {"n", n}, {"version", kVersion}, {"data", QString::fromLatin1(b64.mid(qsizetype(i) * kLine, kLine))}});
	m_logLabel->setText(T("Sending…"));
	// (a server that does not know the message says nothing)
	QTimer::singleShot(12000, this, [this] {
		if (m_logLabel->text() == T("Sending…"))
			m_logLabel->setText(T("The server did not answer - it does not take logs yet."));
	});
}

// what the server keeps of this player's choices (sent with 'hello' and every change)
QJsonObject Launcher::profileMsg() const
{
	return {{"t", "profile"}, {"char", m_char}, {"costume", costume(m_char)}, {"custom", customRecord(m_char, costume(m_char))}, {"side", m_side}, {"region", m_region}, {"region_lock", m_regionLock}, {"strict", m_strict}};
}

QJsonObject Launcher::helloMsg() const
{
	QJsonObject m = profileMsg();
	m["t"] = "hello";
	m["proto"] = kProto;
	m["private_lobbies"] = true;
	m["version"] = kVersion;
	m["client_id"] = m_clientId;
	m["machine"] = machineId();
	m["build"] = m_build;
	return m;
}

void Launcher::sendProfile()
{
	send(profileMsg());
	syncSettings();
	refreshAll();
}

void Launcher::onMessage(const QJsonObject& m)
{
	const QString t = m["t"].toString();
	if (t == "welcome")
	{
		m_connected = true;
		if (!m_session.isEmpty())
			send({{"t", "auth"}, {"session", m_session}});
		else if (QCoreApplication::arguments().contains("--auto"))
			send({{"t", "guest"}, {"name", m_name}});  // tests: no account
		showPage(PageLogin);
	}
	else if (t == "account")
	{
		const bool first = !m_authed;
		m_authed = m_online = true;
		m_guest = m["guest"].toBool();
		m_admin = m["admin"].toBool();
		m_name = m["name"].toString();
		m_elo = m["elo"].toInt(500);
		m_games = m["games"].toInt();
		m_wins = m["wins"].toInt();
		m_rank = m["rank"].toInt(15);
		m_chars = m["chars"].toObject();
		m_rankNames = m["rank_names"].toArray();
		m_onlineCount = m["online"].toInt(m_onlineCount);  // players online (the server sends it every 30 s too)
		// the poll panel (user 2026-10-15): accounts with enough games that did not vote yet
		{
			const QJsonObject poll = m["poll"].toObject();
			m_pollId = poll["id"].toString();
			m_pollQuestion = poll["question"].toString();
			m_pollCanVote = poll["can_vote"].toBool();
			m_pollVoted = poll["voted"].toInt(-1);
			refreshPoll();
		}
		applyNotice(m["notice"].toObject());  // (none: the bar stays empty)
		if (m.contains("session"))
		{
			m_session = m["session"].toString();
			auto s = settings();
			s.setValue("session", m_session);
			s.setValue("account", m_name);
		}
		if (!first)
		{
			refreshAll();
			return;
		}
		if (!m_guest)
			applySettings(m["settings"].toObject());
		send(profileMsg());
		// (no 'watch' since 2.2.2: the lobby list is asked for by the Lobbies page itself)
		if (!m_inMatch && !m_punching)
		{
			// the pages are built again: name, character and delay of the account
			rebuildUi();
		}
		else
			refreshAll();
		if (m_pages && m_pages->currentIndex() == PageLobby)
			requestLobbies(true);
		// --auto quick (tests): into the quick match queue right away; a quick match starts without the Ready button
		autoQuick();
	}
	else if (t == "logged_out")
	{
		m_authed = m_online = m_inLobby = m_queued = m_ready = false;
		m_session.clear();
		settings().remove("session");
		showPage(PageLogin);
	}
	else if (t == "rated")
	{
		const int d = m["elo_delta"].toInt(), rd = m["rank_delta"].toInt();
		m_ratedText = QString("MMR %1%2 (%3)").arg(d >= 0 ? "+" : "").arg(d).arg(m["elo"].toInt());
		// (the board only on request since 2.2.2: page opened / Refresh - not after every rated match)
		m_setRunning = false;
		if (m["quit"].toInt() == 2 && m_inMatch && m_game)
			endOpponentGone();  // ranked: the server knows it before the game's 5 s time-out
		if (const int q = m["quit"].toInt())
			m_ratedText = (q == 1 ? T("You left the match: double loss") : T("The opponent left: you win")) + "   ·   " + m_ratedText;
		else if (const int lf = m["left"].toInt())
			m_ratedText = (lf == 1 ? T("You left the set: loss") : T("The opponent left the set: you win")) + "   ·   " + m_ratedText;
		else if (const QJsonArray w = m["set"].toArray(); w.size() == 2)
			m_ratedText = T("Set %1 – %2").arg(w.at(m_role == 2 ? 1 : 0).toInt()).arg(w.at(m_role == 2 ? 0 : 1).toInt()) + "   ·   " + m_ratedText;
		// promotions only (user 2026-10-16: "kasleme na degradaci"), big under the result; the game itself showed its
		// "Promoted to <rank>" at the final KO (start line promo=)
		m_rankNew = m["rank"].toInt(-1);
		m_rankDir = rd > 0 && m_rankNew >= 0 && m_rankNew < m_rankNames.size() ? 1 : 0;
		refreshVs();
	}
	else if (t == "blocked")
	{
		m_searchNote = T("%1 is blocked: you will not be matched with them again (Profile -> BLOCKED to undo).").arg(m["name"].toString());
		refreshAll();
	}
	else if (t == "blocks")
	{
		m_blocks = m["list"].toArray();
		refreshBlocked();
	}
	else if (t == "lobbies")
	{
		m_lobbies = m["lobbies"].toArray();
		refreshList();
		if (m_lobbyUpdated)
			m_lobbyUpdated->setText(T("Updated %1").arg(QTime::currentTime().toString("HH:mm:ss")));
	}
	else if (t == "queued")
	{
		m_queued = true;
		m_queuedRanked = m["ranked"].toBool();
		refreshAll();
	}
	else if (t == "lobby")
	{
		m_lobby = m["lobby"].toObject();
		if (m["quick"].toBool())
		{
			m_hideOpp = true;  // matchmaking: the opponent stays hidden until both accepted
			m_searchNote.clear();
		}
		m_role = m["you"].toInt(m_role);
		m_inLobby = true;
		m_queued = false;
		if (!m_lobby["guest"].toObject().isEmpty())
			m_oppLeft = false;  // (a lobby with both players: a new pairing)
		if (m_lobby["guest"].toObject().isEmpty())
		{
			if (m_inMatch)
				m_oppLeft = true;  // (padPoll takes this side out of a search lobby once the result showed)
			closeProbe();
			m_lobbyPing = -1;
			m_ready = false;
			endRematchStart();  // the opponent left while the rematch was starting
		}
		showPage(PageVs);
		if (m["quick"].toBool())
			setReady();  // quick match: start right away
	}
	else if (t == "lag_warn")
	{
		// One side's game lagged behind more than its ping explains (user 2026-10-15): nothing ends by itself - the
		// numbers are shown, rematch / Next game stay, and leaving after this game is free (no set lost)
		QStringList names;
		for (const QJsonValue v : m["who"].toArray())
			names << v.toString();
		const bool me = names.contains(m_name, Qt::CaseInsensitive);
		const QString numbers = T("Ping %1 ms should mean about %2 frames behind, it was %3.")
		                            .arg(m["ping"].toInt())
		                            .arg(m["expected10"].toInt() / 10.0, 0, 'f', 1)
		                            .arg(m["behind10"].toInt() / 10.0, 0, 'f', 1);
		m_lagFree = true;
		m_ratedText = (m_ratedText.isEmpty() ? QString() : m_ratedText + "\n") +
		              (me ? T("Your game lagged behind your opponent's (slow PC or connection). %1 You can leave now without penalty.").arg(numbers)
		                  : T("%1's game lagged behind yours. %2 You can leave now without penalty.").arg(names.join(", "), numbers));
		refreshVs();
	}
	else if (t == "slow_end")
	{
		// A game ran below 60 FPS (user 2026-10-15): the game just played counted, then no rematch - the lobby is
		// closed, the button ends the match (an undecided ranked set ended without rating)
		const QJsonArray who = m["who"].toArray(), pct = m["pct"].toArray();
		QStringList names;
		for (const QJsonValue v : who)
			names << v.toString();
		const int mine = pct.at(m_role == 2 ? 1 : 0).toInt(-1), theirs = pct.at(m_role == 2 ? 0 : 1).toInt(-1);
		const bool me = names.contains(m_name, Qt::CaseInsensitive);
		m_slowEnd = true;
		m_matchErr = true;  // the line in red (user 2026-10-16: the players overlooked it)
		if (m["why"].toString() == "lag")
			// (user 2026-10-15: the opponent's game lagged behind the whole fight - "hra v tomhle stavu musi nabidnout odejit")
		{
			// "ping X ms should be about N frames, it was M" (user 2026-10-15: "na tenhle ping to melo byt tolik ale bylo to tolik")
			const QString numbers = m["ping"].toInt(-1) >= 0
			                            ? T(" Ping %1 ms should mean about %2 frames behind, it was %3.")
			                                  .arg(m["ping"].toInt())
			                                  .arg(m["expected10"].toInt() / 10.0, 0, 'f', 1)
			                                  .arg(m["behind10"].toInt() / 10.0, 0, 'f', 1)
			                            : QString();
			m_ratedText = me ? T("Your game lagged behind your opponent's for most of the fight (slow PC or connection) - the match "
			                     "was hard to play for both.%1 Lower the resolution, close other programs, use a cable. No rematch.").arg(numbers)
			                 : T("%1's game lagged behind yours for most of the fight.%2 No rematch.").arg(names.join(", "), numbers);
		}
		else
			m_ratedText = me ? T("Your game ran below 60 FPS in %1 % of the fight - it lags for your opponent. Lower the resolution or "
			                     "close other programs. No rematch.").arg(mine)
			                 : T("%1's game ran below 60 FPS in %2 % of the fight - no rematch.").arg(names.join(", ")).arg(theirs);
		m_setOver = true;
		m_setRunning = false;
		m_inLobby = false;
		endRematchStart();
		closeProbe();
		m_lobbyPing = -1;
		m_rematchWanted = false;
		refreshVs();
	}
	else if (t == "set_over")
	{
		// ranked set (first to 2) decided: no rematch, the player searches again (user 2026-10-12)
		if (m["why"].toString() == "next_timeout")
		{
			const QString loser = m["loser"].toString();
			m_ratedText = loser.isEmpty() ? T("Nobody pressed Next game in %1 s – the set ended without MMR change.").arg(30)
			              : loser.compare(m_name, Qt::CaseInsensitive) == 0 ? T("You did not press Next game in %1 s – set lost.").arg(30)
			                                                             : T("%1 did not press Next game in %2 s – you win the set.").arg(loser).arg(30);
		}
		m_nextSeconds = 0;
		m_setOver = true;
		m_setRunning = false;
		m_inLobby = false;
		endRematchStart();
		closeProbe();
		m_lobbyPing = -1;
		m_rematchWanted = false;
		refreshVs();
	}
	else if (t == "left")
	{
		if (m_inMatch && m["reason"].toString() == "host_left")
			m_oppLeft = true;
		m_setRunning = false;
		m_inLobby = false;
		endRematchStart();
		closeProbe();
		m_lobbyPing = -1;
		m_queued = false;
		m_ready = false;
		selectMode(m_lastMode);
	}
	else if (t == "set")
	{
		// ranked: one game of the set is over, the Elo moves when the set is decided
		const QJsonArray w = m["wins"].toArray();
		m_setRunning = true;
		m_ratedText = T("Set %1 – %2  ·  first to %3").arg(w.at(m_role == 2 ? 1 : 0).toInt()).arg(w.at(m_role == 2 ? 0 : 1).toInt()).arg(m["target"].toInt(2));
		// Next game within next_seconds (user 2026-10-15), else the one who did not press loses the set
		m_nextSeconds = m["next_seconds"].toInt(0);
		if (m_nextSeconds > 0)
		{
			m_nextClock.start();
			if (!m_nextTimer)
			{
				m_nextTimer = new QTimer(this);
				connect(m_nextTimer, &QTimer::timeout, this, [this] {
					if (nextLeft() < 0)
						m_nextTimer->stop();
					refreshVs();
				});
			}
			m_nextTimer->start(500);
		}
		refreshVs();
	}
#ifdef T6_DEV
	else if (t == "stats")
	{
		m_statsAt.restart();
		const QJsonObject lob = m["lobbies"].toObject();
		QString h = QString("<p style='font-size:15pt'><b>%1</b> online &nbsp;·&nbsp; <b>%2</b> playing &nbsp;·&nbsp; <b>%3</b> in lobbies"
		                    " &nbsp;·&nbsp; queue: ranked <b>%4</b>, player match <b>%5</b></p>")
		                .arg(m["online"].toInt()).arg(m["playing"].toInt()).arg(m["in_lobby"].toInt())
		                .arg(m["queue_ranked"].toInt()).arg(m["queue_quick"].toInt());
		h += QString("<p>Accounts online %1, guests %2 &nbsp;·&nbsp; lobbies: open %3, full %4, playing %5</p>")
		         .arg(m["accounts_online"].toInt()).arg(m["guests_online"].toInt())
		         .arg(lob["open"].toInt()).arg(lob["full"].toInt()).arg(lob["playing"].toInt());
		h += QString("<p><b>Accounts</b> %1 (new in 24 h: %2) &nbsp;·&nbsp; played in 24 h: %3<br><b>Matches</b> %4 finished (ranked %5)"
		             " &nbsp;·&nbsp; started in 24 h: %6, in the last hour: %7 &nbsp;·&nbsp; server up %8 h</p>")
		         .arg(m["accounts"].toInt()).arg(m["accounts_24h"].toInt()).arg(m["played_24h"].toInt())
		         .arg(m["matches_total"].toInt()).arg(m["ranked_total"].toInt()).arg(m["matches_24h"].toInt())
		         .arg(m["matches_1h"].toInt()).arg(m["uptime_h"].toDouble());
		if (const QJsonObject poll = m["poll"].toObject(); !poll.isEmpty())
		{
			const int yes = poll["yes"].toInt(), no = poll["no"].toInt(), all = yes + no;
			h += QString("<p><b>Poll</b> %1 &nbsp;·&nbsp; Yes <b>%2</b> (%3 %) &nbsp;·&nbsp; No <b>%4</b> (%5 %) &nbsp;·&nbsp; %6 votes</p>")
			         .arg(poll["question"].toString().toHtmlEscaped()).arg(yes).arg(all ? 100 * yes / all : 0).arg(no)
			         .arg(all ? 100 * no / all : 0).arg(all);
		}
		const auto table = [&](const char* title, const QJsonObject& o) {
			QString r = QString("<p><b>%1</b><br>").arg(title);
			for (auto it = o.begin(); it != o.end(); ++it)
				r += QString("%1 &nbsp;×&nbsp; %2<br>").arg(it.value().toInt()).arg(it.key().toHtmlEscaped());
			return r + "</p>";
		};
		h += table("Launcher versions online", m["versions"].toObject());
		h += table("Game fingerprints online (only equal ones are paired)", m["builds"].toObject());
		h += "<p><b>Matches running now</b><br>";
		const QJsonArray ms = m["matches"].toArray();
		for (const QJsonValue v : ms)
		{
			const QJsonObject o = v.toObject();
			h += QString("%1 vs %2 &nbsp;·&nbsp; %3 &nbsp;·&nbsp; %4 min<br>")
			         .arg(o["p1"].toString().toHtmlEscaped(), o["p2"].toString().toHtmlEscaped(),
			              o["ranked"].toBool() ? "ranked" : o["queue"].toBool() ? "player match" : "lobby")
			         .arg(o["minutes"].toDouble());
		}
		if (ms.isEmpty())
			h += "none";
		h += "</p>";
		m_statsLabel->setText(h);
	}
#endif
	else if (t == "log_saved")
	{
		const int wait = m["wait"].toInt();
		m_logLabel->setText(m["ok"].toBool() ? T("Log sent. Thank you!")
		                    : wait > 0       ? T("A log can be sent once an hour – again in %1 min.").arg((wait + 59) / 60)
		                                     : T("The log was not accepted."));
	}
	else if (t == "history")
		showHistoryOf(m["name"].toString(), m["matches"].toArray());
	else if (t == "leaderboard")
	{
		if (!m.contains("region") || m["region"].toString() == m_boardRegion)  // (else: the answer to an earlier filter)
		{
			m_board = m["top"].toArray();
			m_boardPos = m["you"].toInt();
			if (m_boardUpdated)
				m_boardUpdated->setText(m["updated"].toInteger() > 0
				                            ? T("Updated %1").arg(QDateTime::fromSecsSinceEpoch(m["updated"].toInteger()).toString("HH:mm:ss"))
				                            : QString());
			// (a server before 2.0.3 sends no numbers: the line stays empty)
			const int players = m["players"].toInt(-1), total = m["total"].toInt(-1);
			m_boardCount->setText(players < 0                 ? QString()
			                      : m_boardRegion == "ALL"    ? T("%1 players").arg(players)
			                                                  : T("%1 players in %2  ·  %3 in total").arg(players).arg(m_boardRegion).arg(total));
			refreshBoard();
		}
	}
	else if (t == "confirm")
	{
		// matchmaking: opponent found and the ping measured - the player accepts (or declines) within the time
		if (m["match_id"].toString() == m_match["match_id"].toString())
		{
			m_confirming = true;
			m_accepted = m_oppAccepted = false;
			m_confirmPing = m["ping"].toInt();
			m_confirmUntil = m["seconds"].toInt(15) * 1000;
			m_confirmClock.start();
			if (!m_confirmTimer)
			{
				m_confirmTimer = new QTimer(this);
				connect(m_confirmTimer, &QTimer::timeout, this, [this] {
					if (m_confirming)
						refreshVs();
					else
						m_confirmTimer->stop();
				});
			}
			m_confirmTimer->start(500);
			showPage(PageVs);
			if (!qEnvironmentVariableIsSet("T6_LAUNCHER_NOFOCUS"))
				toFront();  // (also over the practice window)
			if (qEnvironmentVariableIsSet("T6_AUTO_ACCEPT"))  // tests
				readyAction();
		}
	}
	else if (t == "accepted")
	{
		// matchmaking: the opponent accepted (this side has not yet)
		if (m_confirming && m["match_id"].toString() == m_match["match_id"].toString())
		{
			m_oppAccepted = true;
			refreshVs();
		}
	}
	else if (t == "go")
	{
		m_confirming = false;
		m_hideOpp = false;
		// both sides accepted the measured ping: the parked game gets its start line
		if (m_punching && m["match_id"].toString() == m_match["match_id"].toString())
		{
			m_pingMs = m["ping"].toInt(m_pingMs);
			if (m_rematchPark && !m_gameReady)
				m_goPending = true;  // the game is still on its way to the hold point: gameReady() starts it
			else
				launchGame();
		}
	}
	else if (t == "cancel")
	{
		// the ping was above the limit of one of the two: a queue pair is searching again (the server sends 'queued'),
		// a lobby stays
		if (m["match_id"].toString() == m_match["match_id"].toString())
		{
			closeUdp();
			m_ready = false;
			m_confirming = false;
			m_hideOpp = false;
			const QString why = m["reason"].toString();
			// (server 2.2.4+) the side whose connection failed after 15 s (punchFailed: "MATCH DID NOT START" + the
			// reason, then 'leave') gets this 'cancel' of its own leave: the reason stays on the page as before 2.2.4
			// (clearing it here left the title without the reason)
			m_matchDetail = why == "left" && m["you"].toBool() ? m_failNote : QString();
			if (why == "declined" || why == "timeout")
			{
				// the side that declined / did not answer goes back to its page; the other one searches again
				m_inLobby = false;
				endRematchStart();
				closeProbe();
				m_lobbyPing = -1;
				const bool me = m["you"].toBool();
				m_searchNote = me ? (why == "declined" ? T("You declined the match.") : T("You did not accept the match in time."))
				                  : T("The opponent did not accept – searching again.");
				selectMode(m_lastMode);
				return;
			}
			// the other side (searching again, before 2.2.4 its own 15 s ran out and showed its reason): the reason on the
			// search page - its own side's problem if it has one, else the opponent's
			if (why == "left" && !m["you"].toBool() && m_lobby["queue"].toBool())
			{
				const bool own = m_natPort[0] < 0 || m_natPort[0] != m_gamePort;  // (natText: blocked / port changed on this side)
				m_searchNote = own ? T("The last match did not start: %1").arg(natText())
				                   : T("The opponent could not connect or left – searching again.");
			}
			m_failNote.clear();
			if (m_lobby["queue"].toBool())
			{
				m_inLobby = false;
				endRematchStart();
				closeProbe();
				m_lobbyPing = -1;
				selectMode(m_lastMode);
			}
			else
			{
				m_matchDetail = T("Ping %1 ms is above the limit – the match did not start.").arg(m["ping"].toInt());
				refreshAll();
			}
		}
	}
	else if (t == "start")
		startMatch(m);
	else if (t == "peer")
		onPeer(m);
	else if (t == "probe")
		startProbe(m);
	else if (t == "probe_peer")
	{
		if (m["id"].toString() == m_probeId)
		{
			m_probeCands.clear();
			const QJsonArray pub = m["public"].toArray(), loc = m["local"].toArray();
			if (m["same_nat"].toBool() && loc.size() == 2)
				m_probeCands.push_back({QHostAddress(loc[0].toString()), static_cast<quint16>(loc[1].toInt())});
			if (pub.size() == 2)
				m_probeCands.push_back({QHostAddress(pub[0].toString()), static_cast<quint16>(pub[1].toInt())});
		}
	}
	else if (t == "notice")
		applyNotice(m);
	else if (t == "online")
	{
		m_onlineCount = m["n"].toInt();
		refreshAll();
	}
	else if (t == "voted")
	{
		m_pollVoted = m["choice"].toInt();
		m_pollThanks = true;
		refreshPoll();
		QTimer::singleShot(4000, this, [this] {
			m_pollThanks = false;
			refreshPoll();
		});
	}
	else if (t == "error")
	{
		const QString e = m["msg"].toString();
		if (e == "login first")
			return;
		if (e == "session_expired" || e == "logged_in_elsewhere")
		{
			// no automatic login again (two launchers of one account would keep throwing each other out)
			m_session.clear();
			if (e == "session_expired")
				settings().remove("session");
			m_authed = m_online = false;
			showPage(PageLogin);
		}
		const QString le = e == "bad_login"            ? T("Wrong name or password.")
		                   : e == "name_taken"         ? T("This name is already taken.")
		                   : e == "name_invalid"       ? T("Name: 3–15 characters, only letters, digits and _ . -")
		                   : e == "password_short"     ? T("The password needs at least 6 characters.")
		                   : e == "too_many_attempts"  ? T("Too many attempts, try again later.")
		                   : e == "insecure"           ? T("The connection to the server is not encrypted, login refused.")
		                   : e == "guests_off"         ? T("The server requires an account.")
		                   : e == "session_expired"    ? T("The login has expired, log in again.")
		                   : e == "logged_in_elsewhere" ? T("The account was logged in somewhere else.")
		                                               : QString();
		// (logged in: the throttled private lobby codes of the server, 2026-10-18 - the login page is not shown then)
		if (m_authed && e == "too_many_attempts")
		{
			notify(QMessageBox::Warning, T("Too many wrong lobby codes, try again in a minute."));
			return;
		}
		if (!le.isEmpty())
		{
			m_loginErr->setText(le);
			return;
		}
		if (e == "version_mismatch")
		{
			// what differs (server from 2.1.0 on sends both launcher versions): another launcher version = one of
			// the two has to update; the same version = the game files differ (an update that did not finish, an
			// installation updated with the old "changed files only" patches) - rebuilding or a fresh copy cures it
			const QString yours = m["yours"].toString(), theirs = m["theirs"].toString();
			QString text;
			if (!theirs.isEmpty() && yours != theirs)
				text = T("The opponent has another version of the launcher (%1, you have %2).\n\n"
				         "Whoever has the older one: restart the launcher and take the update it offers.").arg(theirs, yours);
			else
				text = T("You and the opponent have the same launcher version, but your game files differ.\n\n"
				         "Both of you: Profile -> BUILD GAME -> build again.\n"
				         "If that does not help, download the full package again and extract it over your game folder "
				         "(replace all files), then build once more.");
			notify(QMessageBox::Warning, text);
			return;
		}
		if (e == "char_locked_set")
		{
			// (user 2026-10-15: no other character in the middle of a ranked set) back to the one the set runs with
			for (const char* side : {"host", "guest"})
			{
				const QJsonObject p = m_lobby[side].toObject();
				if (p["name"].toString() == m_name && p.contains("char"))
					m_char = p["char"].toInt(m_char);
			}
			syncSettings();
			refreshAll();
			notify(QMessageBox::Information, T("You can't change your character in the middle of a ranked set."));
			return;
		}
		if (e == "lobby_not_found" || e == "wrong_code")
			requestLobbies(true);  // (the list may be old: it comes only on request since 2.2.2)
		notify(QMessageBox::Warning,
		                     e == "lobby_not_found" ? T("The lobby does not exist or is full.")
		                     : e == "wrong_code"    ? T("Wrong code for this private lobby.")
		                     : e == "ranked_needs_account" ? T("Ranked needs an account.")
		                     : e == "same_network"  ? T("No ranked match between two players on the same PC or network.")
		                     : e == "version_mismatch" ? T("The opponent has a different version of the game.")
		                     : e == "state_mismatch"   ? T("The two games are not in the same state, the match did not start. Try again.")
		                     : e == "protocol"      ? T("The launcher is outdated, download the new version.")
		                                            : e);
	}
}

void Launcher::applyNotice(const QJsonObject& n)
{
	m_noticeText = n["text"].toString();
	m_noticeAt = n["at"].toInteger();
	showDisplayNote();
}

void Launcher::buildPoll()
{
	m_pollBox = new QFrame(m_content);
	m_pollBox->setObjectName("pollBox");
	m_pollBox->setStyleSheet("QFrame#pollBox { background: #191927; border: 1px solid #EFBC23; border-radius: 12px; }");
	auto* pv = new QVBoxLayout(m_pollBox);
	pv->setContentsMargins(16, 12, 16, 14);
	pv->setSpacing(8);
	pv->addWidget(text(T("QUICK VOTE"), "gold"));
	m_pollLabel = new QLabel;
	m_pollLabel->setWordWrap(true);
	m_pollLabel->setStyleSheet("color: #E8E8F2; font-size: 15px; font-weight: 600;");
	pv->addWidget(m_pollLabel);
	auto* row = new QHBoxLayout;
	row->setSpacing(8);
	m_pollYes = button(T("Yes"), "pick");
	m_pollNo = button(T("No"), "pick");
	for (auto [b, choice] : {std::pair{m_pollYes, 1}, std::pair{m_pollNo, 0}})
	{
		connect(b, &QPushButton::clicked, this, [this, choice] {
			send({{"t", "vote"}, {"poll", m_pollId}, {"choice", choice}});
			m_pollYes->setEnabled(false);
			m_pollNo->setEnabled(false);
		});
		row->addWidget(b);
	}
	pv->addLayout(row);
	m_pollBox->setFixedWidth(260);
	m_content->installEventFilter(this);
	refreshPoll();
}

void Launcher::refreshPoll()
{
	if (!m_pollBox || !m_pages)
		return;
	const int page = m_pages->currentIndex();
	const bool open = !m_pollId.isEmpty() && m_pollCanVote && (m_pollVoted < 0 || m_pollThanks);
	// (only where the corner is free: the profile / leaderboard / practice pages have content there)
	const bool show = m_authed && !m_guest && open && (page == PageRanked || page == PageMatch || page == PageLobby);
	m_pollLabel->setText(m_pollThanks ? T("Thanks for voting!") : m_pollQuestion);
	m_pollYes->setVisible(!m_pollThanks);
	m_pollNo->setVisible(!m_pollThanks);
	m_pollYes->setEnabled(true);
	m_pollNo->setEnabled(true);
	m_pollBox->setVisible(show);
	if (show)
	{
		m_pollBox->adjustSize();
		placePoll();
		m_pollBox->raise();
	}
}

// bottom right of the content area, above the bottom bar
void Launcher::placePoll()
{
	if (m_pollBox && m_content)
		m_pollBox->move(m_content->width() - m_pollBox->width() - 60, m_content->height() - m_pollBox->height() - 104);  // (inside the page card)
}
