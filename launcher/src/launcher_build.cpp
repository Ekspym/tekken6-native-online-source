// Tekken 6 Native Online launcher - Building the game from the player's copy.

#include "launcher.h"

// ================= game files: build from the player's Xbox 360 copy
bool Launcher::gameBuilt() const
{
	return QFile::exists(gameExe());
}

bool Launcher::rebuildNeeded() const
{
	return QFile::exists(rootFile("native/tekken6.exe")) && gameNeed() == GameBuild;
}

// the game is usable for a match only when it is up to date (both players need the same exe)
bool Launcher::gameUsable() const
{
	return gameBuilt() && gameNeed() == GameOk;
}

// after an update: relink / incremental build on its own, without the ISO; the game starts once it is done
bool Launcher::autoUpdateGame()
{
	if (m_builder && m_builder->state() != QProcess::NotRunning)
		return true;
	const GameNeed need = gameNeed();
	if (need != GameRelink && need != GameUpdate)
		return false;
	if (m_autoBuildFailed)
		return true;  // shown in Profile; the player can build from the copy
	m_buildError.clear();
	m_autoBuild = true;
	m_buildStep = need == GameRelink ? T("Updating the game…") : T("Updating the game code… (about a minute)");
	runBuilder({need == GameRelink ? "--relink" : "--update"});
	return true;
}

void Launcher::refreshGameFiles()
{
	if (!m_gameState)
		return;
	const bool running = m_builder && m_builder->state() != QProcess::NotRunning;
	const auto s = settings();
	QString state;
	if (running)
		state = m_buildStep;
	else if (!m_buildError.isEmpty())
		state = T("Build failed: %1").arg(m_buildError);
	else if (!gameBuilt())
		state = T("Not built yet. Choose your Tekken 6 Xbox 360 copy (ISO or extracted folder) – the game is built from it on this PC.");
	else if (rebuildNeeded())
		state = T("This version needs the game built again from your copy.");
	else if (s.contains("built_at"))
		state = T("Built %1 from %2").arg(s.value("built_at").toDateTime().toString("yyyy-MM-dd HH:mm"), QFileInfo(s.value("built_from").toString()).fileName());
	else
		state = T("Ready (development build)");
	m_gameState->setText(state);
	m_gameState->setObjectName(!m_buildError.isEmpty() ? "err" : "muted");
	m_gameState->style()->unpolish(m_gameState);
	m_gameState->style()->polish(m_gameState);
	m_buildBtn->setText(running ? T("Cancel") : gameBuilt() ? T("Rebuild from my copy…") : T("Build from my Xbox 360 copy…"));
	m_buildBtn->setEnabled(!m_inMatch && !m_punching);
	m_buildBar->setVisible(running);
}

QString Launcher::builderPython() const
{
	QString bundled = rootFile("builder/python/python.exe");
	if (QFile::exists(bundled))
		return bundled;
	// (development tree: the PC's own Python.) Never the Microsoft Store stub in WindowsApps: it only prints "Python
	// was not found" and ends with code 9009 - a player whose builder\python was missing (antivirus, a package
	// that was not extracted whole) got "exit code 9009, see the log" instead of "the builder is missing" (2026-10-13)
	const QString sys = QStandardPaths::findExecutable("python");
	return sys.contains("WindowsApps", Qt::CaseInsensitive) ? QString() : sys;
}

void Launcher::chooseCopy()
{
	if (m_builder && m_builder->state() != QProcess::NotRunning)
	{
		m_builder->kill();
		return;
	}
	if (const QString miss = cpuMissing(); !miss.isEmpty())
	{
		m_buildError = T("this processor is too old for the game (it lacks %1)").arg(miss);
		refreshGameFiles();
		return;
	}
	if (!QFile::exists(rootFile("builder/build_native.py")) || builderPython().isEmpty())
	{
		m_buildError = T("the builder is missing (builder\\build_native.py, builder\\python) – extract the whole package");
		refreshGameFiles();
		return;
	}
	QMessageBox box(this);
	box.setWindowTitle(windowTitle());
	box.setText(T("Your Tekken 6 Xbox 360 copy: an ISO file or a folder with the extracted disc."));
	auto* iso = box.addButton(T("ISO file…"), QMessageBox::AcceptRole);
	auto* dir = box.addButton(T("Folder…"), QMessageBox::AcceptRole);
	box.addButton(QMessageBox::Cancel);
	box.exec();
	const QString last = settings().value("built_from").toString();
	QString path;
	if (box.clickedButton() == iso)
		path = QFileDialog::getOpenFileName(this, T("Tekken 6 ISO"), QFileInfo(last).absolutePath(), "Xbox 360 ISO (*.iso);;All files (*)");
	else if (box.clickedButton() == dir)
		path = QFileDialog::getExistingDirectory(this, T("Folder with the extracted disc"), last);
	if (!path.isEmpty())
		startBuild(path);
}

