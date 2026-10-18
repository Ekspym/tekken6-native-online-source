// Tekken 6 Native Online launcher - This PC: CPU, graphics cards, machine id, region, local address, the match found sound.

#include "system.h"

// "Match found" sound (user 2026-10-14), played by Windows while the launcher may be behind the practice game or
// another program. Built in: a gong made by tools\make_match_sound.py (our own synthesis, sounds\match_found.wav).
// The player's own file: Windows' media player (MCI "mpegvideo" = DirectShow: WAV, MP3, WMA, ...); a file that does not
// play gives the built-in gong. Empty path = built-in.
void playMatchFoundSound(const QString& file)
{
	mciSendStringW(L"close t6match", nullptr, 0, nullptr);
	PlaySoundW(nullptr, nullptr, 0);
	if (!file.isEmpty() && QFileInfo::exists(file))
	{
		const QString open = QString("open \"%1\" type mpegvideo alias t6match").arg(QDir::toNativeSeparators(file));
		if (mciSendStringW(reinterpret_cast<LPCWSTR>(open.utf16()), nullptr, 0, nullptr) == 0 &&
		    mciSendStringW(L"play t6match", nullptr, 0, nullptr) == 0)
			return;
		mciSendStringW(L"close t6match", nullptr, 0, nullptr);
	}
	static const QByteArray wav = [] {
		QFile f(":/sounds/match_found.wav");
		return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
	}();
	if (!wav.isEmpty())
		PlaySoundW(reinterpret_cast<LPCWSTR>(wav.constData()), nullptr, SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
}

// Instructions the game is compiled for (builder\compile.json: -march=x86-64-v2) that this processor lacks, "" = all
// there. Without them the game dies at its first such instruction (review 2026-10-18: Phenom II, Core 2 before Penryn):
// the launcher says so instead of building / starting a game that cannot run.
QString cpuMissing()
{
	int r[4] = {};
	__cpuid(r, 1);
	QStringList miss;
	const auto need = [&](int bit, const char* name) {
		if (!(r[2] & (1 << bit)))
			miss << name;
	};
	need(0, "SSE3");
	need(9, "SSSE3");
	need(13, "CMPXCHG16B");
	need(19, "SSE4.1");
	need(20, "SSE4.2");
	need(23, "POPCNT");
	return miss.join(", ");
}

QList<Gpu> gpuList()
{
	QList<Gpu> out;
	IDXGIFactory1* f = nullptr;
	if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&f))))
		return out;
	IDXGIAdapter1* a = nullptr;
	for (UINT i = 0; f->EnumAdapters1(i, &a) == S_OK; i++)
	{
		DXGI_ADAPTER_DESC1 d;
		if (SUCCEEDED(a->GetDesc1(&d)) && !(d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE))
			out.push_back({int(i), QString::fromWCharArray(d.Description)});
		a->Release();
	}
	f->Release();
	return out;
}

QString autoGpu()
{
	QString name;
	IDXGIFactory6* f = nullptr;
	if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&f))))
		return gpuList().value(0).name;
	IDXGIAdapter1* a = nullptr;
	for (UINT i = 0; name.isEmpty() && f->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&a)) == S_OK; i++)
	{
		DXGI_ADAPTER_DESC1 d;
		if (SUCCEEDED(a->GetDesc1(&d)) && !(d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE))
			name = QString::fromWCharArray(d.Description);
		a->Release();
	}
	f->Release();
	return name;
}

// this PC for the server (hash of the Windows machine GUID): no rated match between two launchers of one machine
QString machineId()
{
	const QSettings reg("HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Cryptography", QSettings::Registry64Format);
	const QByteArray guid = reg.value("MachineGuid").toString().toUtf8();
	return guid.isEmpty() ? QString() : QString::fromLatin1(QCryptographicHash::hash("t6:" + guid, QCryptographicHash::Sha256).toHex().left(32));
}

// Region of this PC for the matchmaking, guessed from the Windows time zone (the player can change it in the profile).
// Regions (user 2026-10-11): EU, USA (North America), SA, ME (Middle East), SEA, ASIA, OCE, AF
QString guessRegion()
{
	const QString tz = QString::fromLatin1(QTimeZone::systemTimeZoneId());
	const auto in = [&tz](std::initializer_list<const char*> zones) {
		for (const char* z : zones)
			if (tz.startsWith(z))
				return true;
		return false;
	};
	if (in({"America/Sao_Paulo", "America/Argentina", "America/Buenos_Aires", "America/Santiago", "America/Bogota", "America/Lima",
	        "America/Caracas", "America/Montevideo", "America/Asuncion", "America/La_Paz", "America/Guayaquil", "America/Manaus",
	        "America/Bahia", "America/Fortaleza", "America/Recife", "America/Belem", "America/Cayenne", "America/Paramaribo",
	        "America/Guyana"}))
		return "SA";
	if (in({"Asia/Dubai", "Asia/Riyadh", "Asia/Qatar", "Asia/Kuwait", "Asia/Bahrain", "Asia/Muscat", "Asia/Aden", "Asia/Tehran",
	        "Asia/Baghdad", "Asia/Jerusalem", "Asia/Tel_Aviv", "Asia/Gaza", "Asia/Hebron", "Asia/Amman", "Asia/Beirut", "Asia/Damascus"}))
		return "ME";
	if (in({"Asia/Singapore", "Asia/Kuala_Lumpur", "Asia/Kuching", "Asia/Bangkok", "Asia/Jakarta", "Asia/Pontianak", "Asia/Makassar",
	        "Asia/Jayapura", "Asia/Manila", "Asia/Ho_Chi_Minh", "Asia/Saigon", "Asia/Phnom_Penh", "Asia/Vientiane", "Asia/Yangon",
	        "Asia/Rangoon", "Asia/Brunei", "Asia/Dili"}))
		return "SEA";
	return in({"Europe/", "Atlantic/"})          ? "EU"
	       : in({"America/", "US/", "Canada/"})  ? "USA"
	       : in({"Asia/", "Indian/"})            ? "ASIA"
	       : in({"Australia/", "Pacific/"})      ? "OCE"
	       : in({"Africa/"})                     ? "AF"
	                                             : "EU";
}

QString localIp()
{
	for (const auto& iface : QNetworkInterface::allInterfaces())
	{
		if (!(iface.flags() & QNetworkInterface::IsUp) || (iface.flags() & QNetworkInterface::IsLoopBack))
			continue;
		for (const auto& e : iface.addressEntries())
			if (e.ip().protocol() == QAbstractSocket::IPv4Protocol && !e.ip().isLinkLocal())
				return e.ip().toString();
	}
	return "127.0.0.1";
}
