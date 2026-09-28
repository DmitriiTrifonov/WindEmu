#ifndef PDASCREENWINDOW_H
#define PDASCREENWINDOW_H

#include <QWidget>
#include <QLabel>
#include <QVector>
#include <QRect>
#include "emubase.h"

class PDAScreenWindow : public QWidget
{
	Q_OBJECT
private:
	struct PanelButton { QRect cellRect; QLabel *label; };

	EmuBase *emu;
	QLabel *lcd;
	QVector<PanelButton> panelButtons;
	QLabel *pressedPanelLabel = nullptr;

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
