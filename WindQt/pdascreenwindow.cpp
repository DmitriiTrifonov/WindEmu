#include "pdascreenwindow.h"
#include <QKeyEvent>
#include <QMouseEvent>
#include <QTimer>
#ifdef Q_OS_LINUX
#include <linux/input-event-codes.h>
#endif

static const char *PanelLabelNormalStyle = "border: 1px solid palette(mid); background: palette(button);";
static const char *PanelLabelPressedStyle = "border: 1px solid palette(mid); background: palette(highlight); color: palette(highlighted-text);";

QLabel *PDAScreenWindow::addPanelLabel(const QString &text, int x, int y, int w, int h) {
	const int gap = 2;
	QLabel *label = new QLabel(text, this);
	label->setAlignment(Qt::AlignCenter);
	label->setGeometry(x + gap, y + gap, w - gap * 2, h - gap * 2);
	label->setStyleSheet(PanelLabelNormalStyle);
	panelButtons.append({QRect(x, y, w, h), label});
	return label;
}

void PDAScreenWindow::setPanelLabelPressed(QLabel *label, bool pressed) {
	if (label)
		label->setStyleSheet(pressed ? PanelLabelPressedStyle : PanelLabelNormalStyle);
}

PDAScreenWindow::PDAScreenWindow(EmuBase *emu, QWidget *parent) :
	QWidget(parent),
	emu(emu),
	lcd(new QLabel(this))
{
	setWindowTitle("WindEmu");
	setFixedSize(emu->getDigitiserWidth(), emu->getDigitiserHeight());
	lcd->setGeometry(emu->getLCDOffsetX(), emu->getLCDOffsetY(), emu->getLCDWidth(), emu->getLCDHeight());

	const char *who = emu->getDeviceName();
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
		int leftW = emu->getLCDOffsetX();
		int leftH = emu->getLCDHeight() / 5;
		addPanelLabel("➡️", 0, leftH * 0, leftW, leftH);
		addPanelLabel("📄", 0, leftH * 1, leftW, leftH);
		addPanelLabel("📡", 0, leftH * 2, leftW, leftH);
		addPanelLabel("+",  0, leftH * 3, leftW, leftH);
		addPanelLabel("-",  0, leftH * 4, leftW, leftH);

		int barX = 50;
		int barY = leftH * 5;
		int barW = (emu->getDigitiserWidth() - barX) / 8;
		int barH = emu->getDigitiserHeight() - emu->getLCDHeight();
		addPanelLabel("System",   0, barY, barX, barH);
		addPanelLabel("Word",     barX + barW * 0, barY, barW, barH);
		addPanelLabel("Sheet",    barX + barW * 1, barY, barW, barH);
		addPanelLabel("Contacts", barX + barW * 2, barY, barW, barH);
		addPanelLabel("Agenda",   barX + barW * 3, barY, barW, barH);
		addPanelLabel("Email",    barX + barW * 4, barY, barW, barH);
		addPanelLabel("Calc",     barX + barW * 5, barY, barW, barH);
		addPanelLabel("Jotter",   barX + barW * 6, barY, barW, barH);
		addPanelLabel("Extras",   barX + barW * 7, barY, barW, barH);
	}
}

void PDAScreenWindow::updateScreen() {
	uint8_t *lines[1024];
	QImage img(emu->getLCDWidth(), emu->getLCDHeight(), QImage::Format_Grayscale8);
	for (int y = 0; y < img.height(); y++)
		lines[y] = img.scanLine(y);
	emu->readLCDIntoBuffer(lines, false);

	lcd->setPixmap(QPixmap::fromImage(std::move(img)));
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
// layout (e.g. Russian) doesn't matter. Symbols without their own Psion key
// are typed as on the device, with Shift/Fn combinations.
static KeyMapping mapKey(const QKeyEvent *event) {
	static const char row1[] = "1234567890", row2[] = "QWERTYUIOP", row3[] = "ASDFGHJKL", row4[] = "ZXCVBNM";
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
	case KEY_APOSTROPHE: return {EStdKeySingleQuote, false};
	case KEY_COMMA:      return {EStdKeyComma, false};
	case KEY_DOT:        return {EStdKeyFullStop, false};
	case KEY_SPACE:      return {EStdKeySpace, false};
	case KEY_LEFTSHIFT:  return {EStdKeyLeftShift, false};
	case KEY_RIGHTSHIFT: return {EStdKeyRightShift, false};
	case KEY_LEFTCTRL:
	case KEY_RIGHTCTRL:  return {EStdKeyLeftCtrl, false};
	case KEY_LEFTALT:    return {EStdKeyLeftFunc, false};
	case KEY_RIGHTALT:
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
		});
	} else {
		emu->setKeyboardKey(k.key, false);
	}
}


void PDAScreenWindow::mousePressEvent(QMouseEvent *event)
{
	emu->updateTouchInput(event->x(), event->y(), true);

	QLabel *hit = nullptr;
	for (const auto &button : panelButtons) {
		if (button.cellRect.contains(event->pos())) {
			hit = button.label;
			break;
		}
	}
	if (hit != pressedPanelLabel) {
		setPanelLabelPressed(pressedPanelLabel, false);
		pressedPanelLabel = hit;
		setPanelLabelPressed(pressedPanelLabel, true);
	}
}

void PDAScreenWindow::mouseReleaseEvent(QMouseEvent *event)
{
	emu->updateTouchInput(event->x(), event->y(), false);

	setPanelLabelPressed(pressedPanelLabel, false);
	pressedPanelLabel = nullptr;
}

void PDAScreenWindow::mouseMoveEvent(QMouseEvent *event)
{
	if (event->buttons() & Qt::LeftButton) {
		emu->updateTouchInput(event->x(), event->y(), true);

		QLabel *hit = nullptr;
		for (const auto &button : panelButtons) {
			if (button.cellRect.contains(event->pos())) {
				hit = button.label;
				break;
			}
		}
		if (hit != pressedPanelLabel) {
			setPanelLabelPressed(pressedPanelLabel, false);
			pressedPanelLabel = hit;
			setPanelLabelPressed(pressedPanelLabel, true);
		}
	}
}
