// Tekken 6 Native Online launcher - Starting a match: UDP hole punching, the lobby ping probe, the opponent.

#include "launcher.h"

// input delay of this match: delay 0 is not allowed in ranked (user 2026-10-12, for now)
int Launcher::matchDelay() const
{
	return m_match["ranked"].toBool() ? qMax(1, m_delay) : m_delay;
}

void Launcher::setReady()
{
	m_ready = true;
	send({{"t", "ready"}, {"local_ip", localIp()}, {"local_port", m_gamePort}, {"state", m_inMatch || m_stateHash.isEmpty() || m_blur || m_render3d != 0 || m_smallShadows ? QString() : m_stateHash + "/" + m_heapHash},  // (blur on, another 3D resolution or the smaller shadow map = another render path than the default: its hold point hash differs by nature (perf2 2026-10-17: 301 pages, the match itself in sync), not compared)
	      {"random_stage", m_randomStage}});
	m_randomStage = false;
	refreshVs();
}

void Launcher::setMatchText(const QString& result, const QString& detail, bool err)
{
	m_resultText = result;
	if (!result.isEmpty())
		endRematchStart();
	m_matchDetail = detail;
	m_matchErr = err;
	showPage(PageVs);
}

// ================= match start: NAT punch, then the game
void Launcher::startMatch(const QJsonObject& m)
{
	// the online game takes over (its window shows at the loading); matchmaking: only once both accepted (launchGame)
	if (!m_lobby["queue"].toBool())
		stopPractice();
	// a new match (not the rematch both players asked for): chime + taskbar flash while the launcher is not in front
	if (m_matchSound && !(m_inMatch && m_rematchWanted))
	{
		playMatchSound();
		if (!isActiveWindow())
		{
			FLASHWINFO fw{sizeof(FLASHWINFO), reinterpret_cast<HWND>(winId()), FLASHW_TRAY | FLASHW_TIMERNOFG, 0, 0};
			FlashWindowEx(&fw);
		}
	}
	m_match = m;
	m_role = m["role"].toInt();
	m_ready = false;
	for (const char* k : {"host", "guest"})  // (the server reset both; its lobby info may come after this start)
		if (QJsonObject o = m_lobby[k].toObject(); !o.isEmpty())
		{
			o["ready"] = false;
			m_lobby[k] = o;
		}
	m_hadResult = m_resultShown = m_reported = m_playingSent = m_lostGame = false;
	m_ratedText.clear();
	m_rankDir = 0;
	if (m_inMatch && m_game && m_rematchWanted && !m_disconnected)
	{
		// Rematch = park + a new session from the hold point, NOT the game's REMATCH inside the running session: the
		// result menu remembers its last choice, a game parked after an earlier match stands on CHARACTER SELECT and
		// a fresh one on REMATCH - the same A presses took the two games different ways (desync, user 2026-10-12).
		// Parking walks each game out of its own menu by its own choice; the next session starts equal on both.
		m_rematchWanted = false;
		command("park");
		showGameWindows(false);
		m_inMatch = m_gameReady = m_pendingStart = m_startFailed = m_awaitRematch = m_showGame = false;
		m_parkTimer.start();
		m_parking = true;
		m_rematchPark = true;  // gameReady() goes on with this start once the game is parked
		m_rematchFlow = true;
		// (no return: the punch / ping below runs WHILE the game walks back to its hold point, ~2 s less per rematch
		// - user 2026-10-13. The game's session holds the UDP port until the park command is taken: punchTick binds.)
	}
	m_rematchWanted = false;
	m_goPending = false;
	closeProbe();  // (same UDP port)
	m_punching = true;
	m_peerFound = false;
	m_heardPeer = false;
	m_peerWifi = m_peerLinux = false;
	m_natPort[0] = m_peerPubPort = m_peerLocPort = -1;
	m_pingMs = -1;
	m_pingCount = 0;
	m_pingReported = false;
	m_pingClock.start();
	m_matchDetail = T("Connecting to the opponent…");
	m_matchErr = false;
	showPage(PageVs);
	if (m_udp)
		m_udp->deleteLater();
	m_udp = new QUdpSocket(this);
	m_udpBound = m_udp->bind(QHostAddress::AnyIPv4, static_cast<quint16>(m_gamePort));
	if (!m_udpBound && !m_rematchPark)
	{
		punchFailed(T("The game's UDP port is in use."));
		return;
	}
	connect(m_udp, &QUdpSocket::readyRead, this, [this] { onUdp(); });
	const QString host = serverAddr().host;
	m_rendezvous = QHostAddress(host);
	if (m_rendezvous.isNull())
	{
		const auto info = QHostInfo::fromName(host);
		for (const auto& a : info.addresses())
			if (a.protocol() == QAbstractSocket::IPv4Protocol)
			{
				m_rendezvous = a;
				break;
			}
	}
	m_wifi = onWifi(m_rendezvous);
	m_punchTicks = 0;
	m_punchTimer.disconnect();
	connect(&m_punchTimer, &QTimer::timeout, this, [this] { punchTick(); });
	m_punchTimer.start(100);
}

