#ifndef PDASCREENWINDOW_H
#define PDASCREENWINDOW_H

#include <QWidget>
#include <QLabel>
#include <QVector>
#include <QRect>
#include <QHash>
#include "emubase.h"

// a host key press as Psion keys: the key itself plus combination adjustments
struct KeyMapping { EpocKey key; bool withFn = false; bool dropCtrl = false; };

class PDAScreenWindow : public QWidget
{
	Q_OBJECT
private:
	struct PanelButton { QRect cellRect; QLabel *label; };

	EmuBase *emu;
	QLabel *lcd;
	QVector<PanelButton> panelButtons;
	QLabel *pressedPanelLabel = nullptr;
	// what each held host key was pressed as, so its release matches
	QHash<quint32, KeyMapping> heldKeys;

	QLabel *addPanelLabel(const QString &text, int x, int y, int w, int h);
	void setPanelLabelPressed(QLabel *label, bool pressed);

public:
	explicit PDAScreenWindow(EmuBase *emu, QWidget *parent = nullptr);

public slots:
	void updateScreen();

protected:
	void keyPressEvent(QKeyEvent *event) override;
	void keyReleaseEvent(QKeyEvent *event) override;
	void mousePressEvent(QMouseEvent *event) override;
	void mouseReleaseEvent(QMouseEvent *event) override;
	void mouseMoveEvent(QMouseEvent *event) override;
};

#endif // PDASCREENWINDOW_H
