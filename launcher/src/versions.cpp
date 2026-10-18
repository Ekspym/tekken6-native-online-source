// Tekken 6 Native Online launcher - Release versions and the files an update keeps.

#include "versions.h"
#include "files.h"

// "v1.12" / "1.12.3" -> 1012003 (0 when not a version)
static int versionNumber(QString v)
{
	if (v.startsWith('v'))
		v = v.mid(1);
	const QStringList p = v.split('.');
	bool ok1 = false, ok2 = true, ok3 = true;
	const int major = p.value(0).toInt(&ok1), minor = p.size() > 1 ? p[1].toInt(&ok2) : 0, patch = p.size() > 2 ? p[2].toInt(&ok3) : 0;
	return ok1 && ok2 && ok3 ? (major * 1000 + minor) * 1000 + patch : 0;
}

// TEST CHANNEL (user 2026-10-13: test versions for chosen people, "at se pak sama updatuje"). A package with the
// file test_channel.txt next to the launcher is a test copy: it follows the newest release of the repository
// INCLUDING pre-releases (test versions are published as pre-releases, tag "v2.0.3-test.4"; the normal launcher
// asks for /releases/latest, which never is a pre-release, so nobody else gets them). A test copy takes ONLY test
// versions, never a real release: the tester keeps his normal installation next to it. Its game has another
// fingerprint than the public one, so the server pairs test players only with each other. The file is in the test
// package and in its patch zip; an update never removes files, so the copy stays a test copy. Who may HAVE a test
// copy is decided by who is given the package - an allow list of accounts needs the server (not done).
bool testChannel()
{
	return QFile::exists(rootFile("test_channel.txt"));
}

// "v2.0.3-test.4" -> order of versions with test builds: 2.0.3-test.1 < 2.0.3-test.4 < 2.0.3 (final) < 2.0.4-test.1
qint64 versionKey(const QString& tag)
{
	const int at = tag.indexOf("-test");
	const qint64 base = versionNumber(at < 0 ? tag : tag.left(at));
	if (base == 0)
		return 0;
	int n = 9999;  // (a final version is after all its test builds)
	if (at >= 0)
	{
		QString t = tag.mid(at + 5);
		if (t.startsWith('.'))
			t = t.mid(1);
		bool ok = false;
		n = t.toInt(&ok);
		if (!ok || n < 0 || n >= 9999)
			return 0;
	}
	return base * 10000 + n;
}

// same size and content: the update leaves the file alone
bool sameFile(const QString& a, const QString& b)
{
	QFile fa(a), fb(b);
	if (!fa.open(QIODevice::ReadOnly) || !fb.open(QIODevice::ReadOnly) || fa.size() != fb.size())
		return false;
	QCryptographicHash ha(QCryptographicHash::Sha1), hb(QCryptographicHash::Sha1);
	ha.addData(&fa);
	hb.addData(&fb);
	return ha.result() == hb.result();
}

// a path of the patch zip that must never be replaced: the player's game, his build, runs, settings
bool keptByUpdate(const QString& rel)
{
	const QString r = QDir::fromNativeSeparators(rel).toLower();
	for (const char* dir : {"game/", "native/", "runs/", "user/", "user2/", "update_tmp/"})
		if (r.startsWith(dir))
			return true;
	return r.startsWith("launcher_native") && r.endsWith(".ini");
}
