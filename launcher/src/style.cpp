// Tekken 6 Native Online launcher - The look: colours, fonts, the style sheets.

#include "style.h"

// ---- look: the designer's Sketch "TK6ONLINE" (2026-10-18): red light on black (art/bg.jpg), dark glass boxes with a
// 2 px white 25 % outline and 8 px corners, red Primary / glass Default / gold Update buttons, nav with a white underline

const char* kStyleTemplate = R"(
/* Sketch "TK6ONLINE" (designer, 2026-10-18; values from its symbols). Eurostile of the design -> Bai Jamjuree (OFL). */
* { font-family: "%BODY%"; color: #FFFFFF; font-size: 15px; }
QMainWindow { background: #000000; }
QMessageBox, QDialog, QInputDialog { background: #0B0B0C; }
QToolTip { background: #0B0B0C; color: #FFFFFF; border: 1px solid rgba(255,255,255,0.25); padding: 4px; }
QMenu { background: #0B0B0C; border: 1px solid rgba(255,255,255,0.25); }
QMenu::item:selected { background: #BF1802; }
QMessageBox QPushButton, QDialog QPushButton { background: rgba(0,0,0,0.67); border: 2px solid rgba(255,255,255,0.25); border-radius: 8px;
  padding: 7px 18px; min-width: 70px; font-weight: 500; }
QMessageBox QPushButton:hover, QDialog QPushButton:hover { border-color: rgba(255,255,255,0.5); }
/* text */
QLabel#title { font-size: 26px; font-weight: 700; }
QLabel#sub { color: #ACACAC; font-size: 17px; letter-spacing: 0.6px; }
QLabel#body { font-size: 17px; letter-spacing: 0.6px; }
QLabel#status { color: #EFBC23; font-size: 17px; font-weight: 600; }
QLabel#ddLabel { color: #ACACAC; font-size: 13px; font-weight: 700; letter-spacing: 0.4px; }
QLabel#muted { color: #ACACAC; font-size: 15px; }
QLabel#small { color: #7A7A80; font-size: 12px; }
QLabel#err { color: #FF5A47; font-size: 15px; font-weight: 600; }
QLabel#h1 { font-size: 26px; font-weight: 700; }
QLabel#h3 { font-size: 19px; font-weight: 700; }
QLabel#gold { color: #EFBC23; font-weight: 600; }
QLabel#code { font-size: 44px; font-weight: 700; color: #EFBC23; letter-spacing: 8px; }
QLabel#sticker { background: #BF1802; border-radius: 4px; padding: 3px 8px; font-size: 13px; font-weight: 700; }
QLabel#stickerBlue { background: #1675E5; border-radius: 4px; padding: 3px 8px; font-size: 13px; font-weight: 700; }
QLabel#stickerGold { background: #EFBC23; color: #0C0C14; border-radius: 4px; padding: 3px 8px; font-size: 13px; font-weight: 700; }
QLabel#chip { border: 2px solid rgba(255,255,255,0.25); border-radius: 8px; padding: 5px 12px; background: rgba(0,0,0,0.67);
  font-size: 14px; font-weight: 500; }
/* pages lie on the background; boxes = the "Lobby" frame: black 67 % + white 5 % gloss, 2 px white 25 %, 8 px corners */
QFrame#card { background: transparent; border: none; }
QFrame#inner { background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 rgba(13,13,13,0.70), stop:1 rgba(0,0,0,0.67));
  border: 2px solid rgba(255,255,255,0.25); border-radius: 8px; }
QFrame#side { background: transparent; border: none; border-left: 1px solid rgba(255,255,255,0.25); }
/* lobby row parts */
QLabel#lobbyName { font-size: 17px; font-weight: 700; letter-spacing: 0.5px; }
QLabel#lobbyChar { font-size: 17px; letter-spacing: 0.6px; }
QLabel#region { color: #1BE616; border: 1px solid #0A9506; border-radius: 4px; padding: 1px 5px; font-size: 14px; font-weight: 500; }
QLabel#regionFar { color: #E8321C; border: 1px solid #BF1802; border-radius: 4px; padding: 1px 5px; font-size: 14px; font-weight: 500; }
QLabel#full { color: #E8321C; border: 1px solid #BF1802; border-radius: 4px; padding: 1px 5px; font-size: 14px; font-weight: 700; }
QPushButton#join { background: #0A9506; border: none; border-radius: 4px; padding: 2px 6px; font-size: 14px; font-weight: 700; letter-spacing: 0.4px; }
QPushButton#join:hover { background: #0DB008; }
QPushButton#join:disabled { background: rgba(10,149,6,0.35); color: rgba(255,255,255,0.5); }
/* top bar nav ("Selected" / "Unselected"): 16 px icon, bold 16 px, 60 % when not selected, 2 px white underline */
QPushButton#nav { background: transparent; border: none; border-bottom: 2px solid transparent; border-radius: 0; padding: 9px 0 9px 0;
  margin: 0 14px; font-size: 16px; font-weight: 700; letter-spacing: 0.7px; color: rgba(255,255,255,0.6); text-align: left; }
QPushButton#nav:hover { color: #FFFFFF; }
QPushButton#nav:checked { color: #FFFFFF; border-bottom-color: #FFFFFF; }
QPushButton#nav:disabled { color: rgba(255,255,255,0.25); }
QPushButton#tab { background: transparent; border: none; border-bottom: 2px solid transparent; border-radius: 0; padding: 6px 0;
  margin: 0 18px 0 0; font-size: 15px; font-weight: 700; letter-spacing: 0.6px; color: rgba(255,255,255,0.6); }
QPushButton#tab:hover { color: #FFFFFF; }
QPushButton#tab:checked { color: #FFFFFF; border-bottom-color: #FFFFFF; }
/* the player in the top bar: rank badge + name */
QPushButton#pill { background: transparent; border: none; padding: 0; font-size: 17px; font-weight: 700; letter-spacing: 0.5px; }
QPushButton#pill:hover { color: #EFBC23; }
QLabel#pillText { font-size: 17px; font-weight: 700; letter-spacing: 0.5px; }
/* Btn Primary: #BF1802 90 % + white 12 % gloss + black 10 % shade, 1 px black 67 %; Hover: shade 20 %; Click: shade 15-25 % */
QPushButton#cta { background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 rgba(199,51,32,0.92), stop:1 rgba(172,22,2,0.92));
  border: 1px solid rgba(0,0,0,0.67); border-radius: 8px; min-height: 50px; padding: 0 22px 0 16px; font-size: 16px; font-weight: 500;
  letter-spacing: 0.4px; }
QPushButton#cta:hover { background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #BF1802, stop:1 #991302); }
QPushButton#cta:pressed { background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #A21402, stop:1 #8F1202); }
QPushButton#cta:disabled { background: rgba(60,60,64,0.7); color: rgba(255,255,255,0.4); }
/* Btn Default: black 67 % + white 5 % gloss, 2 px white 25 %; Hover: black 33 %, white 33 %; Click: white 50 % */
QPushButton#ghost { background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 rgba(13,13,13,0.70), stop:1 rgba(0,0,0,0.67));
  border: 2px solid rgba(255,255,255,0.25); border-radius: 8px; min-height: 48px; padding: 0 22px 0 16px; font-size: 16px; font-weight: 500;
  letter-spacing: 0.4px; }
