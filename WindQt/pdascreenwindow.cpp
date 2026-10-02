#include "pdascreenwindow.h"
#include <QKeyEvent>
#include <QMouseEvent>
#include <QTimer>
#include <QContextMenuEvent>
#include <QFileDialog>
#include <QMenu>
#include <QMessageBox>
#include <QOpenGLContext>
#include <QOpenGLWidget>
#ifdef Q_OS_LINUX
#include <linux/input-event-codes.h>
#endif

static const char *PanelLabelNormalStyle = "border: 1px solid palette(mid); background: palette(button);";
static const char *PanelLabelPressedStyle = "border: 1px solid palette(mid); background: palette(highlight); color: palette(highlighted-text);";

QLabel *PDAScreenWindow::addPanelButton(const QString &text, const QRect &shown, const QRect &onDigitiser) {
	const int gap = 2;
	QLabel *label = new QLabel(text, this);
	label->setAlignment(Qt::AlignCenter);
	label->setGeometry(shown.adjusted(gap, gap, -gap, -gap));
	label->setStyleSheet(PanelLabelNormalStyle);
	panelButtons.append({shown, label, onDigitiser.center(), {}});
	return label;
}

QLabel *PDAScreenWindow::addPanelLabel(const QString &text, int x, int y, int w, int h) {
	QRect rect(x, y, w, h);
	return addPanelButton(text, rect, rect);
}

void PDAScreenWindow::setPanelLabelPressed(QLabel *label, bool pressed) {
	if (label)
		label->setStyleSheet(pressed ? PanelLabelPressedStyle : PanelLabelNormalStyle);
}

QLabel *PDAScreenWindow::panelLabelAt(const QPoint &pos) const {
	for (const auto &button : panelButtons)
		if (button.cellRect.contains(pos))
			return button.label;
	return nullptr;
}

