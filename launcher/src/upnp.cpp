// Tekken 6 Native Online launcher - UPnP port mapping on the router (the game's UDP port).

#include "upnp.h"
#include "http.h"

bool privateIpv4(const QString& ip)
{
	bool ok = false;
	const quint32 v = QHostAddress(ip).toIPv4Address(&ok);
	return !ok || v == 0 || (v >> 24) == 10 || (v >> 20) == 0xAC1 || (v >> 16) == 0xC0A8 || (v >> 22) == (100u << 2 | 1);
}

static int upnpSoap(const Upnp& u, const char* action, const QString& args, QByteArray& out)
{
	const QByteArray body = QString("<?xml version=\"1.0\"?><s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
	                                "s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\"><s:Body><u:%1 xmlns:u=\"%2\">%3</u:%1>"
	                                "</s:Body></s:Envelope>")
	                            .arg(QString::fromLatin1(action), u.service, args)
	                            .toUtf8();
	return httpLocal(u.control, L"POST",
	                 QString("Content-Type: text/xml; charset=\"utf-8\"\r\nSOAPAction: \"%1#%2\"\r\n").arg(u.service, QString::fromLatin1(action)), body, out);
}

static QString xmlValue(const QByteArray& xml, const char* tag)
{
	const QRegularExpressionMatch m = QRegularExpression(QString("<(?:\\w+:)?%1>([^<]*)</").arg(QString::fromLatin1(tag))).match(QString::fromUtf8(xml));
	return m.hasMatch() ? m.captured(1).trimmed() : QString();
}

static bool upnpAdd(Upnp& u, int lease)
{
	QByteArray out;
	const int st = upnpSoap(u, "AddPortMapping",
	                        QString("<NewRemoteHost></NewRemoteHost><NewExternalPort>%1</NewExternalPort><NewProtocol>UDP</NewProtocol>"
	                                "<NewInternalPort>%1</NewInternalPort><NewInternalClient>%2</NewInternalClient><NewEnabled>1</NewEnabled>"
	                                "<NewPortMappingDescription>Tekken6 Native Online</NewPortMappingDescription><NewLeaseDuration>%3</NewLeaseDuration>")
	                            .arg(u.port)
	                            .arg(u.localIp)
	                            .arg(lease),
	                        out);
	if (st == 200)
		return true;
	u.error = QString("AddPortMapping: HTTP %1, UPnP error %2 %3").arg(st).arg(xmlValue(out, "errorCode"), xmlValue(out, "errorDescription"));
	return false;
}