QPushButton#ghost:hover { background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 rgba(13,13,13,0.38), stop:1 rgba(0,0,0,0.33));
  border-color: rgba(255,255,255,0.33); }
QPushButton#ghost:pressed { border-color: rgba(255,255,255,0.5); }
QPushButton#ghost:disabled { color: rgba(255,255,255,0.35); border-color: rgba(255,255,255,0.12); }
/* small Default button (inside settings rows) */
QPushButton#ghostSmall { background: rgba(0,0,0,0.67); border: 2px solid rgba(255,255,255,0.25); border-radius: 8px; padding: 7px 14px;
  font-size: 14px; font-weight: 500; }
QPushButton#ghostSmall:hover { border-color: rgba(255,255,255,0.5); }
QPushButton#ghostSmall:disabled { color: rgba(255,255,255,0.35); border-color: rgba(255,255,255,0.12); }
/* Btn Update: #EFBC23 + white 33 % gloss, 1 px black 20 %, gold glow (button()); Hover: flat, black 40 % */
QPushButton#update { background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #F5D57B, stop:1 #EFBC23); color: #0C0C14;
  border: 1px solid rgba(0,0,0,0.2); border-radius: 8px; min-height: 50px; padding: 0 22px 0 16px; font-size: 16px; font-weight: 700;
  letter-spacing: 0.4px; }