void Launcher::punchTick()
{
	if (++m_punchTicks > 150 && !m_pingReported)  // 15 s (after the ping report the server decides: go / cancel)
	{
		punchFailed(m_udpBound ? natText() : T("The game's UDP port is in use."));
		return;
	}
	// 'go' is here and only this side's game is still parking: nothing more is sent - the peer's game may already be
	// in its session on that port (a packet that is not GGPO's ended a game there: assertion + exit(0), e2e 2026-10-13)
	if (m_goPending)
		return;
	// rematch: the parking game closes its session (and the port) when it takes the park command
	if (!m_udpBound)
		m_udpBound = m_udp->bind(QHostAddress::AnyIPv4, static_cast<quint16>(m_gamePort));
	if (!m_udpBound)
		return;
	const QByteArray token = QJsonDocument(QJsonObject{{"t", "punch"}, {"token", m_match["token"].toString()}}).toJson(QJsonDocument::Compact);
	if (!m_peerFound && m_punchTicks % 2 == 1)
		m_udp->writeDatagram(token, m_rendezvous, static_cast<quint16>(m_match["udp_port"].toInt(7701)));
	QJsonObject hiObj{{"t", "hi"}, {"match", m_match["match_id"].toString()}, {"role", m_role}};
	if (m_wifi)
		hiObj["wifi"] = true;
	if (underWine())  // Linux (Proton): only this is told, never Windows
		hiObj["linux"] = true;
	const QByteArray hi = QJsonDocument(hiObj).toJson(QJsonDocument::Compact);
	for (const auto& [a, p] : m_candidates)
		m_udp->writeDatagram(hi, a, p);
	if (!m_heardPeer || m_pingReported)
		return;
	// real ping of the two players: echo packets on the punched path, the lowest round trip of 6 counts (3 s at most).
	// The server lets the match go on ('go') when it is inside the max ping of both; only the matchmaking has a limit.
	const QString id = m_match["match_id"].toString();
	m_udp->writeDatagram(QJsonDocument(QJsonObject{{"t", "pp"}, {"match", id}, {"ts", m_pingClock.elapsed()}}).toJson(QJsonDocument::Compact),
	                     m_peerAddr, m_peerPort);
	if (m_pingCount >= 6 || ++m_afterHeard > 30)
	{
		m_pingReported = true;
		const bool ok = !m_lobby["queue"].toBool() || m_maxPing == 0 || m_pingMs <= m_maxPing;
		send({{"t", "ping_report"}, {"match_id", id}, {"ms", qMax(0, m_pingMs)}, {"ok", ok}});
		m_matchDetail = m_pingMs >= 0 ? T("Ping %1 ms").arg(m_pingMs) : T("Connected");
		refreshVs();
	}
}

