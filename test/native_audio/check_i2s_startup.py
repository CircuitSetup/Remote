"""Run the real ESP32 I2S output against a recording hardware boundary.

Run: python test/native_audio/check_i2s_startup.py [--cc path/to/g++]
Checks clock configuration/retries and packed PCM, not hardware latency/underruns.
"""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ARDUINO = r'''
#pragma once
#include <stdint.h>
#include <stddef.h>
#define CONFIG_IDF_TARGET_ESP32 1
#define ESP_IDF_VERSION_VAL(a,b,c) ((a)*10000+(b)*100+(c))
#define ESP_IDF_VERSION ESP_IDF_VERSION_VAL(4,4,7)
struct SerialStub { void println(const char *) {} };
extern SerialStub Serial;
inline void delay(unsigned long) {}
'''

I2S = r'''
#pragma once
#include <stdint.h>
#include <stddef.h>
typedef int esp_err_t;
typedef int i2s_port_t;
typedef int i2s_mode_t;
typedef int i2s_comm_format_t;
constexpr int ESP_OK=0, ESP_FAIL=-1, I2S_MODE_MASTER=1, I2S_MODE_TX=4,
  I2S_MODE_DAC_BUILT_IN=16, I2S_MODE_PDM=64, I2S_COMM_FORMAT_STAND_MSB=2,
  I2S_COMM_FORMAT_STAND_I2S=1, I2S_BITS_PER_SAMPLE_16BIT=16,
  I2S_CHANNEL_FMT_RIGHT_LEFT=0, ESP_INTR_FLAG_LEVEL1=2, I2S_PIN_NO_CHANGE=-1,
  I2S_DAC_CHANNEL_BOTH_EN=3;
struct esp_chip_info_t { int revision; };
inline void esp_chip_info(esp_chip_info_t *info) { info->revision=1; }
struct i2s_pin_config_t { int bck_io_num, ws_io_num, data_out_num, data_in_num; };
// Complete non-TDM ESP32 legacy configuration, in SDK field order.
struct i2s_config_t {
  i2s_mode_t mode;
  uint32_t sample_rate;
  int bits_per_sample, channel_format;
  i2s_comm_format_t communication_format;
  int intr_alloc_flags, dma_buf_count, dma_buf_len;
  bool use_apll, tx_desc_auto_clear;
  int fixed_mclk, mclk_multiple, bits_per_chan;
};
esp_err_t i2s_driver_install(i2s_port_t,const i2s_config_t *,int,void *);
esp_err_t i2s_set_sample_rates(i2s_port_t,uint32_t);
esp_err_t i2s_set_pin(i2s_port_t,const i2s_pin_config_t *);
esp_err_t i2s_set_dac_mode(int);
esp_err_t i2s_zero_dma_buffer(i2s_port_t);
esp_err_t i2s_driver_uninstall(i2s_port_t);
esp_err_t i2s_write(i2s_port_t,const void *,size_t,size_t *,int);
'''

