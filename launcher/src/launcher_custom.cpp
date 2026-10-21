// Tekken 6 Native Online launcher - Costumes and custom looks.

#include "launcher.h"

int Launcher::costume(int index) const
{
	const int k = hasCostumes(index) ? m_costumes[QString::number(index)].toInt() : 0;
	return k == 2 && !p3Code(index) ? 1 : k;
}

void Launcher::refreshCostume()
{
	if (!m_costumeBox)
		return;
	const bool two = hasCostumes(m_char);
	{
		const QSignalBlocker b(m_costumeBox);
		m_costumeBox->clear();
		for (int k = 0; k < (p3Code(m_char) ? 3 : 2); k++)  // (P3: only the 15 characters with a third model)
			m_costumeBox->addItem(T("P%1 costume").arg(k + 1), k);
		m_costumeBox->setCurrentIndex(qMax(0, m_costumeBox->findData(costume(m_char))));
		m_costumeBox->setEnabled(two);
	}
	m_costumeNote->setText(two ? QString() : T("KUMA and PANDA have one costume."));
	if (m_customizeBtn)
	{
		const bool stored = validRecord(storedRecord(m_char, costume(m_char)));
		const bool has = validRecord(customRecord(m_char, costume(m_char)));
		m_customizeBtn->setText(m_customize ? T("Customizing…") : T("Customize look"));
		m_customizeBtn->setEnabled(!m_customize && !m_practice && gameUsable() && m_gameReady && !m_inMatch && !m_punching);
		m_customResetBtn->setText(stored && !has ? T("Use my custom look") : T("Normal look"));
		m_customResetBtn->setEnabled(stored && !m_customize);
		m_customNote->setText(m_customize && !m_customizeShown ? T("Opening Customize in the game... the first time it can take up to 2 minutes.")
		                      : m_customize ? T("Make the look in the game, then leave Customize with B / Esc - the game saves it and closes.")
		                      : has ? T("This costume has your custom look - your opponents see it.")
		                      : stored ? T("Normal look. Your custom look is kept - \"Use my custom look\" switches it on again.")
		                               : T("Normal look. Customize: every item unlocked, 765,000,000 G."));
	}
}

// The Look choice next to the character on the Ranked / Casual pages (user 2026-10-20: "hned vedle characteru udelat
// customizace vyber"): the costumes of the character, and each costume with a custom look once more as "my custom
// look"; data = costume * 2 + 1 for the custom look. The same settings as Profile -> Costume / Look.
void Launcher::fillLookBox(QComboBox* box) const
{
	const QSignalBlocker b(box);
	box->clear();
	const int n = p3Code(m_char) ? 3 : hasCostumes(m_char) ? 2 : 1;
	for (int k = 0; k < n; k++)
	{
		box->addItem(n == 1 ? T("Normal") : T("P%1 costume").arg(k + 1), k * 2);
		if (validRecord(storedRecord(m_char, k)))
			box->addItem(n == 1 ? T("My custom look") : T("P%1 – my custom look").arg(k + 1), k * 2 + 1);
	}
	const int k = costume(m_char);
	box->setCurrentIndex(qMax(0, box->findData(k * 2 + (validRecord(customRecord(m_char, k)) ? 1 : 0))));
	box->setEnabled(box->count() > 1);
}

void Launcher::pickLook(int look)
{
	const int k = look / 2;
	if (hasCostumes(m_char) && k)
		m_costumes[QString::number(m_char)] = k;
	else
		m_costumes.remove(QString::number(m_char));
	const QString key = QString("%1_%2").arg(m_char).arg(k);
	if (look & 1)
		m_customOff.remove(key);
	else if (validRecord(storedRecord(m_char, k)))
		m_customOff[key] = true;
	settings().setValue("costumes", QJsonDocument(m_costumes).toJson(QJsonDocument::Compact));
	settings().setValue("custom_looks_off", QJsonDocument(m_customOff).toJson(QJsonDocument::Compact));
	refreshCostume();
	sendProfile();
}

QString Launcher::customRecord(int index, int k) const
{
	if (!kCustomLooks || m_customOff.contains(QString("%1_%2").arg(index).arg(k)))
		return QString();  // (switched off: Normal look)
	return m_customs[QString("%1_%2").arg(index).arg(k)].toString();
}

// the stored look, also when it is switched off
QString Launcher::storedRecord(int index, int k) const
{
	return kCustomLooks ? m_customs[QString("%1_%2").arg(index).arg(k)].toString() : QString();
}

// a record worth injecting: 160 hex digits with at least one item (an all-zero record = no clothes at all)
bool Launcher::validRecord(const QString& rec)
{
	if (rec.size() != 160)
		return false;
	const QByteArray b = QByteArray::fromHex(rec.toLatin1());
	if (b.size() != 80)
		return false;
	for (int i = 0; i < 44; i += 2)
	{
		const int id = (uchar(b[i]) << 8) | uchar(b[i + 1]);
		if (id != 0 && id != 0xFFFF)
			return true;
	}
	return false;
}