// ================= ping to the other player of the lobby, before anyone is ready (user 2026-10-12). The same
// punch as a match start, on the game's UDP port (free while the game is parked): 'punch' to the rendezvous, 'hi' to
// the peer's addresses, then 6 echoes; the lowest round trip shows in the lobby. Closed before a match start.
void Launcher::startProbe(const QJsonObject& m)
{
	closeProbe();
	m_lobbyPing = -1;
	if (m_inMatch || m_punching || !m_inLobby)
		return;
	m_probeId = m["id"].toString();
	m_probeToken = m["token"].toString();
	m_probeRole = m["role"].toInt();
	m_probePort = static_cast<quint16>(m["udp_port"].toInt(7701));
	m_probeUdp = new QUdpSocket(this);
	if (!m_probeUdp->bind(QHostAddress::AnyIPv4, static_cast<quint16>(m_gamePort)))
	{
		closeProbe();
		return;
	}
	const QString host = serverAddr().host;
	m_probeServer = QHostAddress(host);
	if (m_probeServer.isNull())
		for (const auto& a : QHostInfo::fromName(host).addresses())
			if (a.protocol() == QAbstractSocket::IPv4Protocol)
			{
				m_probeServer = a;
				break;
			}
	m_probeCands.clear();
	m_probeHeard = false;
	m_probeCount = m_probeTicks = 0;
	m_lobbyPing = -2;  // measuring
	m_probeClock.start();
	connect(m_probeUdp, &QUdpSocket::readyRead, this, [this] {
		while (m_probeUdp && m_probeUdp->hasPendingDatagrams())
		{
			const QNetworkDatagram dg = m_probeUdp->receiveDatagram();
			const QJsonObject o = QJsonDocument::fromJson(dg.data()).object();
			const QString t = o["t"].toString();
			if (o["probe"].toString() != m_probeId)
				continue;
			if (t == "hi" && o["role"].toInt() != m_probeRole && !m_probeHeard)
			{
				m_probeHeard = true;
				m_probePeer = QHostAddress(dg.senderAddress().toIPv4Address());
				m_probePeerPort = static_cast<quint16>(dg.senderPort());
			}
			else if (t == "pp")
				m_probeUdp->writeDatagram(QJsonDocument(QJsonObject{{"t", "pq"}, {"probe", m_probeId}, {"ts", o["ts"]}}).toJson(QJsonDocument::Compact),
				                          dg.senderAddress(), static_cast<quint16>(dg.senderPort()));
			else if (t == "pq")
			{
				const int rtt = static_cast<int>(m_probeClock.elapsed() - o["ts"].toInteger());
				if (rtt >= 0 && (m_lobbyPing < 0 || rtt < m_lobbyPing))
					m_lobbyPing = rtt;
				++m_probeCount;
				if (m_probeCount == 1 || m_probeCount == 6)
					refreshVs();
			}
		}
	});
	m_probeTimer.disconnect();
	connect(&m_probeTimer, &QTimer::timeout, this, [this] { probeTick(); });
	m_probeTimer.start(100);
	refreshVs();
}

void Launcher::probeTick()
{
	if (!m_probeUdp)
		return;
	// 12 s: long enough to answer the other side's echoes after the own six are in
	if (++m_probeTicks > 120)
	{
		const int ping = m_lobbyPing;
		closeProbe();
		m_lobbyPing = ping >= 0 ? ping : -3;  // -3: no direct connection found
		refreshVs();
		return;
	}
	if (m_probeCands.empty() && m_probeTicks % 2 == 1)
		m_probeUdp->writeDatagram(QJsonDocument(QJsonObject{{"t", "punch"}, {"token", m_probeToken},
		                                                    {"local", QJsonArray{localIp(), m_gamePort}}}).toJson(QJsonDocument::Compact),
		                          m_probeServer, m_probePort);
	const QByteArray hi = QJsonDocument(QJsonObject{{"t", "hi"}, {"probe", m_probeId}, {"role", m_probeRole}}).toJson(QJsonDocument::Compact);
	for (const auto& [a, p] : m_probeCands)
		m_probeUdp->writeDatagram(hi, a, p);
	if (m_probeHeard && m_probeCount < 6)
		m_probeUdp->writeDatagram(QJsonDocument(QJsonObject{{"t", "pp"}, {"probe", m_probeId}, {"ts", m_probeClock.elapsed()}}).toJson(QJsonDocument::Compact),
		                          m_probePeer, m_probePeerPort);
}

