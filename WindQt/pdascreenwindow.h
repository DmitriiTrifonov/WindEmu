#ifndef PDASCREENWINDOW_H
#define PDASCREENWINDOW_H

#include <QWidget>
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QGraphicsProxyWidget>
#include <QImage>
#include <QLabel>
#include <QVector>
#include <QRect>
#include <QHash>
#include <functional>
#include "emubase.h"

// a host key press as Psion keys: the key itself plus combination adjustments
struct KeyMapping {
	EpocKey key;
	bool withFn = false;
	bool dropCtrl = false;
	bool dropShift = false; // for a symbol the Psion types without Shift
	bool addShift = false;  // for a symbol the Psion types with Shift
};

class PDAScreenWindow : public QWidget
{
	Q_OBJECT
private:
	// target is where the button lies on the digitiser; a button with an
	// action instead is the emulator's own, rather than the Psion's
	struct PanelButton { QRect cellRect; QLabel *label; QPoint target; std::function<void()> action; };

	EmuBase *emu;
	QLabel *lcd;
	QRect lcdRect;
	// the compact layout rearranges the silkscreen around the LCD, so the
	// widget's own coordinates no longer match the digitiser's
	bool compact = false;
	enum TouchArea { TouchNone, TouchLcd, TouchButton, TouchAnywhere };
	TouchArea touchArea = TouchNone;
	QPoint touchTarget;

	// the CF card that the card button or menu puts in
	QString cardPath;
	QLabel *cardButton = nullptr;
	void toggleCard();
	void updateCardButton();
	QImage lastFrame;
	QVector<PanelButton> panelButtons;
	QLabel *pressedPanelLabel = nullptr;
	// what each held host key was pressed as, so its release matches
	QHash<quint32, KeyMapping> heldKeys;
	void setShiftKeys(bool pressed);

	QLabel *addPanelLabel(const QString &text, int x, int y, int w, int h);
	QLabel *addPanelButton(const QString &text, const QRect &shown, const QRect &onDigitiser);
	void setPanelLabelPressed(QLabel *label, bool pressed);
	QLabel *panelLabelAt(const QPoint &pos) const;
	bool touchPoint(const QPoint &pos, QPoint &digitiserPos) const;

public:
	void setCardPath(const QString &path);

	// compact puts the Series 5mx's silkscreen buttons above and below its LCD,
	// for wide screens such as a phone's held sideways
	explicit PDAScreenWindow(EmuBase *emu, bool compact = false, QWidget *parent = nullptr);

public slots:
	void updateScreen();

protected:
	void keyPressEvent(QKeyEvent *event) override;
	void keyReleaseEvent(QKeyEvent *event) override;
	void mousePressEvent(QMouseEvent *event) override;
	void mouseReleaseEvent(QMouseEvent *event) override;
	void mouseMoveEvent(QMouseEvent *event) override;
	void contextMenuEvent(QContextMenuEvent *event) override;
};

// Shows the Psion scaled to fill the window, keeping its shape
class ScaledScreenView : public QGraphicsView
{
	QGraphicsScene scene;
	QGraphicsProxyWidget *proxy;

public:
	explicit ScaledScreenView(QWidget *screen);
	~ScaledScreenView() override;

protected:
	void resizeEvent(QResizeEvent *event) override;
};

#endif // PDASCREENWINDOW_H