// (worker thread, a few seconds at most) SSDP search, the router's description, its external address, the mapping
Upnp upnpMap(int port)
{
	Upnp u;
	u.port = port;
	WSADATA wsa;
	if (WSAStartup(MAKEWORD(2, 2), &wsa))
	{
		u.error = "no Winsock";
		return u;
	}
	QStringList locations;
	SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (s != INVALID_SOCKET)
	{
		DWORD wait = 500;
		setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&wait), sizeof(wait));
		sockaddr_in to{};
		to.sin_family = AF_INET;
		to.sin_port = htons(1900);
		inet_pton(AF_INET, "239.255.255.250", &to.sin_addr);
		for (const char* st : {"urn:schemas-upnp-org:device:InternetGatewayDevice:1", "urn:schemas-upnp-org:device:InternetGatewayDevice:2",
		                       "urn:schemas-upnp-org:service:WANIPConnection:1"})
		{
			const QByteArray m = QByteArray("M-SEARCH * HTTP/1.1\r\nHOST: 239.255.255.250:1900\r\nMAN: \"ssdp:discover\"\r\nMX: 2\r\nST: ") + st + "\r\n\r\n";
			sendto(s, m.constData(), int(m.size()), 0, reinterpret_cast<const sockaddr*>(&to), sizeof(to));
		}
		char buf[2048];
		for (const ULONGLONG until = GetTickCount64() + 3000; GetTickCount64() < until;)
		{
			sockaddr_in from{};
			int fromLen = sizeof(from);
			const int n = recvfrom(s, buf, sizeof(buf), 0, reinterpret_cast<sockaddr*>(&from), &fromLen);
			if (n <= 0)
			{
				if (!locations.isEmpty())
					break;  // (the answers come within a moment of each other)
				continue;
			}
			// (only a description on the answering device itself, in the LAN: no other address is fetched / sent SOAP to
			// because a device said so - review 2026-10-18)
			char fromIp[64]{};
			inet_ntop(AF_INET, &from.sin_addr, fromIp, sizeof(fromIp));
			for (const QString& line : QString::fromLatin1(buf, n).split("\r\n"))
				if (line.startsWith("location:", Qt::CaseInsensitive))
				{
					const QString loc = line.mid(9).trimmed();
					const QUrl lu(loc);
					if (lu.scheme() == "http" && lu.host() == QLatin1String(fromIp) && privateIpv4(lu.host()) && !locations.contains(loc))
						locations << loc;
				}
		}
		closesocket(s);
	}
	for (const QString& loc : locations)
	{
		QByteArray xml;
		if (httpLocal(loc, L"GET", {}, {}, xml) != 200)
			continue;
		QString base, type, ctrl;
		QList<QPair<QString, QString>> services;
		for (QXmlStreamReader x(xml); !x.atEnd();)
		{
			x.readNext();
			if (x.isEndElement() && x.name() == u"service")
			{
				if ((type.contains("WANIPConnection") || type.contains("WANPPPConnection")) && !ctrl.isEmpty())
					services.push_back({type, ctrl});
				type.clear();
				ctrl.clear();
			}
			else if (x.isStartElement() && x.name() == u"URLBase")
				base = x.readElementText().trimmed();
			else if (x.isStartElement() && x.name() == u"serviceType")
				type = x.readElementText().trimmed();
			else if (x.isStartElement() && x.name() == u"controlURL")
				ctrl = x.readElementText().trimmed();
		}
		for (const auto& [t, c] : services)
		{
			Upnp v = u;
			v.service = t;
			v.control = QUrl(base.isEmpty() ? loc : base).resolved(QUrl(c)).toString();
			if (QUrl(v.control).host() != QUrl(loc).host())
				continue;  // (the control point on another host than the device: not followed)
			QByteArray out;
			if (upnpSoap(v, "GetExternalIPAddress", {}, out) != 200)
				continue;
			v.externalIp = xmlValue(out, "NewExternalIPAddress");
			if (v.externalIp.isEmpty() || v.externalIp == "0.0.0.0")
				continue;  // (a WAN service that is not connected)
			// this PC's address towards the router
			const QUrl gu(loc);
			sockaddr_in g{};
			g.sin_family = AF_INET;
			g.sin_port = htons(static_cast<u_short>(gu.port(80)));
			inet_pton(AF_INET, gu.host().toLatin1().constData(), &g.sin_addr);
			if (SOCKET t2 = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP); t2 != INVALID_SOCKET)
			{
				sockaddr_in me{};
				int len = sizeof(me);
				char ip[64]{};
				if (!connect(t2, reinterpret_cast<const sockaddr*>(&g), sizeof(g)) && !getsockname(t2, reinterpret_cast<sockaddr*>(&me), &len))
					inet_ntop(AF_INET, &me.sin_addr, ip, sizeof(ip));
				closesocket(t2);
				v.localIp = QString::fromLatin1(ip);
			}
			u = v;
			break;
		}
		if (!u.control.isEmpty())
			break;
	}
	if (u.control.isEmpty())
		u.error = locations.isEmpty() ? QString("no UPnP router answered")
		                              : QString("%1 UPnP device(s), none with a connected WAN service").arg(locations.size());
	else if (u.localIp.isEmpty())
		u.error = "this PC's address towards the router not found";
	// 7 days (removed when the launcher closes); 725 = the router only takes permanent mappings
	else
	{
		u.mapped = upnpAdd(u, 604800);
		if (!u.mapped && u.error.contains("error 725"))
			u.mapped = upnpAdd(u, 0);
	}
	WSACleanup();
	return u;
}

void upnpUnmap(const Upnp& u)
{
	QByteArray out;
	upnpSoap(u, "DeletePortMapping",
	         QString("<NewRemoteHost></NewRemoteHost><NewExternalPort>%1</NewExternalPort><NewProtocol>UDP</NewProtocol>").arg(u.port), out);
}
