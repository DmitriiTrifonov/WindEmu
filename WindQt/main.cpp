#include "mainwindow.h"
#include <QApplication>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QStandardPaths>
#include <QTimer>
#include <algorithm>
#include <cstdio>
#include "../WindCore/clps7111.h"
#include "../WindCore/windermere.h"

static EmuBase *createEmulator(QByteArray &buffer)
{
	EmuBase *emu = nullptr;
	uint8_t *romData = (uint8_t *)buffer.data();

	// parse this ROM to learn what hardware it's for
	int variantFile = *((uint32_t *)&romData[0x80 + 0x4C]) & 0xFFFFFFF;
	if (variantFile < (buffer.size() - 8)) {
		int variantImg = *((uint32_t *)&romData[variantFile + 4]) & 0xFFFFFFF;
		if (variantImg < (buffer.size() - 0x70)) {
			int variant = *((uint32_t *)&romData[variantImg + 0x60]);

			if (variant == 0x7060001) {
				// 5mx ROM
				emu = new Windermere::Emulator;
			} else if (variant == 0x5040001) {
				// Osaris ROM
				emu = new CLPS7111::Emulator;
			}
		}
	}

	if (emu)
		emu->loadROM(romData, buffer.size());
	return emu;
}

// Runs emulation directly, for use outside the UI's timer-driven loop
static void runFor(EmuBase *emu, double seconds)
{
	int64_t until = (int64_t)emu->currentCycles() + (int64_t)(seconds * emu->getClockSpeed());
	while ((int64_t)emu->currentCycles() < until) {
		int64_t before = emu->currentCycles();
		emu->executeUntil(std::min(until, before + emu->getClockSpeed() / 64));
		if ((int64_t)emu->currentCycles() == before)
			break; // stopped at a breakpoint
	}
}

// Switches the Psion off (Fn+Esc), so that when the saved state is resumed it
// wakes up and re-reads the clock, as after real standby.
static void powerOff(EmuBase *emu)
{
	if (emu->isAsleep())
		return;
	emu->setKeyboardKey(EStdKeyLeftFunc, true);
	runFor(emu, 0.1);
	emu->setKeyboardKey(EStdKeyEscape, true);
	for (int i = 0; i < 100 && !emu->isAsleep(); i++)
		runFor(emu, 0.1);
	emu->setKeyboardKey(EStdKeyEscape, false);
	emu->setKeyboardKey(EStdKeyLeftFunc, false);
	if (!emu->isAsleep())
		fprintf(stderr, "Psion didn't switch off; saving it while running\n");
}

static bool loadState(EmuBase *emu, const QString &path)
{
	FILE *file = fopen(QFile::encodeName(path).constData(), "rb");
	if (!file)
		return false;
	bool ok = emu->loadState(file);
	fclose(file);
	return ok;
}

static void saveState(EmuBase *emu, const QString &path)
{
	QDir().mkpath(QFileInfo(path).path());
	// write beside the old state and swap it in, so a failed write can't lose it
	QByteArray tmpPath = QFile::encodeName(path + ".tmp");
	FILE *file = fopen(tmpPath.constData(), "wb");
	bool ok = file && emu->saveState(file);
	if (file && fclose(file) != 0)
		ok = false;
	if (ok && std::rename(tmpPath.constData(), QFile::encodeName(path).constData()) == 0) {
		fprintf(stderr, "Saved state to %s\n", qPrintable(path));
	} else {
		fprintf(stderr, "Could not save state to %s\n", qPrintable(path));
		std::remove(tmpPath.constData());
	}
}

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
	auto args = a.arguments();
	bool coldBoot = args.removeAll(QStringLiteral("--cold-boot")) > 0;
	bool fullScreen = args.removeAll(QStringLiteral("--fullscreen")) > 0;
	QString cfImage;
	int cfIndex = args.indexOf(QStringLiteral("--cf"));
	if (cfIndex > 0 && cfIndex + 1 < args.length()) {
		cfImage = args.at(cfIndex + 1);
		args.removeAt(cfIndex + 1);
		args.removeAt(cfIndex);
	}

	QString romFile;
	if (args.length() > 1)
		romFile = args.last();
	else
		romFile = QFileDialog::getOpenFileName(nullptr, "Select a ROM");
	if (romFile.isNull()) return 0;

	// what do we have?
	QFile f(romFile);
	f.open(QFile::ReadOnly);
	auto buffer = f.readAll();
	f.close();

	if (buffer.size() < 0x400000) {
		QMessageBox::critical(nullptr, "WindEmu", "Invalid ROM file!");
		return 0;
	}

	EmuBase *emu = createEmulator(buffer);
	if (!emu) {
		QMessageBox::critical(nullptr, "WindEmu", "Unrecognised ROM file!");
		return 0;
	}

	// the card goes in before any saved state is loaded, as that holds the card's own state
	auto insertCard = [&cfImage](EmuBase *emu) {
		if (!cfImage.isEmpty() && !emu->insertCFCard(QFile::encodeName(cfImage).constData()))
			fprintf(stderr, "Could not use %s as a CF card\n", qPrintable(cfImage));
	};
	insertCard(emu);

	QString statePath = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
		+ "/WindEmu/" + QFileInfo(romFile).fileName() + ".state";
	if (emu->supportsSnapshots() && !coldBoot && QFile::exists(statePath)) {
		if (loadState(emu, statePath)) {
			fprintf(stderr, "Resumed from %s\n", qPrintable(statePath));
			if (emu->isAsleep()) {
				// press On (Esc) to wake it, as after real standby
				emu->setKeyboardKey(EStdKeyEscape, true);
				QTimer::singleShot(200, [emu] { emu->setKeyboardKey(EStdKeyEscape, false); });
			}
		} else {
			fprintf(stderr, "Could not load %s; cold booting\n", qPrintable(statePath));
			delete emu;
			emu = createEmulator(buffer);
			insertCard(emu);
		}
	}

	MainWindow w(emu, fullScreen);
	w.setCardPath(cfImage);
	int result = a.exec();

	if (emu->supportsSnapshots()) {
		powerOff(emu);
		saveState(emu, statePath);
	}
	// after switching off, so EPOC has finished writing to the card
	if (!emu->ejectCFCard())
		fprintf(stderr, "Not all changes on the CF card could be copied back to %s\n", qPrintable(cfImage));
	return result;
}