PDAScreenWindow::PDAScreenWindow(EmuBase *emu, bool compactLayout, QWidget *parent) :
	QWidget(parent),
	emu(emu),
	lcd(new QLabel(this))
{
	setWindowTitle("WindEmu");
	const char *who = emu->getDeviceName();
	compact = compactLayout && strcmp(who, "Series 5mx") == 0;

	int lcdW = emu->getLCDWidth(), lcdH = emu->getLCDHeight();
	if (compact) {
		const int barH = 40;
		setFixedSize(lcdW, barH + lcdH + barH);
		lcdRect = QRect(0, barH, lcdW, lcdH);
	} else {
		setFixedSize(emu->getDigitiserWidth(), emu->getDigitiserHeight());
		lcdRect = QRect(emu->getLCDOffsetX(), emu->getLCDOffsetY(), lcdW, lcdH);
	}
	lcd->setGeometry(lcdRect);

	if (strcmp(who, "Osaris") == 0) {
		// some cheap and cheerful placeholders
		int bitW = (emu->getDigitiserWidth() - emu->getLCDWidth()) / 2;
		int bitH = emu->getDigitiserHeight() / 5;
		int leftX = 0;
		int rightX = bitW + emu->getLCDWidth();
		addPanelLabel("Word",       leftX, bitH * 0, bitW, bitH);
		addPanelLabel("Sheet",      leftX, bitH * 1, bitW, bitH);
		addPanelLabel("Data",       leftX, bitH * 2, bitW, bitH);
		addPanelLabel("Agenda",     leftX, bitH * 3, bitW, bitH);
		addPanelLabel("Extras",     leftX, bitH * 4, bitW, bitH);
		addPanelLabel("EPOC",       rightX, bitH * 0, bitW, bitH);
		addPanelLabel("Menu",       rightX, bitH * 1, bitW, bitH);
		addPanelLabel("Copy/Paste", rightX, bitH * 2, bitW, bitH);
		addPanelLabel("Zoom In",    rightX, bitH * 3, bitW, bitH);
		addPanelLabel("Zoom Out",   rightX, bitH * 4, bitW, bitH);
	} else if (strcmp(who, "Series 5mx") == 0) {
		// the silkscreen: a column of five left of the LCD, and the program bar below it
		int leftW = emu->getLCDOffsetX();
		int leftH = emu->getLCDHeight() / 5;
		int barX = 50;
		int barY = leftH * 5;
		int barW = (emu->getDigitiserWidth() - barX) / 8;
		int barH = emu->getDigitiserHeight() - emu->getLCDHeight();
		static const char *const leftIcons[] = {"➡️", "📄", "📡", "+", "-"};
		static const char *const leftNames[] = {"Menu", "Clipboard", "Infrared", "Zoom in", "Zoom out"};
		static const char *const programs[] = {"System", "Word", "Sheet", "Contacts", "Agenda", "Email", "Calc", "Jotter", "Extras"};
		// the compact layout has room for just a symbol per button
		static const char *const leftSymbols[] = {"☰", "📋", "📡", "🔍➕", "🔍➖"};
		static const char *const programSymbols[] = {"🏠", "📝", "📊", "📇", "📅", "📧", "🧮", "🗒️", "🧩"};
		auto leftCell = [&](int i) { return QRect(0, leftH * i, leftW, leftH); };
		auto barCell = [&](int i) { return i == 0 ? QRect(0, barY, barX, barH) : QRect(barX + barW * (i - 1), barY, barW, barH); };

		if (compact) {
			// the column goes above the LCD, with the card button, and the program bar below it
			auto addSymbolButton = [&](const char *symbol, const char *name, const QRect &shown, const QRect &onDigitiser) {
				QLabel *label = addPanelButton(QString::fromUtf8(symbol), shown, onDigitiser);
				label->setToolTip(name);
				QFont font = label->font();
				font.setPointSizeF(font.pointSizeF() * 1.6);
				label->setFont(font);
			};
			auto topCell = [&](int i) { return QRect(lcdW * i / 6, 0, lcdW * (i + 1) / 6 - lcdW * i / 6, lcdRect.top()); };
			for (int i = 0; i < 5; i++)
				addSymbolButton(leftSymbols[i], leftNames[i], topCell(i), leftCell(i));
			addSymbolButton("", "CF card", topCell(5), QRect());
			cardButton = panelButtons.last().label;
			panelButtons.last().action = [this] { toggleCard(); };
			updateCardButton();
			int bottomY = lcdRect.bottom() + 1;
			for (int i = 0; i < 9; i++)
				addSymbolButton(programSymbols[i], programs[i], QRect(lcdW * i / 9, bottomY, lcdW * (i + 1) / 9 - lcdW * i / 9, height() - bottomY), barCell(i));
		} else {
			for (int i = 0; i < 5; i++)
				addPanelButton(leftIcons[i], leftCell(i), leftCell(i));
			for (int i = 0; i < 9; i++)
				addPanelButton(programs[i], barCell(i), barCell(i));
		}
	}
}

void PDAScreenWindow::updateScreen() {
	uint8_t *lines[1024];
	QImage img(emu->getLCDWidth(), emu->getLCDHeight(), QImage::Format_Indexed8);
	for (int y = 0; y < img.height(); y++)
		lines[y] = img.scanLine(y);
	emu->readLCDIntoBuffer(lines, false);

	// the grey levels, tinted the blue-green of the 5mx's backlight while it's on
	static QVector<QRgb> plain, lit;
	if (plain.isEmpty()) {
		for (int i = 0; i < 256; i++) {
			plain.append(qRgb(i, i, i));
			lit.append(qRgb(i * 0xC8 / 255, i * 0xF0 / 255, i * 0xEC / 255));
		}
	}
	img.setColorTable(emu->isBacklightOn() ? lit : plain);

	// skip unchanged frames, which are costly to redraw when scaled
	if (img == lastFrame)
		return;
	lastFrame = img;
	lcd->setPixmap(QPixmap::fromImage(std::move(img)));
}

ScaledScreenView::ScaledScreenView(QWidget *screen, bool useGpu)
{
	setWindowTitle(screen->windowTitle());
	setScene(&scene);
	proxy = scene.addWidget(screen);
	screen->setFocusPolicy(Qt::StrongFocus);
	scene.setFocusItem(proxy);
	setBackgroundBrush(Qt::black);
	setFrameShape(QFrame::NoFrame);
	setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	setRenderHint(QPainter::SmoothPixmapTransform);

	// scaling the whole screen up in software takes much of a phone's CPU from
	// the emulation, so let the GPU do it where there's OpenGL
	if (useGpu && QOpenGLContext().create()) {
		setViewport(new QOpenGLWidget);
		// a GL viewport redraws in full anyway
		setViewportUpdateMode(QGraphicsView::FullViewportUpdate);
	}
}