// The game in its Customize menu (a second instance like the practice): the menu path up to the Offline menu
// (tools/menu_vs.bin), then Profile -> Customize -> the character grid, where the game itself takes this character
// (T6_CUSTOM_CHAR); own profile folder custom_profile (the looks and the bought items stay for the next time)
void Launcher::startCustomize()
{
	if (!kCustomLooks || m_customize || m_practice || !gameUsable() || !m_gameReady || m_inMatch || m_punching)
		return;
	const QString runDir = rootFile(QString("runs/customize_%1").arg(g_instance));
	QDir().mkpath(runDir);
	auto env = gameEnvironment();
	m_customizeChar = m_char;
	m_customizeOut = QDir(runDir).filePath("custom_out.txt");
	QFile::remove(m_customizeOut);
	QFile::remove(m_customizeOut + ".state");
	env.insert("T6_CUSTOM_OUT", QDir::toNativeSeparators(m_customizeOut));
	env.insert("T6_NET_STATUS", QDir::toNativeSeparators(QDir(runDir).filePath("status.txt")));
	env.insert("T6_PLAY", QDir::toNativeSeparators(rootFile("tools/menu_vs.bin")));
	env.insert("T6_PLAY_UNTIL", "1630");
	// B (Offline menu -> main menu), down down A (PROFILE), down A (CUSTOMIZE), A (the character grid)
	// (tests: T6_CUSTOMIZE_TEST_SEQ = more scripted inputs after it, the player's part)
	env.insert("T6_MENU_SEQ", "0:60,2000:3,0:60,2:3,0:15,2:3,0:15,1000:3,0:40,2:3,0:15,1000:3,0:120,1000:3" +
	                              (qEnvironmentVariableIsSet("T6_CUSTOMIZE_TEST_SEQ") ? "," + qEnvironmentVariable("T6_CUSTOMIZE_TEST_SEQ") : QString()));
	env.insert("T6_CUSTOM_CHAR", QString::number(charCode(m_char, 0)));
	env.insert("T6_MONEY", "765000000");
	env.insert("T6_UNLOCK_ALL", "1");
	env.insert("T6_FIX_SEED", "1");
	env.insert("T6_VIRTUAL_TB", "1");
	env.insert("T6_NET_KEYS", QString::number(m_keys));
	addGameEnv(env, false);
	const QString user = rootFile("custom_profile");
	if (filesUnder(user).isEmpty())
		freshProfile(user);
	addShaderCache(runDir);
	m_customize = new QProcess(this);
	m_customize->setProcessEnvironment(env);
	m_customize->setWorkingDirectory(QFileInfo(gameExe()).absolutePath());
	m_customize->setProgram(gameExe());
	m_customize->setArguments(gameArgs(user, runDir));
	// hidden through the boot and the menus; shown when the character's Customize screen is there
	m_customize->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* a) {
		a->startupInfo->dwFlags |= STARTF_USESHOWWINDOW;
		a->startupInfo->wShowWindow = SW_HIDE;
	});
	m_customizeShown = false;
	connect(m_customize, &QProcess::finished, this, [this](int code) {
		if (!m_customize)
			return;
		testLog(QString("customize finished: code %1").arg(code));
		m_customizePoll.stop();
		m_customize->deleteLater();
		m_customize = nullptr;
		finishCustomize();
	});
	m_customizePoll.disconnect();
	connect(&m_customizePoll, &QTimer::timeout, this, [this] {
		if (!m_customize || !m_customize->processId() || m_customizeShown)
			return;
		const DWORD pid = static_cast<DWORD>(m_customize->processId());
		QFile f(m_customizeOut + ".state");
		const bool ready = f.open(QIODevice::ReadOnly) && f.readAll().startsWith("customize ready");
		showWindowsOf(pid, ready, m_fullscreen);
		if (ready)
		{
			m_customizeShown = true;
			m_customizePoll.stop();
			refreshCostume();
		}
	});
	m_customizePoll.start(100);
	m_customize->start();
	endWithLauncher(m_customize->processId());
	refreshCostume();
	refreshPractice();
}

// the game closed: for each costume of the character, the record of its model (the game's 1P / 2P model) when the
// player gave it items
void Launcher::finishCustomize()
{
	QFile f(m_customizeOut);
	QMap<int, int> modelOf;  // code -> model
	QMap<int, QString> rec, start;  // model -> record now / when the editor opened
	if (f.open(QIODevice::ReadOnly | QIODevice::Text))
		for (const QString& line : QString::fromLatin1(f.readAll()).split('\n'))
		{
			const QStringList w = line.trimmed().split(' ');
			if (w.size() == 4 && w[0] == "code" && w[2] == "model")
				modelOf[w[1].toInt()] = w[3].toInt();
			else if (w.size() == 3 && w[0] == "model")
				rec[w[1].toInt()] = w[2].toUpper();
			else if (w.size() == 3 && w[0] == "start")
				start[w[1].toInt()] = w[2].toUpper();
		}
	if (!rec.isEmpty() && m_customizeChar >= 0)
	{
		for (int k = 0; k < (p3Code(m_customizeChar) ? 3 : hasCostumes(m_customizeChar) ? 2 : 1); k++)
		{
			// only a model the player changed in this session (opening the editor alone is no custom look)
			const int m = modelOf.value(charCode(m_customizeChar, k), k);
			const QString r = rec.value(m);
			if (validRecord(r) && r != start.value(m))
			{
				m_customs[QString("%1_%2").arg(m_customizeChar).arg(k)] = r;
				m_customOff.remove(QString("%1_%2").arg(m_customizeChar).arg(k));  // (a new look is used)
			}
		}
		settings().setValue("custom_looks", QJsonDocument(m_customs).toJson(QJsonDocument::Compact));
		settings().setValue("custom_looks_off", QJsonDocument(m_customOff).toJson(QJsonDocument::Compact));
		sendProfile();
	}
	refreshCostume();
	refreshPractice();
}
