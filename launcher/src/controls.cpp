// Tekken 6 Native Online launcher - Controllers and keyboard: buttons, bindings, key names, the controller panel, key capture.

#include "controls.h"
#include "files.h"

	// combination of the controller's Button layout - 1+3, 2+4, 2+3, 1+4 - as well)

int kbKey(const QString& key, int def)
{
	return settings().value("kb_" + key, def).toInt();
}

QString vkName(int vk)
{
	if (!vk)
		return QString::fromUtf8("–");
	UINT sc = MapVirtualKeyW(UINT(vk), MAPVK_VK_TO_VSC);
	switch (vk)  // extended keys: the name of the gray block, not of the numpad
	{
	case VK_LEFT: case VK_UP: case VK_RIGHT: case VK_DOWN: case VK_PRIOR: case VK_NEXT: case VK_END: case VK_HOME:
	case VK_INSERT: case VK_DELETE: case VK_DIVIDE: case VK_NUMLOCK: case VK_RCONTROL: case VK_RMENU:
		sc |= 0x100;
		break;
	default:
		break;
	}
	wchar_t name[64]{};
	if (GetKeyNameTextW(static_cast<LONG>(sc) << 16, name, 64) > 0)
		return QString::fromWCharArray(name);
	return QString("VK %1").arg(vk);
}

// T6_KEYMAP of the game for the Custom layout
QString keyMap()
{
	QStringList parts;
	for (const KbTarget& t : kKbTargets)
		if (const int vk = kbKey(t.key, t.def))
			parts << QString("%1:%2").arg(vk, 0, 16).arg(t.bits, 0, 16);
	return parts.join(',');
}

// WinMM joysticks that are connected: (index, name)
QList<QPair<int, QString>> mmDevices()
{
	QList<QPair<int, QString>> out;
	for (UINT i = 0; i < joyGetNumDevs() && i < 16; i++)
	{
		JOYINFOEX ji{sizeof(ji), JOY_RETURNALL};
		JOYCAPSW caps{};
		if (joyGetPosEx(i, &ji) == JOYERR_NOERROR && joyGetDevCapsW(i, &caps, sizeof(caps)) == JOYERR_NOERROR)
			out.push_back({int(i), QString::fromWCharArray(caps.szPname)});
	}
	return out;
}

PadPanel::PadPanel() { setFixedSize(980, 470); }

// key: the MmTarget / PadButton key (case does not matter); false when the pad has no place for it
bool PadPanel::add(const QString& key, const QString& label, QWidget* field)
{
	static const QHash<QString, QPoint> at = {
		{"LT", {110, 0}}, {"LB", {110, 75}}, {"RT", {710, 0}}, {"RB", {710, 75}},
		{"BACK", {320, 90}}, {"START", {500, 90}},
		{"UP", {110, 160}}, {"LEFT", {0, 235}}, {"RIGHT", {220, 235}}, {"DOWN", {110, 310}},
		{"Y", {710, 160}}, {"X", {600, 235}}, {"B", {820, 235}}, {"A", {710, 310}},
		{"LS", {320, 320}}, {"RS", {500, 320}}};
	const auto it = at.find(key.toUpper());
	if (it == at.end())
		return false;
	auto* l = new QLabel(label.toUpper(), this);
	l->setObjectName("ddLabel");
	l->move(*it);
	field->setParent(this);
	field->setFixedWidth(kField);
	field->move(it->x(), it->y() + 20);
	return true;
}

void PadPanel::paintEvent(QPaintEvent*)
{
	// a rounded body (shoulder buttons on its top edge, the sticks inside at the bottom) and two rounded grips
	QPainterPath body, grips;
	body.addRoundedRect(QRectF(40, 110, 900, 295), 110, 110);
	grips.addRoundedRect(QRectF(40, 260, 290, 205), 100, 100);
	grips.addRoundedRect(QRectF(650, 260, 290, 205), 100, 100);
	QPainter p(this);
	p.setRenderHint(QPainter::Antialiasing);
	p.setPen(QPen(QColor(255, 255, 255, 46), 2));
	p.setBrush(QColor(255, 255, 255, 10));
	p.drawPath(body.united(grips));
}

KeyGrab::KeyGrab(QObject* parent, std::function<void(int)> done) : QObject(parent), m_done(std::move(done)) {}

bool KeyGrab::eventFilter(QObject*, QEvent* e)
{
	if (e->type() != QEvent::KeyPress || !m_done)
		return false;
	const int vk = int(static_cast<QKeyEvent*>(e)->nativeVirtualKey());
	auto done = std::move(m_done);
	m_done = nullptr;
	done(vk);
	return true;
}