// (the measured value stays; the socket frees the game's port)
void Launcher::closeProbe()
{
	m_probeTimer.stop();
	if (m_probeUdp)
	{
		m_probeUdp->close();
		m_probeUdp->deleteLater();
		m_probeUdp = nullptr;
	}
}

void Launcher::onPeer(const QJsonObject& m)
{
	if (m["match_id"].toString() != m_match["match_id"].toString())
		return;
	m_peerFound = true;
	m_candidates.clear();
	const QJsonArray pub = m["public"].toArray(), loc = m["local"].toArray();
	m_peerPubPort = pub.size() == 2 ? pub[1].toInt() : -1;
	m_peerLocPort = loc.size() == 2 ? loc[1].toInt() : -1;
	if (m["same_nat"].toBool() && loc.size() == 2)
		m_candidates.push_back({QHostAddress(loc[0].toString()), static_cast<quint16>(loc[1].toInt())});
	if (pub.size() == 2)
		m_candidates.push_back({QHostAddress(pub[0].toString()), static_cast<quint16>(pub[1].toInt())});
	m_matchDetail = T("Punching through…");
	refreshVs();
}

void Launcher::onUdp()
{
	while (m_udp && m_udp->hasPendingDatagrams())
	{
		const QNetworkDatagram dg = m_udp->receiveDatagram();
		const QJsonObject o = QJsonDocument::fromJson(dg.data()).object();
		if (o["t"].toString() == "seen")  // the rendezvous' answer to a punch (every server version)
		{
			m_natPort[0] = o["addr"].toArray().at(1).toInt();
			m_natIp = o["addr"].toArray().at(0).toString();
		}
		else if (o["t"].toString() == "hi" && o["match"].toString() == m_match["match_id"].toString() && o["role"].toInt() != m_role && !m_heardPeer)
		{
			m_heardPeer = true;
			m_afterHeard = 0;
			m_peerAddr = QHostAddress(dg.senderAddress().toIPv4Address());
			m_peerPort = static_cast<quint16>(dg.senderPort());
			m_peerWifi = o["wifi"].toBool();
			m_peerLinux = o["linux"].toBool();
			refreshVs();
		}
		else if (o["t"].toString() == "pp" && o["match"].toString() == m_match["match_id"].toString())
			m_udp->writeDatagram(QJsonDocument(QJsonObject{{"t", "pq"}, {"match", o["match"]}, {"ts", o["ts"]}}).toJson(QJsonDocument::Compact),
			                     dg.senderAddress(), static_cast<quint16>(dg.senderPort()));
		else if (o["t"].toString() == "pq" && o["match"].toString() == m_match["match_id"].toString())
		{
			const int rtt = static_cast<int>(m_pingClock.elapsed() - o["ts"].toInteger());
			if (rtt >= 0 && (m_pingMs < 0 || rtt < m_pingMs))
				m_pingMs = rtt;
			m_pingCount++;
		}
	}
}

// the rematch that was starting is off (opponent left, server lost, ...): also its punch, which runs while the game parks
void Launcher::endRematchStart()
{
	if (m_rematchPark && m_punching)
		closeUdp();
	m_rematchFlow = m_rematchPark = false;
}

void Launcher::closeUdp()
{
	m_punchTimer.stop();
	if (m_udp)
	{
		m_udp->close();
		m_udp->deleteLater();
		m_udp = nullptr;
	}
	m_punching = false;
	m_heardPeer = false;
	m_candidates.clear();
}