ScaledScreenView::~ScaledScreenView()
{
	// the screen widget belongs to its owner, not to the scene
	proxy->setWidget(nullptr);
}

void ScaledScreenView::resizeEvent(QResizeEvent *event)
{
	QGraphicsView::resizeEvent(event);
	fitInView(proxy, Qt::KeepAspectRatio);
}

#if defined(Q_OS_MAC)
static EpocKey resolveKey(int key, int vk) {
	// Although Cocoa/Carbon's virtual keycodes include
	// modifiers, Qt doesn't expose them through QKeyEvent...
	switch (key) {
	case Qt::Key_Control: return EStdKeyLeftFunc;
	case Qt::Key_Shift: return EStdKeyLeftShift;
	case Qt::Key_Alt: return EStdKeyMenu;
	case Qt::Key_Meta: return EStdKeyLeftCtrl;
	}

	// https://github.com/phracker/MacOSX-SDKs/blob/master/MacOSX10.6.sdk/System/Library/Frameworks/Carbon.framework/Versions/A/Frameworks/HIToolbox.framework/Versions/A/Headers/Events.h#L182
	switch (vk) {
	case 0x00: return (EpocKey)'A';
	case 0x01: return (EpocKey)'S';
	case 0x02: return (EpocKey)'D';
	case 0x03: return (EpocKey)'F';
	case 0x04: return (EpocKey)'H';
	case 0x05: return (EpocKey)'G';
	case 0x06: return (EpocKey)'Z';
	case 0x07: return (EpocKey)'X';
	case 0x08: return (EpocKey)'C';
	case 0x09: return (EpocKey)'V';
	case 0x0B: return (EpocKey)'B';
	case 0x0C: return (EpocKey)'Q';
	case 0x0D: return (EpocKey)'W';
	case 0x0E: return (EpocKey)'E';
	case 0x0F: return (EpocKey)'R';

	case 0x10: return (EpocKey)'Y';
	case 0x11: return (EpocKey)'T';
	case 0x12: return (EpocKey)'1';
	case 0x13: return (EpocKey)'2';
	case 0x14: return (EpocKey)'3';
	case 0x15: return (EpocKey)'4';
	case 0x16: return (EpocKey)'6';
	case 0x17: return (EpocKey)'5';
	case 0x19: return (EpocKey)'9';
	case 0x1A: return (EpocKey)'7';
	case 0x1C: return (EpocKey)'8';
	case 0x1D: return (EpocKey)'0';
	case 0x1F: return (EpocKey)'O';

	case 0x20: return (EpocKey)'U';
	case 0x22: return (EpocKey)'I';
	case 0x23: return (EpocKey)'P';
	case 0x24: return EStdKeyEnter;
	case 0x25: return (EpocKey)'L';
	case 0x26: return (EpocKey)'J';
	case 0x27: return EStdKeySingleQuote;
	case 0x28: return (EpocKey)'K';
	case 0x2B: return EStdKeyComma;
	case 0x2D: return (EpocKey)'N';
	case 0x2E: return (EpocKey)'M';
	case 0x2F: return EStdKeyFullStop;

	case 0x30: return EStdKeyTab;
	case 0x31: return EStdKeySpace;
	case 0x33: return EStdKeyBackspace;
	case 0x35: return EStdKeyEscape;

	case 0x7B: return EStdKeyLeftArrow;
	case 0x7C: return EStdKeyRightArrow;
	case 0x7D: return EStdKeyDownArrow;
	case 0x7E: return EStdKeyUpArrow;
	}

	return EStdKeyNull;
}

