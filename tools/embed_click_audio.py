"""Generate the raw-DEFLATE built-in click asset from its lossless WAV source."""
import argparse
from pathlib import Path
import re
import zlib


def click_wav_bytes(source):
    text = Path(source).read_text(encoding='ascii')
    match = re.search(r'unsigned\s+char\s+data_click_wav\s*\[\s*\]\s*=\s*\{(.*?)\};', text, re.S)
    length = re.search(r'const\s+unsigned\s+int\s+data_click_wav_len\s*=\s*(\d+)\s*;', text)
    if not match or not length:
        raise ValueError(f'Could not find click WAV data and declared length in {source}')
    values = re.findall(r'0[xX]([0-9a-fA-F]{1,2})', match.group(1))
    data = bytes(int(value, 16) for value in values)
    if len(data) != int(length.group(1)):
        raise ValueError(f'Click WAV declares {length.group(1)} bytes but contains {len(data)}')
    return data


def generated_header(raw):
    compressor = zlib.compressobj(level=9, wbits=-15)
    compressed = compressor.compress(raw) + compressor.flush()
    restored = zlib.decompress(compressed, wbits=-15)
    if restored != raw:
        raise ValueError('Raw-DEFLATE round trip changed click WAV bytes')
    lines = [
        '#pragma once',
        '#include <stdint.h>',
        f'static const uint8_t data_click_wav_deflate[{len(compressed)}] PROGMEM = {{',
    ]
    lines.extend('    ' + ', '.join(f'0x{value:02x}' for value in compressed[index:index + 16]) + ','
                 for index in range(0, len(compressed), 16))
    lines.extend([
        '};',
        f'static constexpr uint32_t data_click_wav_deflate_len = {len(compressed)};',
        f'static constexpr uint32_t data_click_wav_decoded_len = {len(raw)};',
        f'static constexpr uint32_t data_click_wav_crc32 = 0x{zlib.crc32(raw):08x};',
        '',
    ])
    return '\n'.join(lines), compressed


def generate(source, output):
    raw = click_wav_bytes(source)
    header, compressed = generated_header(raw)
    Path(output).write_text(header, encoding='ascii', newline='\n')
    return len(raw), len(compressed), zlib.crc32(raw)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, default=Path('src/remote_click.h'))
    parser.add_argument('--output', type=Path, default=Path('src/remote_click_compressed.h'))
    args = parser.parse_args()
    raw_size, compressed_size, crc = generate(args.source, args.output)
    print(f'click WAV: {raw_size} -> {compressed_size} bytes; saved {raw_size - compressed_size}; CRC32 {crc:08x}')
