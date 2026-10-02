#pragma once
#include "../WindCore/emubase.h"
#include <QElapsedTimer>

// Plays the Psion's sound on the host (through ALSA on Linux), touching the
// sound device only while there's something to hear
class AudioOutput {
	EmuBase *emu;
	void *pcm = nullptr;
	int silentSamples = 0;
	bool playing = false;
	// for telling a stalled host from a slow emulation when the sound breaks up
	QElapsedTimer sinceStart, sinceUpdate;
	qint64 samplesSinceStart = 0, longestWait = 0;

public:
	explicit AudioOutput(EmuBase *emu);
	~AudioOutput();
	// passes on the sound made since the last call
	void update();
};