static KeyMapping mapKey(const QKeyEvent *event) {
	return {resolveKey(event->key(), event->nativeVirtualKey()), false};
}
#elif defined(Q_OS_LINUX)
// Map by physical key position, like the real keyboard matrix, so the host
// layout (e.g. Russian) doesn't matter. Punctuation keys type what they do on
// a PC with a US layout, using the Psion's Fn and Shift combinations for the
// symbols it has no key for; that also lets CyrLat-style Cyrillic layouts,
// which re-map symbol codes, follow the PC's ЙЦУКЕН.
static KeyMapping mapKey(const QKeyEvent *event) {
	static const char row1[] = "1234567890", row2[] = "QWERTYUIOP", row3[] = "ASDFGHJKL", row4[] = "ZXCVBNM";
	bool shift = event->modifiers() & Qt::ShiftModifier;
	auto fnSymbol = [](char key, bool dropShift = false) {
		KeyMapping k{(EpocKey)key, true};
		k.dropShift = dropShift;
		return k;
	};
	// Qt reports XKB keycodes, which are evdev keycodes + 8
	int code = (int)event->nativeScanCode() - 8;
	if (code >= KEY_1 && code <= KEY_0) return {(EpocKey)row1[code - KEY_1], false};
	if (code >= KEY_Q && code <= KEY_P) return {(EpocKey)row2[code - KEY_Q], false};
	if (code >= KEY_A && code <= KEY_L) return {(EpocKey)row3[code - KEY_A], false};
	if (code >= KEY_Z && code <= KEY_M) return {(EpocKey)row4[code - KEY_Z], false};

	switch (code) {
	// ` is where Esc sits on many compact keyboards; Ctrl+` switches the
	// Psion off (Fn+Esc) without Ctrl reaching it
	case KEY_GRAVE:
		if (event->modifiers() & Qt::ControlModifier)
			return {EStdKeyEscape, true, true};
		return {EStdKeyEscape, false};
	case KEY_ESC:        return {EStdKeyEscape, false};
	case KEY_BACKSPACE:  return {EStdKeyBackspace, false};
	case KEY_TAB:        return {EStdKeyTab, false};
	case KEY_ENTER:
	case KEY_KPENTER:    return {EStdKeyEnter, false};
	// Shift+2 is " on the Psion
	case KEY_APOSTROPHE: return shift ? KeyMapping{(EpocKey)'2'} : KeyMapping{EStdKeySingleQuote};
	case KEY_COMMA:      return shift ? fnSymbol('5', true) : KeyMapping{EStdKeyComma};      // <
	case KEY_DOT:        return shift ? fnSymbol('6', true) : KeyMapping{EStdKeyFullStop};   // >
	case KEY_SLASH: {
		// the Psion types / as Shift+comma, and ? as Shift+full stop
		if (shift)
			return {EStdKeyFullStop};
		KeyMapping k{EStdKeyComma};
		k.addShift = true;
		return k;
	}
	case KEY_SEMICOLON:  return fnSymbol('L');                                            // ; and :
	case KEY_LEFTBRACE:  return shift ? fnSymbol('9', true) : fnSymbol('7');              // { [
	case KEY_RIGHTBRACE: return shift ? fnSymbol('0', true) : fnSymbol('8');              // } ]
	case KEY_MINUS:      return shift ? fnSymbol('1', true) : fnSymbol('O');              // _ -
	case KEY_EQUAL:      return shift ? fnSymbol('I', true) : fnSymbol('P');              // + =
	case KEY_BACKSLASH:  return fnSymbol('3');                                            // backslash, Shift kept for CyrLat
	case KEY_SPACE:      return {EStdKeySpace, false};
	case KEY_LEFTSHIFT:  return {EStdKeyLeftShift, false};
	case KEY_LEFTCTRL:
	case KEY_RIGHTCTRL:  return {EStdKeyLeftCtrl, false};
	// either Alt, as small keyboards often have just the right one
	case KEY_LEFTALT:
	case KEY_RIGHTALT:   return {EStdKeyLeftFunc, false};
	// phone keyboards often lack all of these but Right Shift
	case KEY_RIGHTSHIFT:
	case KEY_COMPOSE:
	case KEY_F1:         return {EStdKeyMenu, false};
	case KEY_UP:         return {EStdKeyUpArrow, false};
	case KEY_DOWN:       return {EStdKeyDownArrow, false};
	case KEY_LEFT:       return {EStdKeyLeftArrow, false};
	case KEY_RIGHT:      return {EStdKeyRightArrow, false};
	// the Psion has no dedicated keys for these; they are Fn + arrows
	case KEY_HOME:       return {EStdKeyLeftArrow, true};
	case KEY_END:        return {EStdKeyRightArrow, true};
	case KEY_PAGEUP:     return {EStdKeyUpArrow, true};
	case KEY_PAGEDOWN:   return {EStdKeyDownArrow, true};
	}
	return {EStdKeyNull, false};
}
#else
static EpocKey resolveKey(int key) {
	// Placeholder, doesn't work for all keys
	switch (key) {
	case Qt::Key_Apostrophe: return EStdKeySingleQuote;
	case Qt::Key_Backspace: return EStdKeyBackspace;
	case Qt::Key_Escape: return EStdKeyEscape;
	case Qt::Key_Enter: return EStdKeyEnter;
	case Qt::Key_Return: return EStdKeyEnter;
	case Qt::Key_Alt: return EStdKeyMenu;
	case Qt::Key_Tab: return EStdKeyTab;
#ifdef Q_OS_MAC
	case Qt::Key_Meta: return EStdKeyLeftCtrl;
#else
	case Qt::Key_Control: return EStdKeyLeftCtrl;
#endif
	case Qt::Key_Down: return EStdKeyDownArrow;
	case Qt::Key_Period: return EStdKeyFullStop;
#ifdef Q_OS_MAC
	case Qt::Key_Control: return EStdKeyLeftFunc;
#else
	case Qt::Key_Meta: return EStdKeyLeftFunc;
#endif
	case Qt::Key_Shift: return EStdKeyLeftShift;
	case Qt::Key_Right: return EStdKeyRightArrow;
	case Qt::Key_Left: return EStdKeyLeftArrow;
	case Qt::Key_Comma: return EStdKeyComma;
	case Qt::Key_Up: return EStdKeyUpArrow;
	case Qt::Key_Space: return EStdKeySpace;
	}

	if (key >= '0' && key <= '9') return (EpocKey)key;
	if (key >= 'A' && key <= 'Z') return (EpocKey)key;
	return EStdKeyNull;
}

