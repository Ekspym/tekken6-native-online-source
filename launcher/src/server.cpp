// Tekken 6 Native Online launcher - The lobby server's address and pinned certificate.

#include "server.h"

static QString serverAddress()
{
	const QString env = qEnvironmentVariable("T6_LOBBY_SERVER");
	return env.isEmpty() ? QString(kServer) : env;
}

ServerAddr serverAddr()
{
	ServerAddr a;
	QString srv = serverAddress();
	a.tls = srv.startsWith("tls://");
	if (a.tls)
		srv = srv.mid(6);
	const int colon = srv.lastIndexOf(':');
	a.host = colon > 0 ? srv.left(colon) : srv;
	if (colon > 0)
		a.port = srv.mid(colon + 1).toUShort();
	return a;
}

// the certificate of our server (self-signed, server/tls): the only one a TLS connection accepts
const QSslCertificate& pinnedCert()
{
	static const QSslCertificate cert = [] {
		QFile f(":/tls/t6_cert.pem");
		return f.open(QIODevice::ReadOnly) ? QSslCertificate(&f, QSsl::Pem) : QSslCertificate();
	}();
	return cert;
}
