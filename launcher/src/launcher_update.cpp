// Tekken 6 Native Online launcher - Updates of the package from the GitHub releases.

#include "launcher.h"

// ================= update: the latest GitHub release with a patch zip
void Launcher::checkForUpdate()
{
	for (const QString& f : QDir(root()).entryList({"*.old"}, QDir::Files))
		QFile::remove(rootFile(f));
	QDir(rootFile("update_tmp")).removeRecursively();
	std::wstring api = qEnvironmentVariableIsSet("T6_UPDATE_API") ? qEnvironmentVariable("T6_UPDATE_API").toStdWString() : std::wstring(kReleaseApi);
	if (api.empty())
		return;
	// test copy: the list of releases with the pre-releases, the newest version of all of them
	if (testChannel() && !qEnvironmentVariableIsSet("T6_UPDATE_API") && api.size() > 7 && api.compare(api.size() - 7, 7, L"/latest") == 0)
		api = api.substr(0, api.size() - 7) + L"?per_page=30";
	QPointer<Launcher> self(this);
	std::thread([self, api] {
		QByteArray body;
		if (httpGet(api.c_str(), body) != 200)
			return;
		const QJsonDocument doc = QJsonDocument::fromJson(body);
		QJsonObject rel = doc.object();
		if (doc.isArray())  // (a list: the highest version that has the patch zip)
		{
			qint64 best = 0;
			for (const QJsonValue v : doc.array())
			{
				const QJsonObject o = v.toObject();
				bool patch = false;
				for (const QJsonValue a : o.value("assets").toArray())
					patch = patch || a.toObject().value("name").toString() == kPatchAsset;
				// (a test copy only follows TEST versions - user 2026-10-13: "testovaci hra se nemusi umet patchovat z
				//  real releasu"; the tester's normal installation gets the real ones)
				if (testChannel() && !o.value("tag_name").toString().contains("-test"))
					continue;
				if (const qint64 k = versionKey(o.value("tag_name").toString()); patch && !o.value("draft").toBool() && k > best)
				{
					best = k;
					rel = o;
				}
			}
		}
		const QString tag = rel.value("tag_name").toString();
		if (versionKey(tag) <= versionKey(kVersion))
			return;
		for (const QJsonValue a : rel.value("assets").toArray())
		{
			const QJsonObject o = a.toObject();
			if (o.value("name").toString() != kPatchAsset)
				continue;
			const QString url = o.value("browser_download_url").toString();
			QString sha = o.value("digest").toString();
			sha = sha.startsWith("sha256:") ? sha.mid(7) : QString();
			QMetaObject::invokeMethod(qApp, [self, tag, url, sha, api] {
				if (!self)
					return;
				self->m_updateApi = api;
				self->m_updateTag = tag;
				self->m_updateUrl = url;
				self->m_updateSha = sha;
				self->showUpdate();
				if (g_updateTest)
					self->installUpdate();
			}, Qt::QueuedConnection);
			return;
		}
	}).detach();
}

void Launcher::showUpdate()
{
	if (!m_updateBtn)
		return;
	// no update: the same place opens the patch notes (user 2026-10-14)
	const bool update = !m_updateUrl.isEmpty();
	// Sketch: Btn Update (gold, glow, download icon) bottom right; without an update a small Default button
	if (m_updateBtn->objectName() != (update ? "update" : "ghostSmall"))
	{
		m_updateBtn->setObjectName(update ? "update" : "ghostSmall");
		m_updateBtn->style()->unpolish(m_updateBtn);
		m_updateBtn->style()->polish(m_updateBtn);
		if (update)
		{
			auto* glow = new QGraphicsDropShadowEffect(m_updateBtn);
			glow->setColor(QColor(0xEF, 0xBC, 0x23, 150));
			glow->setBlurRadius(40);
			glow->setOffset(0, 3);
			m_updateBtn->setGraphicsEffect(glow);
		}
		else
			m_updateBtn->setGraphicsEffect(nullptr);
		setButtonIcon(m_updateBtn, update ? "download" : QString());
	}
	m_updateBtn->setEnabled(!m_updating);
	m_updateBtn->setText(m_updating ? m_updateStatus : update ? T("Update available – %1").arg(m_updateTag) : T("Patch notes"));
}