QPushButton#update:hover { background: #EFBC23; border-color: rgba(0,0,0,0.4); }
QPushButton#update:disabled { background: #6b5a20; }
/* icon only Default button (Refresh) */
QPushButton#iconBtn { background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 rgba(13,13,13,0.70), stop:1 rgba(0,0,0,0.67));
  border: 2px solid rgba(255,255,255,0.25); border-radius: 8px; min-width: 48px; max-width: 48px; min-height: 40px; max-height: 40px; }
QPushButton#iconBtn:hover { border-color: rgba(255,255,255,0.33); }
QPushButton#iconBtn:pressed { border-color: rgba(255,255,255,0.5); }
QPushButton#pick { background: rgba(0,0,0,0.67); border: 2px solid rgba(255,255,255,0.25); border-radius: 8px; padding: 9px 6px;
  font-size: 14px; font-weight: 500; }
QPushButton#pick:hover { border-color: rgba(255,255,255,0.5); }
QPushButton#pick:checked { background: #BF1802; border-color: rgba(0,0,0,0.67); }
QPushButton#pick:disabled { color: rgba(255,255,255,0.35); }
/* Input: black 30 %, 2 px white 25 %; Hover: white 50 %; Click (focus): 2 px #BF1802 */
QLineEdit { background: rgba(0,0,0,0.3); border: 2px solid rgba(255,255,255,0.25); border-radius: 8px; padding: 0 14px; min-height: 48px;
  font-size: 17px; font-weight: 500; letter-spacing: 0.5px; }
QLineEdit:hover { border-color: rgba(255,255,255,0.5); }
QLineEdit:focus { border-color: #BF1802; background: rgba(40,4,0,0.35); }
QLineEdit#codeEdit { font-size: 26px; font-weight: 700; letter-spacing: 6px; }
/* Dropdown Button Field: like Btn Default, 44 px, chevron 20 px */
QComboBox { background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 rgba(13,13,13,0.70), stop:1 rgba(0,0,0,0.67));
  border: 2px solid rgba(255,255,255,0.25); border-radius: 8px; min-height: 40px; padding: 0 10px 0 14px; font-size: 16px; font-weight: 500;
  letter-spacing: 0.4px; }
QComboBox:hover { background: rgba(0,0,0,0.33); border-color: rgba(255,255,255,0.33); }
QComboBox:on { border-color: rgba(255,255,255,0.5); }
QComboBox::drop-down { border: none; width: 30px; }
QComboBox::down-arrow { image: url(:/icons/chevron.svg); width: 20px; height: 20px; }
QComboBox QAbstractItemView { background: #0B0B0C; color: #FFFFFF; border: 2px solid rgba(255,255,255,0.25); outline: 0; padding: 4px;
  selection-background-color: #BF1802; selection-color: #FFFFFF; }
/* Toggle: 40 x 24 (icons/toggle_*.svg), label medium 14 px */
QCheckBox { color: #FFFFFF; spacing: 8px; font-size: 14px; font-weight: 500; letter-spacing: 0.3px; }
QCheckBox:disabled { color: rgba(255,255,255,0.4); }
QCheckBox::indicator { width: 40px; height: 24px; image: url(:/icons/toggle_off.svg); }
QCheckBox::indicator:checked { image: url(:/icons/toggle_on.svg); }
QScrollArea { border: none; background: transparent; }
QScrollArea > QWidget > QWidget { background: transparent; }
QStackedWidget, QStackedWidget > QWidget { background: transparent; }
QTextBrowser { background: rgba(0,0,0,0.67); border: 2px solid rgba(255,255,255,0.25); border-radius: 8px; }
QScrollBar:vertical { background: transparent; width: 8px; }
QScrollBar::handle:vertical { background: rgba(255,255,255,0.25); border-radius: 4px; min-height: 30px; }
QScrollBar::add-line, QScrollBar::sub-line { height: 0; width: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }
)";

// The boxes of the official build drawn from the designer's Sketch symbols (official.qrc art/ui, exported at 2x without
// the label): the double border, the radius and the gloss stay sharp where QSS borders come out jagged. 20 px of the
// 2x image = the 10 px border; the paddings / heights above are made smaller by those 8 px so the sizes stay the same.
const char* kArtStyle = R"(
QPushButton#ghost, QPushButton#iconBtn, QPushButton#pick, QPushButton#ghostSmall { background: transparent; border-radius: 0;
  border-width: 10px; border-image: url(:/art/ui/btn_default.png) 20 20 20 20 stretch stretch; }