QString Launcher::natText() const
{
	const bool ownBlocked = m_natPort[0] < 0, peerBlocked = !m_peerFound;
	const bool ownChanged = !ownBlocked && m_natPort[0] != m_gamePort;
	const bool peerChanged = !peerBlocked && m_peerPubPort >= 0 && m_peerLocPort > 0 && m_peerPubPort != m_peerLocPort;
	if (ownBlocked)
		return T("Your PC or router blocks the game's UDP traffic (not even the server got it): allow \"Tekken6 Native Online\" "
		         "in the firewall / antivirus for public AND private networks.");
	if (peerBlocked)
		return T("The opponent's PC or router blocks the game's UDP traffic (the server never got it) – their firewall / "
		         "antivirus. Your connection is fine.");
	const QString upnpHint = !m_upnpDone || m_upnp.mapped ? QString()
	                         : T(" Turning on UPnP in your router lets the launcher open the game's port by itself.");
	if (ownChanged && behindCgnat())
		return T("Your internet provider (CGNAT) or a second router in front of yours changes the game's port – no setting on "
		         "your PC or router helps. Ask your provider for a public IP address (or set the first router to bridge mode).");
	if (ownChanged && peerChanged)
		return T("Both of your routers change the game's port (strict NAT: mobile internet, CGNAT or two routers) – a direct "
		         "connection between you two does not work. Not a firewall setting.");
	if (ownChanged)
		return T("Your router changes the game's port (strict NAT: mobile internet, CGNAT or two routers) – that is why the "
		         "direct connection failed, not the firewall. The opponent's connection looks fine.") + upnpHint;
	if (peerChanged)
		return T("The opponent's router changes the game's port (strict NAT: mobile internet, CGNAT or two routers) – that is "
		         "why the direct connection failed. Your connection looks fine.");
	return T("Both connections look normal, but the packets did not get through – most likely a firewall or router on one "
	         "side blocks incoming UDP (allow the launcher on public AND private networks).");
}

void Launcher::punchFailed(const QString& why)
{
	closeUdp();
	m_failNote = why;  // (the server's 'cancel' for this 'leave' keeps it on the page)
	send({{"t", "leave"}});
	setMatchText(T("MATCH DID NOT START"), why, true);
}

// --auto quick (tests): into the quick match queue once the server is connected and the game is parked
void Launcher::autoQuick()
{
	bool& done = m_autoQuickDone;
	const QStringList args = QCoreApplication::arguments();
	if (done || !m_online || !m_gameReady || args.indexOf("--auto") < 0 || args.value(args.indexOf("--auto") + 1) != "quick")
		return;
	done = true;
	send({{"t", "quick"}});
}

void Launcher::endOpponentGone()
{
	if (!m_game)
		return;
	m_oppGone = true;
	m_disconnected = true;
	killGame("the opponent left (server)");
}

void Launcher::playMatchSound()
{
	if (m_matchSound)
		playMatchFoundSound(m_matchSound == 2 ? m_matchSoundFile : QString());
}

int Launcher::nextLeft() const
{
	if (m_nextSeconds <= 0 || !m_nextClock.isValid() || !m_setRunning || m_setOver)
		return -1;
	return (std::max)(0, m_nextSeconds - static_cast<int>(m_nextClock.elapsed() / 1000));
}

// the interface the route to 'to' leaves on is Wi-Fi (a UDP connect sends nothing, it only picks the local address)
bool Launcher::onWifi(const QHostAddress& to)
{
	QUdpSocket s;
	s.connectToHost(to, 7701);
	const QHostAddress local = s.localAddress();
	if (local.isNull())
		return false;
	for (const QNetworkInterface& i : QNetworkInterface::allInterfaces())
		for (const QNetworkAddressEntry& e : i.addressEntries())
			if (e.ip().isEqual(local))
				return i.type() == QNetworkInterface::Wifi;
	return false;
}
