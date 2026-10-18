"""Run the real playback/tag lifecycle with fake decoder and file devices."""
from check_crsf_adc import ROOT, compile_and_run
from check_crsf_local_actions import function

source = (ROOT / "src/remote_audio.cpp").read_text()
production = "\n".join(function(source, name) for name in
    ("play_file", "audio_loop", "stopAudio", "timeTravelAudioPlaying"))
fixture = r'''
#include <cstdint>
#include <cassert>
#include "remote_audio.h"
struct Decoder {
    bool running = false, more = true;
    bool isRunning() { return running; }
    bool loop() { return more; }
    void stop() { running = false; }
} mp3Device, wavDevice, *mp3 = &mp3Device, *wav = &wavDevice;
bool haveFile = true, decoderStarts = true;
struct File { bool open(const char *) { return haveFile; } } sd, fs, *mySD0L = &sd, *myFS0L = &fs;
struct Output { void SetGain(float) {} } output, *out = &output;
bool audioInitDone = true, audioMute = false, haveSD = true, haveFS = true, FlashROMode = false;
bool dynVol = false, mpActive = false, appendFile = false;
uint32_t playflags = 0, append_flags = 0;
float curVolFact = 1, append_vol = 1;
const char *append_audio_file = "/next.mp3";
int sampleCnt = 0, musicNext = 0;
float getVolume() { return 1; }
void setupLoopAndBegin(File *, uint32_t) { mp3->running = decoderStarts; mp3->more = true; }
void mp_next(bool) { ++musicNext; }
'''
cases = r'''
int main() {
    const uint32_t flags = PA_INTRMUS|PA_ALLOWSD|PA_DYNVOL|PA_TIMETRAVEL;
    assert(!timeTravelAudioPlaying());
    play_file("/timetravel.mp3", flags); assert(timeTravelAudioPlaying());
    audio_loop(); assert(timeTravelAudioPlaying());
    mp3->more = false; audio_loop(); assert(!timeTravelAudioPlaying());
    play_file("/timetravel.mp3", flags); stopAudio(); assert(!timeTravelAudioPlaying());
    play_file("/timetravel.mp3", flags);
    play_file("/other.mp3", PA_INTRMUS); assert(mp3->running && !timeTravelAudioPlaying());
    haveFile = false; play_file("/timetravel.mp3", flags); assert(!timeTravelAudioPlaying());
    haveFile = true; decoderStarts = false;
    play_file("/timetravel.mp3", flags); assert(!timeTravelAudioPlaying());
    decoderStarts = true; audioMute = true;
    play_file("/timetravel.mp3", flags); assert(!timeTravelAudioPlaying());
    audioMute = false;
    play_file("/protected.mp3", PA_NOINTR|PA_INTRMUS);
    play_file("/timetravel.mp3", flags); assert(!timeTravelAudioPlaying());
    stopAudio();
    play_file("/timetravel.mp3", flags); appendFile = true;
    mp3->more = false; audio_loop(); assert(mp3->running && !timeTravelAudioPlaying());
}
'''
compile_and_run(fixture + production + cases, [])
print("Time-travel audio completion, interruption, failed start, mute and queued sound checks passed")
