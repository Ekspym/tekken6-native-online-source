// Tekken 6 Native Online launcher - Widgets and widget helpers of the pages.

#include "widgets.h"
#include "characters.h"
#include "style.h"

QLabel* text(const QString& t, const char* name)
{
	auto* l = new QLabel(t);
	l->setObjectName(name);
	// only running text wraps; titles, the logo, chips and the status stay on one line
	l->setWordWrap(QByteArray(name) == "muted" || QByteArray(name) == "err");
	return l;
}

// Material Symbols icon (launcher/icons, Apache 2.0) in one colour; drawn at 2x for high DPI screens
QPixmap tinted(const QString& name, const QColor& color, int size)
{
	// the designer's icons of the official build (official.qrc, art/icons) first, else the public ones
	const QString art = QString(":/art/icons/%1.svg").arg(name);
	QPixmap pm = QIcon(QFile::exists(art) ? art : QString(":/icons/%1.svg").arg(name)).pixmap(QSize(size, size), 2.0);
	QPainter p(&pm);
	p.setCompositionMode(QPainter::CompositionMode_SourceIn);
	p.fillRect(pm.rect(), color);
	return pm;
}

// nav icon: grey, white when hovered / selected (the QSS colours of QPushButton#nav)
QIcon navIcon(const QString& name)
{
	QIcon icon;
	const QPixmap grey = tinted(name, QColor(255, 255, 255, 153), 16), white = tinted(name, Qt::white, 16);
	icon.addPixmap(grey, QIcon::Normal, QIcon::Off);
	icon.addPixmap(white, QIcon::Active, QIcon::Off);
	icon.addPixmap(white, QIcon::Normal, QIcon::On);
	icon.addPixmap(white, QIcon::Active, QIcon::On);
	icon.addPixmap(tinted(name, QColor(255, 255, 255, 64), 16), QIcon::Disabled, QIcon::Off);
	return icon;
}

// a 20 px icon in front of a button's label (Sketch screens: trophy on FIND RANKED MATCH, figure on PRACTICE & SEARCH ...)
void setButtonIcon(QPushButton* b, const QString& icon)
{
	if (icon.isEmpty())
	{
		b->setIcon(QIcon());
		return;
	}
	const bool dark = b->objectName() == "update";
	QIcon ic;
	ic.addPixmap(tinted(icon, dark ? QColor(0x0C, 0x0C, 0x14) : Qt::white, 20));
	ic.addPixmap(tinted(icon, QColor(255, 255, 255, 100), 20), QIcon::Disabled);
	b->setIcon(ic);
	b->setIconSize(QSize(20, 20));
}

QPushButton* button(const QString& t, const char* name, const QString& icon)
{
	auto* b = new QPushButton(t);
	b->setObjectName(name);
	b->setCursor(Qt::PointingHandCursor);
	const QByteArray n(name);
	// Sketch: every button label, nav item and the name in the top bar in capitals
	if (n == "cta" || n == "ghost" || n == "update" || n == "nav" || n == "tab" || n == "pill" || n == "join" || n == "ghostSmall")
	{
		QFont f = b->font();
		f.setCapitalization(QFont::AllUppercase);
		b->setFont(f);
	}
	if (n == "update")
	{
		// Btn Update: gold glow (shadow #EFBC23 60 %, blur 40, y 3)
		auto* glow = new QGraphicsDropShadowEffect(b);
		glow->setColor(QColor(0xEF, 0xBC, 0x23, 150));
		glow->setBlurRadius(40);
		glow->setOffset(0, 3);
		b->setGraphicsEffect(glow);
	}
	// Sketch Btn Default / Primary / Update: an arrow in front of the label unless the button has its own icon
	setButtonIcon(b, icon.isEmpty() && !t.isEmpty() && (n == "cta" || n == "ghost" || n == "update") ? QString("arrow_forward") : icon);
	return b;
}

// a field that takes a key / stick button by pressing it (a Default button without the arrow)
QPushButton* keyButton(const QString& t)
{
	auto* b = button(t, "ghost");
	setButtonIcon(b, QString());
	b->setMinimumWidth(120);
	return b;
}

// "card" = a page (lies on the background, no frame since the Sketch design), "inner" = a box (the design's Lobby frame)
// a dialog's Close button, bottom right
void addCloseButton(QDialog* d, QBoxLayout* v)
{
	auto* close = button(T("Close"), "ghost");
	QObject::connect(close, &QPushButton::clicked, d, &QDialog::close);
	v->addWidget(close, 0, Qt::AlignRight);
}