// the GitHub release notes of this version and the ones before it (newest first); a test copy shows its test versions
void Launcher::showPatchNotes()
{
	auto* d = new QDialog(this);
	d->setAttribute(Qt::WA_DeleteOnClose);
	d->setWindowTitle(T("Patch notes"));
	d->resize(820, 640);
	auto* v = new QVBoxLayout(d);
	v->setContentsMargins(24, 20, 24, 20);
	v->setSpacing(10);
	v->addWidget(text(T("Patch notes"), "gold"));
	auto* view = new QTextBrowser;
	view->setOpenExternalLinks(true);
	view->setFrameShape(QFrame::NoFrame);
	view->setPlainText(T("Loading…"));
	v->addWidget(view, 1);
	addCloseButton(d, v);
	d->show();
	if (!m_patchNotes.isEmpty())
	{
		view->setMarkdown(m_patchNotes);
		return;
	}
	std::wstring api(kReleaseApi);
	if (api.size() > 7 && api.compare(api.size() - 7, 7, L"/latest") == 0)
		api = api.substr(0, api.size() - 7) + L"?per_page=15";
	QPointer<Launcher> self(this);
	QPointer<QTextBrowser> target(view);
	std::thread([self, target, api] {
		QByteArray body;
		QString md;
		if (httpGet(api.c_str(), body) == 200)
			for (const QJsonValue r : QJsonDocument::fromJson(body).array())
			{
				const QJsonObject o = r.toObject();
				const QString tag = o.value("tag_name").toString();
				if (o.value("draft").toBool() || versionKey(tag) > versionKey(kVersion) || tag.contains("-test") != testChannel())
					continue;
				md += QString("## %1  ·  %2\n\n%3\n\n---\n\n")
				          .arg(tag, o.value("published_at").toString().left(10), o.value("body").toString().trimmed());
			}
		QMetaObject::invokeMethod(qApp, [self, target, md] {
			if (self && !md.isEmpty())
				self->m_patchNotes = md;
			if (target)
				md.isEmpty() ? target->setPlainText(T("Could not load the patch notes (no connection to GitHub)."))
				             : target->setMarkdown(md);
		}, Qt::QueuedConnection);
	}).detach();
}

// Downloads the patch zip, checks its SHA-256 (GitHub's digest), extracts it with the system tar and replaces the
// files next to the launcher (never game\, native\, runs\, the settings). The running launcher is renamed to *.old.
void Launcher::installUpdate()
{
	if (m_updating || m_updateUrl.isEmpty())
		return;
	if (m_inMatch || m_punching || (m_builder && m_builder->state() != QProcess::NotRunning))
	{
		QMessageBox::information(this, windowTitle(), T("Finish the match / the build first."));
		return;
	}
	m_updating = true;
	m_updateStatus = T("Downloading…");
	showUpdate();
	QPointer<Launcher> self(this);
	std::wstring url = m_updateUrl.toStdWString(), api = m_updateApi;
	QString sha = m_updateSha;
	std::thread([self, url = std::move(url), sha = std::move(sha), api = std::move(api)] {
		QByteArray zip;
		QString error;
		try
		{
		const int status = httpGet(url.c_str(), zip, [self](qint64 got, qint64 total) {
			if (total <= 0)
				return;
			const int pct = int(got * 100 / total);
			QMetaObject::invokeMethod(qApp, [self, pct] {
				if (self)
				{
					// (at 100 % the checksum, extracting and copying follow: "Installing…", not a stuck 100 % - user 2026-10-14)
					self->m_updateStatus = pct >= 100 ? T("Installing…") : T("Downloading… %1 %").arg(pct);
					self->showUpdate();
				}
			}, Qt::QueuedConnection);
		});
		if (status != 200 || zip.isEmpty())
			error = T("The download failed (HTTP %1).").arg(status);
		else if (const QByteArray got = QCryptographicHash::hash(zip, QCryptographicHash::Sha256).toHex();
		         sha.isEmpty() || got != sha.toLatin1().toLower())
		{
			// (no digest from GitHub = not installed unchecked either: the same second look, review 2026-10-18)
			// v2.1.6: the files of a release can be replaced after this launcher saw it (v2.1.5, 2026-10-15: players got
			// "damaged" until they restarted the launcher) - ask GitHub again for the digest of the same file
			const QString fresh = assetDigest(api, QString::fromStdWString(url));
			if (fresh.isEmpty())
				error = T("GitHub gave no checksum for the update - not installed. Try again later.");
			else if (got != fresh.toLatin1().toLower())
				error = T("The downloaded file is damaged (checksum mismatch).");
		}
			QMetaObject::invokeMethod(qApp, [self, zip, error] {
				if (self)
					self->finishUpdate(zip, error);
			}, Qt::QueuedConnection);
		}
		catch (...)  // (nothing may leave a thread: out of memory - the update does not finish, the button stays)
		{
			g_updateWorkerFailures.fetch_add(1);
		}
	}).detach();
}

// GitHub's current SHA-256 of the release asset with this download url (the release api: one release or a list)
QString Launcher::assetDigest(const std::wstring& api, const QString& url)
{
	QByteArray body;
	if (api.empty() || httpGet(api.c_str(), body) != 200)
		return QString();
	const QJsonDocument doc = QJsonDocument::fromJson(body);
	QJsonArray rels = doc.isArray() ? doc.array() : QJsonArray{doc.object()};
	for (const QJsonValue r : rels)
		for (const QJsonValue a : r.toObject().value("assets").toArray())
			if (a.toObject().value("browser_download_url").toString() == url)
			{
				const QString d = a.toObject().value("digest").toString();
				return d.startsWith("sha256:") ? d.mid(7) : QString();
			}
	return QString();
}

