// Tekken 6 Native Online launcher - The package folder: the game, its build state, the settings file, the reference profile, shader storage.

#include "files.h"

// Root: the folder with build\tekken6.exe (project) or with tekken6.exe (package)
QString root()
{
	QDir d(QCoreApplication::applicationDirPath());
	for (int i = 0; i < 3; i++)
	{
		if (QFile::exists(d.filePath("build/tekken6.exe")) || QFile::exists(d.filePath("tekken6.exe")) ||
		    QFile::exists(d.filePath("builder/build_native.py")))
			return d.absolutePath();
		if (!d.cdUp())
			break;
	}
	return QCoreApplication::applicationDirPath();
}

QString rootFile(const QString& rel)
{
	return QDir(root()).filePath(rel);
}

// the game built from the player's copy (native\) first; build\ = development tree
QString gameExe()
{
	for (const char* rel : {"native/tekken6.exe", "build/tekken6.exe", "tekken6.exe"})
		if (QFile::exists(rootFile(rel)))
			return rootFile(rel);
	return rootFile("native/tekken6.exe");
}

QString gameDataRoot()
{
	return QDir(rootFile("native/game")).exists() ? rootFile("native/game") : rootFile("game");
}

// version of the game code of the package (changes = the player's tekken6.exe has to be built again)
static QString firstLine(const QString& rel)
{
	QFile f(rootFile(rel));
	return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readLine()).trimmed() : QString();
}

QString builderVersion()
{
	return firstLine("builder/build.version");
}

GameNeed gameNeed()
{
	if (!QFile::exists(rootFile("native/tekken6.exe")))
		return QFile::exists(rootFile("build/tekken6.exe")) || QFile::exists(rootFile("tekken6.exe")) ? GameOk : GameBuild;
	const bool data = QFile::exists(rootFile("native/game/default.xex")) || QFile::exists(rootFile("native/game/Default.xex"));
	const bool objs = QFile::exists(rootFile("native/obj/keys.json")) && QFile::exists(rootFile("native/objects.rsp"));
	const QString v = builderVersion();
	if (!v.isEmpty() && firstLine("native/build.version") != v)
		return data ? GameUpdate : GameBuild;
	const QString sha = firstLine("builder/prebuilt.sha");
	if (!sha.isEmpty() && firstLine("native/prebuilt.sha") != sha)
		return objs ? GameRelink : data ? GameUpdate : GameBuild;
	return GameOk;
}

QSettings settings()
{
	return QSettings(rootFile(g_instance == 1 ? "launcher_native.ini" : QString("launcher_native_%1.ini").arg(g_instance)),
	                 QSettings::IniFormat);
}

// reference profile of the package (development tree: the user/ folder the tests use)
static QString profileDir()
{
	return QDir(rootFile("profile")).exists() ? rootFile("profile") : rootFile("user");
}

QStringList filesUnder(const QString& dir)
{
	QStringList out;
	QDirIterator it(dir, QDir::Files, QDirIterator::Subdirectories);
	while (it.hasNext())
		out << QDir(dir).relativeFilePath(it.next());
	out.sort();
	return out;
}

// everything the boot state of the game depends on: executable, runtime, menu script, snapshot exclusions, profile
QString buildHash()
{
	QCryptographicHash h(QCryptographicHash::Sha1);
	const auto add = [&h](const QString& name, const QString& path) {
		h.addData(name.toUtf8());
		QFile f(path);
		if (f.open(QIODevice::ReadOnly))
			h.addData(&f);
		else
			h.addData("missing");
	};
	const QString bin = QFileInfo(gameExe()).absolutePath();
	add("exe", gameExe());
	for (const char* dll : {"rexruntime.dll", "rexgpu-xenos.dll"})
		add(dll, QDir(bin).filePath(dll));
	add("menu", rootFile("tools/menu_vs.bin"));
	add("exclude", rootFile("tools/netplay_exclude.txt"));
	for (const QString& rel : filesUnder(profileDir()))
		add("profile/" + rel, QDir(profileDir()).filePath(rel));
	return QString::fromLatin1(h.result().toHex().left(16));
}

// clean copy of the reference profile for one run of the game
bool freshProfile(const QString& to)
{
	QDir(to).removeRecursively();
	bool ok = QDir().mkpath(to);
	for (const QString& rel : filesUnder(profileDir()))
	{
		const QString dst = QDir(to).filePath(rel);
		QDir().mkpath(QFileInfo(dst).absolutePath());
		ok = QFile::copy(QDir(profileDir()).filePath(rel), dst) && ok;
	}
	return ok;
}

// Shader storage shipped with the game (tools\shader_cache, made by tools\shader_cache.py) added to the storage of
// one run folder before the game starts. The game creates every stored pipeline at its start; one it meets only in a
// match costs a hitch there. The files hold descriptions, not compiled code, so they are the same on every PC.
// The player's own records stay (after the shipped ones: a torn end of the player's file cannot cut those off).
static QList<QByteArray> shaderRecords(const QByteArray& d, int header, int fixed)
{
	QList<QByteArray> out;
	qsizetype p = header;
	while (p + (fixed ? fixed : 12) <= d.size())
	{
		qsizetype n = fixed;
		if (!fixed)  // guest shader: u64 hash, u32 dword count (31 bits) + type, the dwords
		{
			quint32 w;
			memcpy(&w, d.constData() + p + 8, 4);
			n = 12 + qsizetype(w & 0x7FFFFFFF) * 4;
			if (p + n > d.size())
				break;
		}
		out += d.mid(p, n);
		p += n;
	}
	return out;
}

void addShaderCache(const QString& runDir)
{
	if (qEnvironmentVariableIsSet("T6_NO_SHADER_CACHE"))
		return;
	const QString to = QDir(runDir).filePath("cache/shaders/shareable");
	const struct { const char* name; int header; int fixed; } files[] = {
		{"4E4D07FC.xsh", 8, 0}, {"4E4D07FC.rov.d3d12.xpso", 12, 72}};
	for (const auto& f : files)
	{
		QFile sf(rootFile(QString("tools/shader_cache/") + f.name));
		if (!sf.open(QIODevice::ReadOnly))
			continue;
		const QByteArray shipped = sf.readAll();
		if (shipped.size() <= f.header)
			continue;
		const QList<QByteArray> ours = shaderRecords(shipped, f.header, f.fixed);
		QByteArray own;
		QFile pf(QDir(to).filePath(f.name));
		if (pf.open(QIODevice::ReadOnly))
		{
			own = pf.readAll();
			pf.close();
		}
		QList<QByteArray> theirs;
		if (own.left(f.header) == shipped.left(f.header))  // same format version, else the game would drop it anyway
			theirs = shaderRecords(own, f.header, f.fixed);
		const QSet<QByteArray> have(theirs.begin(), theirs.end());
		bool missing = false;
		for (const QByteArray& r : ours)
			if (!have.contains(r))
			{
				missing = true;
				break;
			}
		if (!missing)
			continue;
		const QSet<QByteArray> shippedSet(ours.begin(), ours.end());
		QDir().mkpath(to);
		QSaveFile out(QDir(to).filePath(f.name));
		if (!out.open(QIODevice::WriteOnly))
			continue;
		out.write(shipped.left(f.header));
		for (const QByteArray& r : ours)
			out.write(r);
		for (const QByteArray& r : theirs)
			if (!shippedSet.contains(r))
				out.write(r);
		out.commit();
	}
}