QFrame* card(const char* name)
{
	auto* f = new QFrame;
	f->setObjectName(name);
	return f;
}

// Sketch dropdown fields: the value in capitals, the hand cursor
void styleField(QComboBox* box)
{
	box->setCursor(Qt::PointingHandCursor);
	QFont f = box->font();
	f.setCapitalization(QFont::AllUppercase);
	box->setFont(f);
}

// Sketch "Dropdown": the label (bold 13 px, grey, capitals) over the field
QWidget* dropdown(const QString& label, QComboBox* box)
{
	auto* w = new QWidget;
	auto* v = new QVBoxLayout(w);
	v->setContentsMargins(0, 0, 0, 0);
	v->setSpacing(4);
	auto* l = new QLabel(label.toUpper());
	l->setObjectName("ddLabel");
	v->addWidget(l);
	styleField(box);
	v->addWidget(box);
	return w;
}

// Sketch "Dropdown Horizontal" as a settings row: the label left (bold 13 px, grey, capitals, one column for all rows),
// then the controls; a hint (hintText) takes the rest of the row
QHBoxLayout* settingRow(const QString& label, std::initializer_list<QWidget*> widgets)
{
	auto* row = new QHBoxLayout;
	row->setSpacing(12);
	auto* l = new QLabel(label.toUpper());
	l->setObjectName("ddLabel");
	l->setFixedWidth(190);
	row->addWidget(l);
	for (QWidget* w : widgets)
	{
		auto* hint = qobject_cast<QLabel*>(w);
		row->addWidget(w, hint && hint->wordWrap() ? 1 : 0);
	}
	row->addStretch(1);  // (the fields keep their own width, the rest of the row stays empty)
	return row;
}

// the grey running text next to a setting
QLabel* hintText(const QString& t)
{
	auto* l = text(t);
	l->setMinimumWidth(240);
	l->setMaximumWidth(620);
	return l;
}

// Sketch "Dropdown Button Field" with these (label, value) items, on the current value
QComboBox* choiceBox(const QList<std::pair<QString, QVariant>>& items, const QVariant& current, int minWidth)
{
	auto* box = new QComboBox;
	for (const auto& [label, value] : items)
		box->addItem(label, value);
	box->setCurrentIndex(qMax(0, box->findData(current)));
	styleField(box);
	if (minWidth)
		box->setMinimumWidth(minWidth);
	return box;
}

// Sketch "Toggle" (the QCheckBox indicator of the style sheet), without a label of its own
QCheckBox* toggle(bool on)
{
	auto* t = new QCheckBox;
	t->setChecked(on);
	t->setCursor(Qt::PointingHandCursor);
	return t;
}

// every character in one table, alphabetical, 7 columns - no scrolling (profile, user 2026-10-18: "velka tabulka"); the
// buttons carry their kChars index in the property "char"
QWidget* charGrid(int current, const std::function<void(int)>& pick)
{
	auto* w = new QWidget;
	auto* g = new QGridLayout(w);
	g->setContentsMargins(0, 0, 0, 0);
	g->setSpacing(8);
	int n = 0;
	for (const int i : charsByName())
	{
		auto* b = button(kChars[i], "pick");
		b->setCheckable(true);
		b->setAutoExclusive(true);
		b->setChecked(i == current);
		b->setProperty("char", i);
		b->setMinimumWidth(130);
		QObject::connect(b, &QPushButton::clicked, w, [pick, i] { pick(i); });
		g->addWidget(b, n / 7, n % 7);
		n++;
	}
	return w;
}

CharCombo::CharCombo(bool showRank) : m_showRank(showRank) {}

void CharCombo::setRank(const QPixmap& badge, int elo)
{
	m_badge = badge;
	m_elo = elo;
	update();
}