static KeyMapping mapKey(const QKeyEvent *event) {
	return {resolveKey(event->key()), false};
}
#endif


static const int ComboKeyDelayMs = 50;

static quint32 hostKeyId(const QKeyEvent *event) {
	return event->nativeScanCode() ? event->nativeScanCode() : (quint32)event->key();
}

void PDAScreenWindow::keyPressEvent(QKeyEvent *event)
{
	emu->log("KeyPress: QtKey=%d nativeScanCode=%u nativeVirtualKey=%x nativeModifiers=%x", event->key(), event->nativeScanCode(), event->nativeVirtualKey(), event->nativeModifiers());
	// EPOC does its own key repeat while a key is held in the matrix
	if (event->isAutoRepeat())
		return;
	KeyMapping k = mapKey(event);
	if (k.key == EStdKeyNull)
		return;
	heldKeys.insert(hostKeyId(event), k);
	if (k.dropCtrl)
		emu->setKeyboardKey(EStdKeyLeftCtrl, false);
	if (k.dropShift)
		setShiftKeys(false);
	if (k.addShift)
		emu->setKeyboardKey(EStdKeyLeftShift, true);
	if (k.withFn) {
		// EPOC must see Fn held before the key, as when a person presses them
		emu->setKeyboardKey(EStdKeyLeftFunc, true);
		QTimer::singleShot(ComboKeyDelayMs, this, [this, k] { emu->setKeyboardKey(k.key, true); });
	} else {
		emu->setKeyboardKey(k.key, true);
	}
}

void PDAScreenWindow::keyReleaseEvent(QKeyEvent *event)
{
	if (event->isAutoRepeat())
		return;
	auto it = heldKeys.find(hostKeyId(event));
	if (it == heldKeys.end())
		return;
	KeyMapping k = it.value();
	heldKeys.erase(it);
	if (k.withFn) {
		// released after the delayed press above, even for a quick tap
		QTimer::singleShot(2 * ComboKeyDelayMs, this, [this, k] {
			emu->setKeyboardKey(k.key, false);
			emu->setKeyboardKey(EStdKeyLeftFunc, false);
			if (k.dropShift)
				setShiftKeys(true);
		});
	} else {
		emu->setKeyboardKey(k.key, false);
		if (k.dropShift)
			setShiftKeys(true);
	}
	if (k.addShift)
		emu->setKeyboardKey(EStdKeyLeftShift, false);
}

// Releases the Shift keys the host is holding down, or presses them again
void PDAScreenWindow::setShiftKeys(bool pressed)
{
	for (const KeyMapping &held : heldKeys)
		if (held.key == EStdKeyLeftShift || held.key == EStdKeyRightShift)
			emu->setKeyboardKey(held.key, pressed);
}