CASES = r'''
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include "AudioOutputI2S.h"
#include "driver/i2s.h"
SerialStub Serial;
#define CHECK(expr) do { if(!(expr)) { std::fprintf(stderr,"line %d: %s\n",__LINE__,#expr); std::exit(1); } } while(0)
std::vector<uint32_t> installs, rates, pcm;
bool installed=false, failInstall=false, failRate=false;
int zeroCalls=0, pinCalls=0;
esp_err_t i2s_driver_install(i2s_port_t,const i2s_config_t *config,int,void *) {
  installs.push_back(config->sample_rate);
  if(failInstall) return ESP_FAIL;
  installed=true; return ESP_OK;
}
esp_err_t i2s_set_sample_rates(i2s_port_t,uint32_t rate) {
  CHECK(installed); rates.push_back(rate); return failRate ? ESP_FAIL : ESP_OK;
}
esp_err_t i2s_set_pin(i2s_port_t,const i2s_pin_config_t *) { CHECK(installed); ++pinCalls; return ESP_OK; }
esp_err_t i2s_set_dac_mode(int) { CHECK(installed); return ESP_OK; }
esp_err_t i2s_zero_dma_buffer(i2s_port_t) { CHECK(installed); ++zeroCalls; return ESP_OK; }
esp_err_t i2s_driver_uninstall(i2s_port_t) { CHECK(installed); installed=false; return ESP_OK; }
esp_err_t i2s_write(i2s_port_t,const void *data,size_t size,size_t *written,int timeout) {
  CHECK(installed && size==4 && timeout==0);
  uint32_t sample; std::memcpy(&sample,data,4); pcm.push_back(sample); *written=4; return ESP_OK;
}
void reset() { CHECK(!installed); installs.clear(); rates.clear(); pcm.clear(); zeroCalls=pinCalls=0; failInstall=failRate=false; }

void default_clock_and_rate_changes() {
  reset(); AudioOutputI2S output;
  CHECK(output.begin()); CHECK(installs==std::vector<uint32_t>{44100});
  CHECK(rates.empty()); // No redundant clock reset after installing the requested rate.
  CHECK(output.SetRate(44100)); CHECK(rates.empty()); // First MP3 frame has the same rate.
  CHECK(output.begin()); CHECK(installs.size()==1 && rates.empty() && zeroCalls==1 && pinCalls==1);
  output.SetGain(0.5f); output.SetChannels(2);
  CHECK(output.ConsumeSample(1000,-2000)==4 && pcm.back()==0xfc1801f4u);
  CHECK(output.SetRate(48000)); CHECK(rates==std::vector<uint32_t>{48000});
  CHECK(output.ConsumeSample(1000,-2000)==4 && pcm.back()==0xfc1801f4u);
  output.SetChannels(1);
  CHECK(output.ConsumeSample(-1000,2000)==4 && pcm.back()==0xfe0cfe0cu);
  output.SetChannels(2); output.SetGain(0.5f,1);
  CHECK(output.ConsumeSample(1000,-2000)==4 && pcm.back()==0xfc180000u);
  CHECK(output.stop()); CHECK(output.begin());
  CHECK((installs==std::vector<uint32_t>{44100,48000}));
  CHECK(rates==std::vector<uint32_t>{48000});
  CHECK(output.SetRate(44100)); CHECK((rates==std::vector<uint32_t>{48000,44100}));
}

void prebegin_rate_and_driver_failures() {
  reset(); AudioOutputI2S output;
  CHECK(output.SetRate(22050)); CHECK(rates.empty());
  failInstall=true;
  CHECK(!output.begin());
  CHECK(!installed && zeroCalls==0 && pinCalls==0 && rates.empty());
  CHECK(output.ConsumeSample(1000,-2000)==0 && pcm.empty());
  failInstall=false; CHECK(output.begin());
  CHECK((installs==std::vector<uint32_t>{22050,22050}));
  CHECK(zeroCalls==1 && pinCalls==1 && rates.empty());
  failRate=true; CHECK(!output.SetRate(48000));
  failRate=false; CHECK(output.SetRate(48000));
  CHECK((rates==std::vector<uint32_t>{48000,48000}));
  CHECK(output.SetRate(48000)); CHECK(rates.size()==2);
  // A failed clock change may partially affect hardware, invalidating the old-rate cache.
  failRate=true; CHECK(!output.SetRate(44100));
  failRate=false; CHECK(output.SetRate(48000));
  CHECK((rates==std::vector<uint32_t>{48000,48000,44100,48000}));
  CHECK(output.SetRate(48000)); CHECK(rates.size()==4);
  CHECK(output.stop()); CHECK(output.begin());
  CHECK(installs.back()==48000 && rates.size()==4);
}

class AdjustedOutput : public AudioOutputI2S {
  int AdjustI2SRate(int hz) override { return hz+100; }
};
void adjusted_clock_uses_same_mapping() {
  reset(); AdjustedOutput output;
  CHECK(output.SetRate(22050)); CHECK(output.begin());
  CHECK(installs==std::vector<uint32_t>{22150} && rates.empty());
  CHECK(output.SetRate(22050)); CHECK(rates.empty());
  CHECK(output.SetRate(48000)); CHECK(rates==std::vector<uint32_t>{48100});
  CHECK(output.stop()); CHECK(output.begin());
  CHECK((installs==std::vector<uint32_t>{22150,48100}) && rates.size()==1);
}
int main() {
  default_clock_and_rate_changes();
  prebegin_rate_and_driver_failures();
  adjusted_clock_uses_same_mapping();
  std::puts("Real ESP32 I2S startup clocks, rate retries, restarts and packed PCM passed");
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--project", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--cc", default=shutil.which("g++"))
    args = parser.parse_args()
    if not args.cc and os.name == "nt":
        args.cc = str(Path.home() / ".platformio/packages/toolchain-gccmingw32/bin/g++.exe")
    if not args.cc:
        parser.error("pass --cc with a C++ compiler")
    audio = args.project.resolve() / "src/src/ESP8266Audio"
    with tempfile.TemporaryDirectory(prefix="remote-i2s-check-") as temporary:
        scratch = Path(temporary)
        (scratch / "driver").mkdir()
        (scratch / "Arduino.h").write_text(ARDUINO)
        (scratch / "driver/i2s.h").write_text(I2S)
        (scratch / "check.cpp").write_text(CASES)
        executable = scratch / "check.exe"
        subprocess.run([args.cc, "-std=gnu++11", "-DESP32", "-Wno-narrowing", "-I", str(scratch),
                        "-I", str(audio), str(audio / "AudioOutputI2S.cpp"),
                        str(scratch / "check.cpp"), "-o", str(executable)], check=True)
        subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    main()
