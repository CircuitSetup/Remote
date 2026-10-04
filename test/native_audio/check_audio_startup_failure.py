"""Check startup failure cleanup using real playback wrappers and ESP32 I2S output.

Run: python test/native_audio/check_audio_startup_failure.py [--cc path/to/g++]
Only file and codec boundaries are stubbed; clock behavior uses the real output.
"""
import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

from check_i2s_startup import ARDUINO, I2S, CASES


BOUNDARIES = r'''
class AudioFileSourceLoop {
public:
  bool opened=false;
  int opens=0, closes=0, startWrites=0, start=0;
  bool open(const char *) { ++opens; opened=true; return true; }
  bool close() { ++closes; opened=false; return true; }
  void setEndPos(int) {} void setPlayLoop(bool) {}
  void setStartPos(int pos) { ++startWrites; start=pos; }
  int read(void *data,int size) { std::memset(data,0,size); return size; }
  bool seek(int,int) { return true; } int getPos() { return 0; }
};
bool failCodec=false;
class CodecBoundary {
  AudioOutputI2S *output=nullptr;
  AudioFileSourceLoop *file=nullptr;
public:
  int attempts=0, startPos=777;
  bool running=false;
  bool begin(AudioFileSourceLoop *source,AudioOutputI2S *sink) {
    ++attempts; file=source; output=sink;
    output->SetBitsPerSample(16); output->SetChannels(2);
    if(!output->begin() || failCodec) return false;
    running=true; return true;
  }
  bool isRunning() { return running; }
  void stop() { running=false; output->stop(); file->close(); }
};
bool appendFile=false, audioMute=false, mpActive=false, haveSD=false,
  haveFS=true, FlashROMode=false, dynVol=false;
uint32_t playflags=0;
float curVolFact=1;
AudioFileSourceLoop *mySD0L, *myFS0L;
CodecBoundary *mp3, *wav;
AudioOutputI2S *out;
float getVolume() { return 1.0f; }
'''

CHECKS = r'''
int main() {
  for(bool fromSD : {false,true}) for(bool useWav : {false,true})
  for(bool codecFailure : {false,true}) {
    reset();
    AudioOutputI2S output;
    AudioFileSourceLoop sd, fs;
    CodecBoundary mp3Codec, wavCodec;
    out=&output; mySD0L=&sd; myFS0L=&fs; mp3=&mp3Codec; wav=&wavCodec;
    haveSD=fromSD; playflags=0; failInstall=!codecFailure; failCodec=codecFailure;
    AudioFileSourceLoop &selected=fromSD ? sd : fs, &unused=fromSD ? fs : sd;
    std::printf("source=%s codec=%s failure=%s\n",fromSD ? "SD" : "FS",
                useWav ? "WAV" : "MP3",codecFailure ? "codec" : "I2S install");
    play_file("/brake.mp3",PA_NOINTR|PA_INTRMUS|PA_ALLOWSD|PA_DYNVOL|
              (useWav ? PA_WAV : 0),1.0f);
    CHECK(playflags==0);
    CHECK(selected.closes==1 && !selected.opened && unused.opens==0);
    CHECK(!mp3->isRunning() && !wav->isRunning() && !installed);
    if(useWav) CHECK(selected.startWrites==0);

    failInstall=failCodec=false;
    int attempts=mp3->attempts;
    play_file("/next.mp3",PA_INTRMUS|PA_ALLOWSD,1.0f);
    CHECK(mp3->attempts==attempts+1 && mp3->isRunning() && installed);
    CHECK(selected.opened && selected.opens==2);
    mp3->stop();
    play_file("/next.wav",PA_INTRMUS|PA_ALLOWSD|PA_WAV,1.0f);
    CHECK(wav->isRunning() && installed && selected.start==wav->startPos);
    wav->stop();
  }
  std::puts("PASS: SD/FS MP3/WAV startup failures release playback/source/driver and recover");
}
'''


def function(source, name):
    """Extract the actual definition without fixing its return type."""
    start = re.search(r"^(?:static )?\w+ " + re.escape(name) + r"\(", source, re.M).start()
    return source[start:source.index("\n}", start) + 2] + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--project", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--cc", default=shutil.which("g++"))
    args = parser.parse_args()
    if not args.cc and os.name == "nt":
        args.cc = str(Path.home() / ".platformio/packages/toolchain-gccmingw32/bin/g++.exe")
    if not args.cc:
        parser.error("pass --cc with a C++ compiler")
    project = args.project.resolve()
    audio = project / "src/src/ESP8266Audio"
    source = (project / "src/remote_audio.cpp").read_text()
    flags = "\n".join(line for line in (project / "src/remote_audio.h").read_text().splitlines()
                      if line.startswith("#define PA_"))
    driver = CASES[:CASES.index("void default_clock_and_rate_changes()")]
    production = "".join(function(source, name) for name in
                         ("skipID3", "setupLoopAndBegin", "play_file"))
    with tempfile.TemporaryDirectory(prefix="remote-audio-failure-") as temporary:
        scratch = Path(temporary)
        (scratch / "driver").mkdir()
        (scratch / "Arduino.h").write_text(ARDUINO)
        (scratch / "driver/i2s.h").write_text(I2S)
        (scratch / "check.cpp").write_text(driver + flags + "\n" + BOUNDARIES + production + CHECKS)
        executable = scratch / "check.exe"
        subprocess.run([args.cc, "-std=gnu++11", "-DESP32", "-Wno-narrowing",
                        "-I", str(scratch), "-I", str(audio),
                        str(audio / "AudioOutputI2S.cpp"), str(scratch / "check.cpp"),
                        "-o", str(executable)], check=True)
        subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    main()