void Launcher::finishUpdate(const QByteArray& zip, QString error)
{
	if (error.isEmpty() && m_updateBtn)
	{
		// the rest runs on this thread (tar, file copies): paint the state now, the event loop is blocked until the end
		m_updateStatus = T("Installing…");
		showUpdate();
		m_updateBtn->repaint();
	}
	const QString tmp = rootFile("update_tmp");
	QDir(tmp).removeRecursively();
	QDir().mkpath(tmp + "/files");
	if (error.isEmpty())
	{
		QFile f(tmp + "/patch.zip");
		if (!f.open(QIODevice::WriteOnly) || f.write(zip) != zip.size())
			error = T("Cannot write into the launcher's folder.");
	}
	if (error.isEmpty())
	{
		const QString tar = QDir(qEnvironmentVariable("SystemRoot", "C:/Windows")).filePath("System32/tar.exe");
		if (QProcess::execute(tar, {"-xf", QDir::toNativeSeparators(tmp + "/patch.zip"), "-C", QDir::toNativeSeparators(tmp + "/files")}) != 0)
			error = T("Extracting the update failed.");
	}
	if (error.isEmpty() && m_game)
	{
		// m_updating keeps prelaunch() from starting the game again in its finished handler (v0.1 started it at
		// once and the copy then found its files in use)
		m_restartGame = true;
		command("quit");
		killGame("update");
		m_game->waitForFinished(3000);
	}
	if (error.isEmpty())
	{
		// All or nothing (review 2026-10-18: a file locked half way left half old, half new files - "different
		// version" or a launcher that does not start). 1: every new file next to its target as <name>.new (nothing
		// replaced yet); 2: each target to <name>.old (also the running launcher and loaded DLLs: a rename works),
		// <name>.new to <name>; a failure there puts the .old files back. The .old files go at the next update check.
		QStringList todo;
		QDirIterator it(tmp + "/files", QDir::Files, QDirIterator::Subdirectories);
		while (error.isEmpty() && it.hasNext())
		{
			const QString src = it.next();
			const QString rel = QDir(tmp + "/files").relativeFilePath(src);
			if (keptByUpdate(rel))
				continue;
			const QString dst = rootFile(rel);
			if (sameFile(src, dst))
				continue;  // unchanged (also the Qt DLLs this launcher has loaded)
			QDir().mkpath(QFileInfo(dst).absolutePath());
			QFile::remove(dst + ".new");
			if (!QFile::copy(src, dst + ".new"))
				error = T("Copying %1 failed.").arg(rel);
			else
				todo << rel;
		}
		QStringList done;  // replaced (their old file is <name>.old, or there was none: "!" in front)
		for (const QString& rel : todo)
		{
			if (!error.isEmpty())
				break;
			const QString dst = rootFile(rel);
			const bool had = QFileInfo::exists(dst);
			if (had)
			{
				QFile::remove(dst + ".old");
				if (!QFile::rename(dst, dst + ".old"))
				{
					error = T("%1 is in use – close the game and try again.").arg(rel);
					break;
				}
			}
			if (!QFile::rename(dst + ".new", dst))
			{
				if (had)
					QFile::rename(dst + ".old", dst);
				error = T("Copying %1 failed.").arg(rel);
				break;
			}
			done << (had ? rel : "!" + rel);
		}
		if (!error.isEmpty())
		{
			for (const QString& d : done)
			{
				const bool had = !d.startsWith('!');
				const QString dst = rootFile(had ? d : d.mid(1));
				QFile::remove(dst);
				if (had)
					QFile::rename(dst + ".old", dst);
			}
			for (const QString& rel : todo)
				QFile::remove(rootFile(rel) + ".new");
		}
		else
			for (const QString& d : done)  // (the files no process holds go now; the launcher / loaded DLLs at the next check)
				if (!d.startsWith('!'))
					QFile::remove(rootFile(d) + ".old");
	}
	QDir(tmp).removeRecursively();
	m_updating = false;
	if (g_updateTest)
	{
		QFile out(rootFile("update_result.txt"));
		if (out.open(QIODevice::WriteOnly | QIODevice::Text))
			out.write((error.isEmpty() ? "ok " + m_updateTag : error).toUtf8());
		qApp->quit();
		return;
	}
	if (!error.isEmpty())
	{
		m_updateStatus = error;
		showUpdate();
		prelaunch();
		QMessageBox::warning(this, windowTitle(), error);
		return;
	}
	QMessageBox::information(this, windowTitle(), T("Updated to %1. The launcher starts again now.").arg(m_updateTag));
	QProcess::startDetached(QCoreApplication::applicationFilePath(), {});
	qApp->quit();
}
