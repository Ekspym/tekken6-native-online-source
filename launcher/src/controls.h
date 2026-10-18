#pragma once
// Tekken 6 Native Online launcher - Controllers and keyboard: buttons, bindings, key names, the controller panel, key capture.

#include "common.h"

// ---- controls (Settings -> CONTROLLER, BUTTON MAPPING): input backend of the game, WinMM joystick bindings, Tekken button layout
// pad buttons the layout can change (names = T6_PAD_MAP of the game, src/t6_netplay.cpp RemapPad)
struct PadButton
{
	const char* key;
	const char* label;
};

inline const PadButton kPadButtons[] = {{"A", "A / Cross"}, {"B", "B / Circle"}, {"X", "X / Square"}, {"Y", "Y / Triangle"},
	{"LB", "LB / L1"}, {"RB", "RB / R1"}, {"LT", "LT / L2"}, {"RT", "RT / R2"}, {"LS", "LS / L3"}, {"RS", "RS / R3"},
	{"BACK", "Back / Share"}, {"START", "Start / Options"}};

// what a button can do: XINPUT wButtons of the game (X = 1 left punch, Y = 2 right punch, A = 3 left kick, B = 4 right kick)
struct PadAction
{
	int value;  // -1 = the button's own meaning (not in T6_PAD_MAP)
	const char* label;
};

inline const PadAction kPadActions[] = {{-1, "Default"}, {0x4000, "1  Left punch"}, {0x8000, "2  Right punch"},
	{0x1000, "3  Left kick"}, {0x2000, "4  Right kick"}, {0xC000, "1+2"}, {0x3000, "3+4"}, {0x5000, "1+3"}, {0xA000, "2+4"},
	{0x9000, "2+3"}, {0x6000, "1+4"}, {0xF000, "1+2+3+4"}, {0x0010, "Start"}, {0, "Nothing"}};

// targets of the WinMM joystick mapping (mmjoystick.ini of the SDK) and their defaults
struct MmTarget
{
	const char* key;
	const char* label;
	const char* def;
};

inline const MmTarget kMmTargets[] = {{"Up", "Up", "POV Up"}, {"Down", "Down", "POV Down"}, {"Left", "Left", "POV Left"},
	{"Right", "Right", "POV Right"}, {"X", "X / Square  (1)", "Button 1"}, {"Y", "Y / Triangle  (2)", "Button 4"},
	{"A", "A / Cross  (3)", "Button 2"}, {"B", "B / Circle  (4)", "Button 3"}, {"LB", "LB / L1", "Button 5"},
	{"RB", "RB / R1", "Button 6"}, {"LT", "LT / L2", "Button 7"}, {"RT", "RT / R2", "Button 8"},
	{"Back", "Back / Share", "Button 9"}, {"Start", "Start / Options", "Button 10"}, {"LS", "LS / L3", "Button 11"},
	{"RS", "RS / R3", "Button 12"}};

// keyboard Custom layout: action -> XINPUT wButtons of the game (T6_KEYMAP vk:bits) and the default key (WASD layout)
struct KbTarget
{
	const char* key;
	const char* label;
	int bits;
	int def;  // virtual key
};

inline const KbTarget kKbTargets[] = {{"up", "Up", 0x0001, 'W'}, {"down", "Down", 0x0002, 'S'}, {"left", "Left", 0x0004, 'A'},
	{"right", "Right", 0x0008, 'D'}, {"lp", "1  Left punch", 0x4000, 'U'}, {"rp", "2  Right punch", 0x8000, 'I'},
	{"lk", "3  Left kick", 0x1000, 'J'}, {"rk", "4  Right kick", 0x2000, 'K'}, {"lprp", "1+2", 0xC000, 0},
	{"lkrk", "3+4", 0x3000, 0}, {"lplk", "1+3", 0x5000, 0},
	{"rprk", "2+4", 0xA000, 0}, {"rplk", "2+3", 0x9000, 0}, {"lprk", "1+4", 0x6000, 0}, {"all", "1+2+3+4", 0xF000, 0}, {"start", "Start", 0x0010, VK_RETURN},
	{"select", "Select / Back", 0x0020, VK_BACK}};  // (user 2026-10-14: the keyboard had no Select; 2026-10-18: every
int kbKey(const QString& key, int def);
QString vkName(int vk);
QString keyMap();
QList<QPair<int, QString>> mmDevices();

// Settings -> CONTROLLER buttons / BUTTON MAPPING: each field where its button sits on a controller (user 2026-10-18:
// "stejny layout jako je PS controller aby to bylo vic intuitivni"), over a faint generic gamepad outline (rounded
// shapes only - no copy of any real controller's design, user: "nesmi byt narusenej zadnej copyright")
class PadPanel : public QWidget
{
public:
	static constexpr int kField = 160;
	PadPanel();

	bool add(const QString& key, const QString& label, QWidget* field);

protected:
	void paintEvent(QPaintEvent*) override;
};

// event filter of the key capture (Settings -> KEYBOARD MAPPING, Custom): the virtual key of the next key press
class KeyGrab : public QObject
{
public:
	KeyGrab(QObject* parent, std::function<void(int)> done);

protected:
	bool eventFilter(QObject*, QEvent* e) override;

private:
	std::function<void(int)> m_done;
};