void PDAScreenWindow::setCardPath(const QString &path)
{
	cardPath = path;
	updateCardButton();
}

void PDAScreenWindow::updateCardButton()
{
	if (!cardButton)
		return;
	bool inserted = emu->hasCFCard();
	cardButton->setText(QString::fromUtf8(inserted ? "⏏️" : "💾"));
	cardButton->setToolTip(inserted ? "Eject the CF card" : "Insert the CF card");
}

// Takes the CF card out, copying changes back to its folder, or puts it back in
void PDAScreenWindow::toggleCard()
{
	if (emu->hasCFCard()) {
		if (!emu->ejectCFCard())
			QMessageBox::warning(this, "WindEmu", "Not all changes on the CF card could be copied back to " + cardPath);
	} else {
		if (cardPath.isEmpty())
			cardPath = QFileDialog::getExistingDirectory(this, "Choose a folder to use as the CF card");
		if (!cardPath.isEmpty() && !emu->insertCFCard(QFile::encodeName(cardPath).constData()))
			QMessageBox::warning(this, "WindEmu", "Could not use " + cardPath + " as a CF card");
	}
	updateCardButton();
}

void PDAScreenWindow::contextMenuEvent(QContextMenuEvent *event)
{
	if (strcmp(emu->getDeviceName(), "Series 5mx") != 0)
		return;
	QMenu menu(this);
	menu.addAction(emu->hasCFCard() ? "Eject CF card" : "Insert CF card", this, [this] { toggleCard(); });
	menu.exec(event->globalPos());
}

// Where a touch at pos lands on the digitiser, for the area the touch began in
bool PDAScreenWindow::touchPoint(const QPoint &pos, QPoint &digitiserPos) const
{
	switch (touchArea) {
	case TouchAnywhere:
		digitiserPos = pos;
		return true;
	case TouchLcd: {
		// a drag that strays off the LCD stays on its edge
		int x = qBound(lcdRect.left(), pos.x(), lcdRect.right());
		int y = qBound(lcdRect.top(), pos.y(), lcdRect.bottom());
		digitiserPos = QPoint(x - lcdRect.left() + emu->getLCDOffsetX(), y - lcdRect.top() + emu->getLCDOffsetY());
		return true;
	}
	case TouchButton:
		digitiserPos = touchTarget;
		return true;
	default:
		return false;
	}
}

void PDAScreenWindow::mousePressEvent(QMouseEvent *event)
{
	if (!compact) {
		touchArea = TouchAnywhere;
	} else if (lcdRect.contains(event->pos())) {
		touchArea = TouchLcd;
	} else {
		touchArea = TouchNone;
		for (const auto &button : panelButtons) {
			if (button.cellRect.contains(event->pos())) {
				if (button.action) {
					button.action();
				} else {
					touchArea = TouchButton;
					touchTarget = button.target;
				}
				break;
			}
		}
	}

	QPoint pos;
	if (touchPoint(event->pos(), pos))
		emu->updateTouchInput(pos.x(), pos.y(), true);

	QLabel *hit = panelLabelAt(event->pos());
	if (hit != pressedPanelLabel) {
		setPanelLabelPressed(pressedPanelLabel, false);
		pressedPanelLabel = hit;
		setPanelLabelPressed(pressedPanelLabel, true);
	}
}

void PDAScreenWindow::mouseReleaseEvent(QMouseEvent *event)
{
	QPoint pos;
	if (touchPoint(event->pos(), pos))
		emu->updateTouchInput(pos.x(), pos.y(), false);
	touchArea = TouchNone;

	setPanelLabelPressed(pressedPanelLabel, false);
	pressedPanelLabel = nullptr;
}

void PDAScreenWindow::mouseMoveEvent(QMouseEvent *event)
{
	if (event->buttons() & Qt::LeftButton) {
		QPoint pos;
		if (touchPoint(event->pos(), pos))
			emu->updateTouchInput(pos.x(), pos.y(), true);

		// in the compact layout, a button stays pressed until the touch ends
		QLabel *hit = compact ? pressedPanelLabel : panelLabelAt(event->pos());
		if (hit != pressedPanelLabel) {
			setPanelLabelPressed(pressedPanelLabel, false);
			pressedPanelLabel = hit;
			setPanelLabelPressed(pressedPanelLabel, true);
		}
	}
}