// first only the check of the copy (a wrong file is refused at once), then the build
void Launcher::startBuild(const QString& path)
{
	// m_buildChecking keeps prelaunch() from starting the game again in its finished handler (v0.1.1: the game was
	// back at once and the build's last step could not replace native\libc++.dll)
	m_buildChecking = true;
	if (m_game)
	{
		m_restartGame = true;
		command("quit");
		killGame("restart for new settings");
		m_game->waitForFinished(3000);
	}
	m_buildError.clear();
	m_buildStep = T("Checking your copy…");
	m_buildPath = path;
	m_buildChecking = true;
	runBuilder({"--check", QDir::toNativeSeparators(path)});
}

void Launcher::runBuilder(const QStringList& args)
{
	if (m_builder)
		m_builder->deleteLater();
	m_builder = new QProcess(this);
	m_builder->setWorkingDirectory(root());
	m_builder->setProcessChannelMode(QProcess::MergedChannels);
	m_builder->setProgram(builderPython());
	m_builder->setArguments(QStringList{"-u", "-X", "utf8", QDir::toNativeSeparators(rootFile("builder/build_native.py"))} + args);
	// no console window of Python
	m_builder->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* a) { a->flags |= CREATE_NO_WINDOW; });
	builderLog(QString("run %1 %2").arg(m_builder->program(), m_builder->arguments().join(' ')));
	m_buildDone.clear();
	connect(m_builder, &QProcess::readyRead, this, [this] {
		while (m_builder && m_builder->canReadLine())
			onBuildLine(QString::fromUtf8(m_builder->readLine()).trimmed());
	});
	connect(m_builder, &QProcess::finished, this, [this](int code, QProcess::ExitStatus st) { buildFinished(code, st); });
	connect(m_builder, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
		builderLog(QString("process error %1").arg(int(e)));
		if (e == QProcess::FailedToStart)
		{
			m_buildError = T("the builder could not be started");
			m_buildChecking = false;
			refreshGameFiles();
		}
	});
	m_buildBar->setValue(0);
	m_builder->start();
	refreshGameFiles();
}

// what the launcher saw of the builder: native\launcher_build.txt (next to the builder's own build.log)
void Launcher::builderLog(const QString& line)
{
	QDir().mkpath(rootFile("native"));
	QFile f(rootFile("native/launcher_build.txt"));
	if (f.open(QIODevice::Append | QIODevice::Text))
		f.write((QDateTime::currentDateTime().toString("HH:mm:ss ") + line + "\n").toUtf8());
}

void Launcher::onBuildLine(const QString& line)
{
	builderLog(line);
	if (line.startsWith("STEP "))
		m_buildStep = line.mid(5);
	else if (line.startsWith("PROGRESS "))
		m_buildBar->setValue(line.mid(9).toInt());
	else if (line.startsWith("DONE ") || line.startsWith("OK"))
		m_buildDone = line;
	else if (line.startsWith("ERROR "))
		m_buildError = line.mid(6);
	else
		return;
	refreshGameFiles();
}

void Launcher::buildFinished(int code, QProcess::ExitStatus st)
{
	const bool ok = st == QProcess::NormalExit && code == 0 && m_buildError.isEmpty();
	builderLog(QString("exit %1 %2 %3").arg(code).arg(st == QProcess::NormalExit ? "normal" : "crash", m_buildError));
	if (!ok && m_buildError.isEmpty())
		m_buildError = st == QProcess::NormalExit ? T("exit code %1, see the log").arg(code) : T("cancelled");
	if (m_buildChecking)
	{
		m_buildChecking = false;
		if (ok)
		{
			m_buildStep = T("Building the game from your copy… (this takes a while)");
			runBuilder({QDir::toNativeSeparators(m_buildPath)});
			return;
		}
	}
	else if (ok)
	{
		auto s = settings();
		if (!m_autoBuild)
		{
			s.setValue("built_at", QDateTime::currentDateTime());
			s.setValue("built_from", m_buildPath);
		}
		s.setValue("built_version", builderVersion());
	}
	if (m_autoBuild && !ok)
		m_autoBuildFailed = true;  // no loop: the error stays in Profile
	m_autoBuild = false;
	refreshGameFiles();
	if (ok)
	{
		m_bootError = false;
		m_bootFails = 0;
		m_autoBuildFailed = false;
		refreshBuildHash();
	}
	prelaunch();  // (also after a failed check / build: the game that was running before)
	refreshAll();
}

// a new tekken6.exe = a new build hash for the server's version check
void Launcher::refreshBuildHash()
{
	m_build = buildHash();
	send(helloMsg());
	if (!m_session.isEmpty())
		send({{"t", "auth"}, {"session", m_session}});
}