void CharCombo::paintEvent(QPaintEvent*)
{
	QStylePainter p(this);
	QStyleOptionComboBox opt;
	initStyleOption(&opt);
	opt.currentText.clear();
	p.drawComplexControl(QStyle::CC_ComboBox, opt);
	QRect r = rect().adjusted(14, 0, -34, 0);
	if (m_showRank && !m_badge.isNull())
	{
		const QSize bs = m_badge.size().scaled(58, 28, Qt::KeepAspectRatio);
		p.setRenderHint(QPainter::SmoothPixmapTransform);
		p.drawPixmap(QRect(QPoint(r.left(), (height() - bs.height()) / 2), bs), m_badge);
		r.setLeft(r.left() + bs.width() + 8);
		QFont ef = font();
		ef.setPixelSize(14);
		ef.setWeight(QFont::Medium);
		p.setFont(ef);
		p.setPen(QColor(0xEF, 0xBC, 0x23));
		const QString e = QString::number(m_elo);
		p.drawText(r, Qt::AlignVCenter | Qt::AlignLeft, e);
		r.setLeft(r.left() + QFontMetrics(ef).horizontalAdvance(e) + 8);
	}
	QFont nf = font();
	nf.setPixelSize(17);
	nf.setWeight(QFont::Medium);
	p.setFont(nf);
	p.setPen(Qt::white);
	p.drawText(r, Qt::AlignVCenter | Qt::AlignLeft, QFontMetrics(nf).elidedText(currentText(), Qt::ElideRight, r.width()));
}

BgWidget::BgWidget() { setAttribute(Qt::WA_StyledBackground, false); }

void BgWidget::paintEvent(QPaintEvent*)
{
	static const QPixmap bg(":/art/bg.jpg");
	QPainter p(this);
	p.fillRect(rect(), Qt::black);
	if (bg.isNull())
		return;
	const QSize s = bg.size().scaled(size(), Qt::KeepAspectRatioByExpanding);
	p.setRenderHint(QPainter::SmoothPixmapTransform);
	p.drawPixmap(QRect(QPoint((width() - s.width()) / 2, (height() - s.height()) / 2), s), bg);
}

QLabel* sticker(const QString& t, const char* kind)
{
	auto* l = new QLabel(t.toUpper());
	l->setObjectName(kind);
	l->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
	return l;
}

// Rank icon (launcher/ranks/NN.png, cut from the game's rank graphics; NN = the server's rank index, 0 = Beginner)
QPixmap rankIcon(int rank, int height)
{
	static QHash<int, QPixmap> cache;
	const int key = rank * 1000 + height;
	if (!cache.contains(key))
	{
		const QPixmap src(QString(":/ranks/%1.png").arg(rank, 2, 10, QChar('0')));
		cache[key] = src.isNull() ? src : src.scaledToHeight(height, Qt::SmoothTransformation);
	}
	return cache[key];
}

VsPanel::VsPanel() { setMinimumHeight(230); }

// sidesHidden: a found match not accepted by both yet - the left card is this player, no PLAYER 1 / 2 and no side colours
void VsPanel::set(const QJsonObject& p1, const QJsonObject& p2, const QString& top, bool sidesHidden)
{
	m_p1 = p1;
	m_p2 = p2;
	m_top = top;
	m_sidesHidden = sidesHidden;
	update();
}

