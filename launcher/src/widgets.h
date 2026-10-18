#pragma once
// Tekken 6 Native Online launcher - Widgets and widget helpers of the pages.

#include "common.h"

QLabel* text(const QString& t, const char* name = "muted");
QPixmap tinted(const QString& name, const QColor& color, int size);
QIcon navIcon(const QString& name);
void setButtonIcon(QPushButton* b, const QString& icon);
QPushButton* button(const QString& t, const char* name, const QString& icon = QString());
QPushButton* keyButton(const QString& t);
void addCloseButton(QDialog* d, QBoxLayout* v);
QFrame* card(const char* name = "card");
void styleField(QComboBox* box);
QWidget* dropdown(const QString& label, QComboBox* box);
QHBoxLayout* settingRow(const QString& label, std::initializer_list<QWidget*> widgets);
QLabel* hintText(const QString& t);
QComboBox* choiceBox(const QList<std::pair<QString, QVariant>>& items, const QVariant& current, int minWidth = 0);
QCheckBox* toggle(bool on);
QWidget* charGrid(int current, const std::function<void(int)>& pick);

// Sketch "Character" field: rank badge, MMR in gold (ranked), the character's name; a normal list when opened
class CharCombo : public QComboBox
{
public:
	explicit CharCombo(bool showRank);
	void setRank(const QPixmap& badge, int elo);

protected:
	void paintEvent(QPaintEvent*) override;

private:
	bool m_showRank;
	QPixmap m_badge;
	int m_elo = 0;
};

// the window's background (Sketch "BG": red light on black), scaled to cover
class BgWidget : public QWidget
{
public:
	BgWidget();

protected:
	void paintEvent(QPaintEvent*) override;
};
QLabel* sticker(const QString& t, const char* kind = "sticker");
QPixmap rankIcon(int rank, int height);

// VS card: P1 (red sticker) and P2 (blue sticker), character in Rajdhani, name, Elo; VS in the middle
class VsPanel : public QWidget
{
public:
	VsPanel();
	void set(const QJsonObject& p1, const QJsonObject& p2, const QString& top, bool sidesHidden = false);

protected:
	void paintEvent(QPaintEvent*) override;

private:
	QJsonObject m_p1, m_p2;
	QString m_top;
	bool m_sidesHidden = false;
};
void clearLayout(QLayout* l);

// a left click on a widget (leaderboard row -> that player's match history)
class ClickFilter : public QObject
{
public:
	ClickFilter(QObject* parent, std::function<void()> fn);

protected:
	bool eventFilter(QObject*, QEvent* e) override;

private:
	std::function<void()> m_fn;
};
