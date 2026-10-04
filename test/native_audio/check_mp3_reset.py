"""Check libmad resets against poisoned reusable buffers using its actual C sources."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


CHECK_SOURCE = r'''
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "config.h"
#include "frame.h"
#include "synth.h"
#include "decoder.h"

_Static_assert(sizeof(long) == 4 && sizeof(mad_fixed_t) == 4,
               "use a compiler with 32-bit long and mad_fixed_t");

/* Guard bytes and full-object comparisons catch clearing neighboring fields. */
struct frame_guard { unsigned char before[8]; struct mad_frame value; unsigned char after[8]; };
struct synth_guard { unsigned char before[8]; struct mad_synth value; unsigned char after[8]; };

static void poison(void *memory, size_t size, unsigned seed)
{
  unsigned char *bytes = memory;
  for (size_t i = 0; i < size; ++i) bytes[i] = (unsigned char)(1 + (i * 73 + seed * 39) % 255);
}

/* Build byte-level expectations independently of the production clear loops. */
static void zero_bytes(void *memory, size_t size)
{
  unsigned char *bytes = memory;
  for (size_t i = 0; i < size; ++i) bytes[i] = 0;
}

static void expected_frame_mute(struct mad_frame *frame)
{
  zero_bytes(frame->sbsample, sizeof(frame->sbsample));
  zero_bytes(frame->overlap, sizeof(frame->overlap));
}

static int same(const void *actual, const void *expected, size_t size, const char *name, unsigned seed)
{
  const unsigned char *a = actual, *e = expected;
  for (size_t i = 0; i < size; ++i) if (a[i] != e[i]) {
    fprintf(stderr, "%s pattern %u byte %lu: got %u expected %u\n", name, seed,
            (unsigned long)i, a[i], e[i]);
    return 0;
  }
  return 1;
}

int main(void)
{
  struct frame_guard frame, expected_frame;
  struct synth_guard synth, expected_synth;
  for (unsigned seed = 0; seed < 8; ++seed) {
    poison(&frame, sizeof(frame), seed);
    memcpy(&expected_frame, &frame, sizeof(frame));
    expected_frame_mute(&expected_frame.value);
    mad_frame_mute(&frame.value);
    if (!same(&frame, &expected_frame, sizeof(frame), "frame mute", seed)) return 1;

    poison(&synth, sizeof(synth), seed);
    memcpy(&expected_synth, &synth, sizeof(synth));
    zero_bytes(expected_synth.value.filter, sizeof(expected_synth.value.filter));
    mad_synth_mute(&synth.value);
    if (!same(&synth, &expected_synth, sizeof(synth), "synth mute", seed)) return 1;

    poison(&frame, sizeof(frame), seed);
    memcpy(&expected_frame, &frame, sizeof(frame));
    expected_frame_mute(&expected_frame.value);
    zero_bytes(&expected_frame.value.header, sizeof(expected_frame.value.header));
    expected_frame.value.options = 0;
    mad_frame_init(&frame.value);
    if (!same(&frame, &expected_frame, sizeof(frame), "frame init", seed)) return 1;

    poison(&synth, sizeof(synth), seed);
    memcpy(&expected_synth, &synth, sizeof(synth));
    zero_bytes(expected_synth.value.filter, sizeof(expected_synth.value.filter));
    expected_synth.value.phase = 0;
    expected_synth.value.pcm.samplerate = 0;
    expected_synth.value.pcm.channels = 0;
    expected_synth.value.pcm.length = 0;
    mad_synth_init(&synth.value);
    if (!same(&synth, &expected_synth, sizeof(synth), "synth init", seed)) return 1;
  }
  puts("PASS: 8 poison patterns x 4 libmad resets; complete arrays cleared, neighboring fields and guards preserved");
  return 0;
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--project", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--cc", default=shutil.which("gcc"))
    args = parser.parse_args()
    if not args.cc and os.name == "nt":
        args.cc = str(Path.home() / ".platformio/packages/toolchain-gccmingw32/bin/gcc.exe")
    if not args.cc:
        parser.error("pass --cc with a compiler supporting 32-bit long")
    libmad = args.project.resolve() / "src/src/ESP8266Audio/libmad"
    with tempfile.TemporaryDirectory(prefix="remote-mp3-reset-") as temporary:
        scratch = Path(temporary)
        (scratch / "check.c").write_text(CHECK_SOURCE)
        (scratch / "pgmspace.h").write_text("#include <string.h>\n#define PROGMEM\n#define memcpy_P memcpy\n#define PSTR(x) (x)\n")
        executable = scratch / "check.exe"
        subprocess.run([args.cc, "-std=c11", "-O2",
                        "-I", str(scratch), "-I", str(libmad), str(scratch / "check.c"),
                        *[str(libmad / (name + ".c")) for name in
                          ("bit", "fixed", "frame", "huffman", "layer3", "stream", "synth", "timer")],
                        "-o", str(executable)], check=True)
        subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    main()