QPushButton#ghost:hover, QPushButton#iconBtn:hover, QPushButton#pick:hover, QPushButton#ghostSmall:hover {
  border-image: url(:/art/ui/btn_default_hover.png) 20 20 20 20 stretch stretch; }
QPushButton#ghost:pressed, QPushButton#iconBtn:pressed, QPushButton#pick:pressed, QPushButton#ghostSmall:pressed {
  border-image: url(:/art/ui/btn_default_click.png) 20 20 20 20 stretch stretch; }
QPushButton#ghost { min-height: 32px; padding: 0 14px 0 8px; }
QPushButton#ghostSmall { padding: 0 6px; }
QPushButton#iconBtn { min-width: 32px; max-width: 32px; min-height: 24px; max-height: 24px; }
QPushButton#pick { padding: 1px 0; }
QPushButton#cta, QPushButton#pick:checked { background: transparent; border-radius: 0; border-width: 10px;
  border-image: url(:/art/ui/btn_primary.png) 20 20 20 20 stretch stretch; }
QPushButton#cta { min-height: 32px; padding: 0 14px 0 8px; }
QPushButton#cta:hover { border-image: url(:/art/ui/btn_primary_hover.png) 20 20 20 20 stretch stretch; }
QPushButton#cta:pressed { border-image: url(:/art/ui/btn_primary_click.png) 20 20 20 20 stretch stretch; }
QPushButton#cta:disabled { border-image: url(:/art/ui/btn_default.png) 20 20 20 20 stretch stretch; }
QPushButton#update { background: transparent; border-radius: 0; border-width: 10px; min-height: 32px; padding: 0 14px 0 8px;
  border-image: url(:/art/ui/btn_update.png) 20 20 20 20 stretch stretch; }
QPushButton#update:hover { border-image: url(:/art/ui/btn_update_hover.png) 20 20 20 20 stretch stretch; }
QPushButton#update:pressed { border-image: url(:/art/ui/btn_update_click.png) 20 20 20 20 stretch stretch; }
QLineEdit { background: transparent; border-radius: 0; border-width: 10px; min-height: 32px; padding: 0 6px;
  border-image: url(:/art/ui/input.png) 20 20 20 20 stretch stretch; }
QLineEdit:hover { border-image: url(:/art/ui/input_hover.png) 20 20 20 20 stretch stretch; }
QLineEdit:focus { background: transparent; border-image: url(:/art/ui/input_focus.png) 20 20 20 20 stretch stretch; }
QComboBox { background: transparent; border-radius: 0; border-width: 10px; min-height: 24px; padding: 0 2px 0 6px;
  border-image: url(:/art/ui/field.png) 20 20 20 20 stretch stretch; }
QComboBox:hover { background: transparent; border-image: url(:/art/ui/field_hover.png) 20 20 20 20 stretch stretch; }
QComboBox:on { border-image: url(:/art/ui/field_click.png) 20 20 20 20 stretch stretch; }
QCheckBox::indicator { image: url(:/art/ui/toggle_off.png); }
QCheckBox::indicator:checked { image: url(:/art/ui/toggle_on.png); }
)";
