"""Compare real bundled MP3 decoding with 32-bit and ESP32 16-bit Huffman fields.
Run: python test/native_audio/check_mp3_decode.py [--cc path/to/gcc]
Requires a C compiler with 32-bit long (e.g. PlatformIO Windows GCC).
"""
import argparse
import hashlib
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import tempfile
from zipfile import ZipFile

DECODER_SOURCE = r"""
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "config.h"
#include "decoder.h"
#include "huffman.h"
int main(int argc, char **argv) {
    if(argc != 3 || sizeof(long) != 4) { fputs("decoder requires 32-bit long\n", stderr); return 1; }
    if(sizeof(((union huffpair *)0)->final) != EXPECT_HUFF_WORD_BYTES) return 9;
    FILE *in=fopen(argv[1],"rb"), *out=fopen(argv[2],"wb");
    if(!in || !out) return 2;
    fseek(in,0,SEEK_END); long len=ftell(in); rewind(in);
    unsigned char *bytes=calloc(1,(size_t)len+MAD_BUFFER_GUARD);
    if(!bytes || fread(bytes,1,(size_t)len,in)!=(size_t)len) return 3;
    fclose(in);
    struct mad_stream *stream=calloc(1,sizeof(*stream));
    struct mad_frame *frame=calloc(1,sizeof(*frame));
    struct mad_synth *synth=calloc(1,sizeof(*synth));
    if(!stream || !frame || !synth) return 4;
    mad_stream_init(stream); mad_frame_init(frame); mad_synth_init(synth);
    mad_stream_buffer(stream,bytes,(unsigned long)len+MAD_BUFFER_GUARD);
    unsigned frames=0,errors=0,samples=0,nonzero=0,mono=0,stereo=0,joint=0;
    for(long attempts=0; attempts<len*2; ++attempts) {
        if(mad_frame_decode(frame,stream)) {
            if(stream->error==MAD_ERROR_BUFLEN) break;
            if(!MAD_RECOVERABLE(stream->error)) return 5;
            ++errors; continue;
        }
        ++frames;
        mono+=frame->header.mode==MAD_MODE_SINGLE_CHANNEL;
        joint+=frame->header.mode==MAD_MODE_JOINT_STEREO;
        stereo+=frame->header.mode==MAD_MODE_STEREO;
        for(unsigned ns=0; ns<MAD_NSBSAMPLES(&frame->header); ++ns) {
            if(mad_synth_frame_onens(synth,frame,ns)!=MAD_FLOW_CONTINUE) return 6;
            for(unsigned ch=0; ch<synth->pcm.channels; ++ch) {
                if(fwrite(synth->pcm.samples[ch],sizeof(int16_t),synth->pcm.length,out)!=synth->pcm.length) return 7;
                samples+=synth->pcm.length;
                for(unsigned k=0;k<synth->pcm.length;++k) nonzero+=synth->pcm.samples[ch][k]!=0;
            }
        }
    }
    mad_frame_finish(frame); mad_stream_finish(stream);
    free(synth); free(frame); free(stream); free(bytes);
    if(fclose(out) || !frames || !nonzero) return 8;
    printf("frames=%u errors=%u samples=%u nonzero=%u mono=%u joint=%u stereo=%u pair=%u quad=%u dispatch=%u\n",frames,errors,samples,nonzero,mono,joint,stereo,(unsigned)sizeof(union huffpair),(unsigned)sizeof(union huffquad),(unsigned)sizeof(struct hufftable));
    return 0;
}
"""


def unmask(data, seed):
    # Same 32-bit XOR sequence and 1024-byte reset used by cfc()/mpren_renOrder().
    result = bytearray(data)
    seed = (seed + ((seed // 2) << 24)) & 0xffffffff
    for offset in range(0, len(result) // 4 * 4, 4):
        value = struct.unpack_from("<I", result, offset)[0] ^ seed
        struct.pack_into("<I", result, offset, value)
        seed = (seed // 2 + (seed << 31)) & 0xffffffff
    return result


def samples(project):
    with ZipFile(project / "install/sound-pack-rm13.zip") as archive:
        data = archive.read("REMA.bin")
    assert data[:4] == b"REMA" and data[4] == 0x82 and data[5:9] == b"RM13"
    seed = struct.unpack_from("<I", data, 10)[0]
    position = 14
    result = []
    for _ in range(data[9]):
        name = bytes(unmask(data[position:position + 32], seed)).split(b"\0", 1)[0].decode("ascii")
        size = struct.unpack_from("<I", data, position + 32)[0]
        position += 36
        assert position + size <= len(data)
        payload = b"".join(unmask(data[position + offset:position + min(offset + 1024, size)], seed)
                           for offset in range(0, size, 1024))
        position += size
        if name.endswith(".mp3"):
            assert Path(name).name == name
            result.append((name, payload))
    assert position == len(data) and len(result) == 26
    return result


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
    sources = [str(libmad / (name + ".c")) for name in
               ("bit", "fixed", "frame", "huffman", "layer3", "stream", "synth", "timer")]
    totals = dict(frames=0, samples=0, mono=0, joint=0, stereo=0)
    with tempfile.TemporaryDirectory(prefix="remote-mp3-check-") as temporary:
        scratch = Path(temporary)
        (scratch / "decode.c").write_text(DECODER_SOURCE)
        (scratch / "pgmspace.h").write_text("#include <string.h>\n#define PROGMEM\n#define memcpy_P memcpy\n#define PSTR(x) (x)\n")
        executables = []
        for name, word_bytes, defines in (("baseline", 4, []), ("compact", 2, ["-DESP32"])):
            executable = scratch / (name + ".exe")
            command = [args.cc, "-std=c11", "-O2", "-DEXPECT_HUFF_WORD_BYTES=" + str(word_bytes),
                       *defines, "-I", str(scratch), "-I", str(libmad), str(scratch / "decode.c"),
                       *sources, "-o", str(executable)]
            subprocess.run(command, check=True)
            executables.append(executable)
        for name, payload in samples(args.project):
            sample = scratch / "sample.mp3"
            sample.write_bytes(payload)
            outputs, reports = [], []
            for executable in executables:
                pcm = scratch / "output.pcm"
                decoded = subprocess.run([str(executable), str(sample), str(pcm)],
                                         check=True, capture_output=True, text=True)
                outputs.append(pcm.read_bytes())
                reports.append(decoded.stdout.split(" pair=")[0])
            assert outputs[0] == outputs[1] and reports[0] == reports[1], name
            counts = {key: int(value) for key, value in re.findall(r"(\w+)=(\d+)", reports[0])}
            assert counts["frames"] > 0 and counts["nonzero"] > 0, name
            for key in totals:
                totals[key] += counts[key]
            print(name, hashlib.sha256(outputs[0]).hexdigest())
    assert totals["mono"] > 0 and totals["joint"] > 0
    print("26 MP3s produced identical PCM:", totals)


if __name__ == "__main__":
    main()
