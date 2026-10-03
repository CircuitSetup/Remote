"""Compare two ESP32 ELF files built with the same toolchain and flags."""
from pathlib import Path
import shutil
import subprocess
import sys

SIZE = shutil.which('xtensa-esp32-elf-size') or str(
    Path.home() / '.platformio/packages/toolchain-xtensa-esp32/bin/xtensa-esp32-elf-size.exe')


def memory(path):
    output = subprocess.check_output([SIZE, '-A', path], text=True)
    sections = {}
    for line in output.splitlines():
        fields = line.split()
        if len(fields) == 3 and fields[0].startswith('.'):
            sections[fields[0]] = int(fields[1])
    flash = sum(sections[name] for name in (
        '.iram0.vectors', '.iram0.text', '.dram0.data', '.flash.text', '.flash.rodata'))
    ram = sections['.dram0.data'] + sections['.dram0.bss']
    return flash, ram


if __name__ == '__main__':
    before, after = (memory(path) for path in sys.argv[1:3])
    print(f'Program: {before[0]:,} -> {after[0]:,} bytes; RAM: {before[1]:,} -> {after[1]:,} bytes')
    assert after[0] < before[0], 'Program memory must decrease'
    assert after[1] <= before[1], 'Static RAM must not increase'
