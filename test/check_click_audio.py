"""Check that the compressed click header exactly represents the source WAV."""
from pathlib import Path
import re
import sys
import zlib

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from embed_click_audio import click_wav_bytes, generated_header


raw = click_wav_bytes(ROOT / 'src/remote_click.h')
expected_header, compressed = generated_header(raw)
header_path = ROOT / 'src/remote_click_compressed.h'
actual_header = header_path.read_text(encoding='ascii')
if actual_header != expected_header:
    raise SystemExit(f'{header_path} is stale; regenerate it with python tools/embed_click_audio.py')

decoded = zlib.decompress(compressed, wbits=-15)
crc = re.search(r'data_click_wav_crc32\s*=\s*0x([0-9a-fA-F]+)', actual_header)
if decoded != raw or len(decoded) != len(raw) or not crc or int(crc.group(1), 16) != zlib.crc32(raw):
    raise SystemExit('compressed click WAV failed byte, length, or CRC validation')

print(f'click WAV exact round trip: {len(raw)} bytes -> {len(compressed)} bytes; '
      f'saved {len(raw) - len(compressed)} bytes; CRC32 {zlib.crc32(raw):08x}')
