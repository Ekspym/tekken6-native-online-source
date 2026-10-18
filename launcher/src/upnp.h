#pragma once
// Tekken 6 Native Online launcher - UPnP port mapping on the router (the game's UDP port).

#include "common.h"

// UPnP (2026-10-17, user: a player behind a strict NAT whose router settings cannot be changed): at start the launcher
// asks the router (UPnP Internet Gateway Device) to forward the game's UDP port to this PC, so the punch works without
// router settings. The router's own external address also shows a second router or the provider's CGNAT (a private /
// 100.64.0.0/10 address, or not the address the server sees): no setting at home helps then - natText() says so.
struct Upnp
{
	QString control, service;  // the router's WANIPConnection / WANPPPConnection ('' = no UPnP router answered)
	QString localIp, externalIp;
	int port = 0;
	bool mapped = false;
	QString error;             // why the mapping failed / what was found (log)
};
bool privateIpv4(const QString& ip);
Upnp upnpMap(int port);
void upnpUnmap(const Upnp& u);
