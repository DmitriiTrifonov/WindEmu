#include "audiooutput.h"
#include <QtGlobal>
#include <stdio.h>
#include <algorithm>
#ifdef Q_OS_LINUX
#include <alsa/asoundlib.h>
#endif

// stop playing after this much silence, so the device can rest
static const int SilenceBeforeStopping = EmuBase::AudioSampleRate;

AudioOutput::AudioOutput(EmuBase *emu) : emu(emu)
{
#ifdef Q_OS_LINUX
	snd_pcm_t *handle;
	if (snd_pcm_open(&handle, "default", SND_PCM_STREAM_PLAYBACK, SND_PCM_NONBLOCK) < 0) {
		fprintf(stderr, "No sound: can't open the sound device\n");
		return;
	}
	// a quarter of a second of buffering rides out the emulation's unevenness,
	// such as the time the scaled-up screen takes to redraw
	if (snd_pcm_set_params(handle, SND_PCM_FORMAT_S16_LE, SND_PCM_ACCESS_RW_INTERLEAVED, 1,
	                       EmuBase::AudioSampleRate, 1, 250000) < 0) {
		fprintf(stderr, "No sound: the sound device doesn't take 16kHz mono\n");
		snd_pcm_close(handle);
		return;
	}
	pcm = handle;
#endif
}

AudioOutput::~AudioOutput()
{
#ifdef Q_OS_LINUX
	if (pcm)
		snd_pcm_close((snd_pcm_t *)pcm);
#endif
}

void AudioOutput::update()
{
	if (playing)
		longestWait = std::max(longestWait, sinceUpdate.elapsed());
	sinceUpdate.start();
	int16_t samples[4096];
	size_t count;
	while ((count = emu->readAudio(samples, sizeof(samples) / sizeof(samples[0]))) > 0) {
		bool silent = true;
		for (size_t i = 0; i < count && silent; i++)
			silent = (samples[i] == 0);
		silentSamples = silent ? silentSamples + (int)count : 0;
		if (!playing && silent)
			continue;
		bool wasPlaying = playing;
		playing = silentSamples < SilenceBeforeStopping;
		if (!wasPlaying) {
			sinceStart.start();
			samplesSinceStart = 0;
			longestWait = 0;
		}
		samplesSinceStart += count;
#ifdef Q_OS_LINUX
		if (!pcm)
			continue;
		snd_pcm_t *handle = (snd_pcm_t *)pcm;
		snd_pcm_sframes_t written = snd_pcm_writei(handle, samples, count);
		if (written == -EPIPE || written == -ESTRPIPE) {
			// it ran dry: a gap, unless nothing was playing before
			if (playing && wasPlaying)
				fprintf(stderr, "Sound: the host's buffer ran dry %lldms in, with %lldms of sound made; "
				        "longest wait between updates %lldms\n", (long long)sinceStart.elapsed(),
				        samplesSinceStart * 1000 / EmuBase::AudioSampleRate, (long long)longestWait);
			snd_pcm_prepare(handle);
			snd_pcm_writei(handle, samples, count);
		}
		// with the buffer full (-EAGAIN), the rest is dropped to keep up
#endif
	}
}