void VsPanel::paintEvent(QPaintEvent*)
{
	QPainter p(this);
	p.setRenderHint(QPainter::Antialiasing);
	const qreal w = width(), h = height(), gap = 110, cw = (w - gap) / 2;
	const auto side = [&](QRectF r, bool left, const QJsonObject& pl) {
		QPainterPath path;
		path.addRoundedRect(r, 16, 16);
		QLinearGradient g(left ? r.topLeft() : r.topRight(), left ? r.bottomRight() : r.bottomLeft());
		const QColor c = m_sidesHidden ? QColor(0x5A, 0x5A, 0x6E) : left ? kRed : kBlue;
		g.setColorAt(0, QColor(c.red(), c.green(), c.blue(), pl.isEmpty() ? 40 : 120));
		g.setColorAt(0.7, QColor(0x14, 0x14, 0x20));
		g.setColorAt(1, kBg);
		p.fillPath(path, g);
		// ready (Ready / Rematch / Next game / accepted): orange frame + READY at the top in the middle (user 2026-10-14)
		const bool ready = pl["ready"].toBool();
		p.setPen(ready ? QPen(kOrange, 3) : QPen(QColor(232, 232, 242, 26), 1));
		p.drawPath(path);
		if (ready)
		{
			QFont rf(g_body);
			rf.setPixelSize(13);
			rf.setWeight(QFont::Bold);
			rf.setLetterSpacing(QFont::AbsoluteSpacing, 1.5);
			const QString rt = T("READY");
			const qreal rw = QFontMetrics(rf).horizontalAdvance(rt) + 22;
			const QRectF rr(r.center().x() - rw / 2, r.top() + 14, rw, 22);
			QPainterPath rp;
			rp.addRoundedRect(rr, 8, 8);
			p.fillPath(rp, kOrange);
			p.setFont(rf);
			p.setPen(kBg);
			p.drawText(rr, Qt::AlignCenter, rt);
		}
		// sticker
		QFont sf(g_body);
		sf.setPixelSize(12);
		sf.setWeight(QFont::DemiBold);
		p.setFont(sf);
		const QString st = m_sidesHidden ? (left ? T("YOU") : T("OPPONENT")) : left ? "PLAYER 1" : "PLAYER 2";
		const qreal sw = QFontMetrics(sf).horizontalAdvance(st) + 16;
		QRectF sr(left ? r.left() + 14 : r.right() - 14 - sw, r.top() + 14, sw, 22);
		QPainterPath sp;
		sp.addRoundedRect(sr, 8, 8);
		p.fillPath(sp, c);
		p.setPen(kText);
		p.drawText(sr, Qt::AlignCenter, st);
		const Qt::Alignment al = left ? Qt::AlignLeft : Qt::AlignRight;
		const QRectF body = r.adjusted(20, 46, -20, -16);
		if (pl.isEmpty() || pl["hidden"].toBool())
		{
			QFont f(g_body);
			f.setPixelSize(18);
			f.setWeight(QFont::Medium);
			p.setFont(f);
			p.setPen(kMuted);
			p.drawText(body, al | Qt::AlignVCenter, pl.isEmpty() ? T("Waiting for an opponent…") : T("Opponent found"));
			return;
		}
		QFont d(g_display);
		d.setWeight(QFont::Bold);
		const QString cn = charName(pl["char"].toInt());
		d.setPixelSize(qMin(54, int(body.width() / qMax(4, int(cn.size())) * 1.7)));
		p.setFont(d);
		p.setPen(Qt::white);
		p.drawText(QRectF(body.left(), body.top(), body.width(), 62), al | Qt::AlignVCenter, cn);
		QFont n(g_body);
		n.setPixelSize(18);
		n.setWeight(QFont::DemiBold);
		p.setFont(n);
		p.drawText(QRectF(body.left(), body.top() + 66, body.width(), 26), al | Qt::AlignVCenter, pl["name"].toString());
		QFont e(g_body);
		e.setPixelSize(14);
		e.setWeight(QFont::DemiBold);
		p.setFont(e);
		p.setPen(kGold);
		const QPixmap icon = rankIcon(pl["rank"].toInt(-1), 40);
		const qreal iw = icon.isNull() ? 0 : icon.width() + 14;
		if (!icon.isNull())
			p.drawPixmap(QPointF(left ? body.left() : body.right() - icon.width(), body.top() + 96), icon);
		p.drawText(QRectF(body.left() + (left ? iw : 0), body.top() + 96, body.width() - iw, 40), al | Qt::AlignVCenter,
		           QString("MMR %1").arg(pl["elo"].toInt(500)));
	};
	const qreal top = m_top.isEmpty() ? 0 : 36;
	side(QRectF(0, top, cw, h - top), true, m_p1);
	side(QRectF(w - cw, top, cw, h - top), false, m_p2);
	QFont vs(g_display);
	vs.setPixelSize(60);
	vs.setWeight(QFont::Bold);
	p.setFont(vs);
	p.setPen(kGold);
	p.drawText(QRectF(cw, top, gap, h - top), Qt::AlignCenter, "VS");
	if (!m_top.isEmpty())
	{
		QFont f(g_body);
		f.setPixelSize(15);
		f.setWeight(QFont::DemiBold);
		f.setCapitalization(QFont::AllUppercase);
		p.setFont(f);
		p.setPen(QColor(232, 232, 242, 160));
		p.drawText(QRectF(0, 0, w, 26), Qt::AlignCenter, m_top);
	}
}

void clearLayout(QLayout* l)
{
	while (auto* it = l->takeAt(0))
	{
		if (auto* w = it->widget())
			w->deleteLater();
		if (auto* sub = it->layout())
			clearLayout(sub);
		delete it;
	}
}

ClickFilter::ClickFilter(QObject* parent, std::function<void()> fn) : QObject(parent), m_fn(std::move(fn)) {}

bool ClickFilter::eventFilter(QObject*, QEvent* e)
{
	if (e->type() == QEvent::MouseButtonRelease && static_cast<QMouseEvent*>(e)->button() == Qt::LeftButton && m_fn)
		m_fn();
	return false;
}
