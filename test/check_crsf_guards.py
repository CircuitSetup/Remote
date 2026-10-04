"""Run with python test/check_crsf_guards.py; check CRSF on/off compilation."""
from pathlib import Path
import shutil
import subprocess
import tempfile

from check_crsf_adc import COMPILER, ROOT

NM = shutil.which('nm') or str(Path(COMPILER).with_name('nm.exe'))
failed = False

with tempfile.TemporaryDirectory(prefix='crsf-guards-') as work:
    work = Path(work)
    (work / 'Arduino.h').write_text('#include <cstdint>\n#include <cstddef>\n')
    (work / 'IPAddress.h').write_text('#pragma once\nclass IPAddress {};\n')
    (work / 'WiFiClient.h').write_text('#pragma once\nclass WiFiClient {};\n')
    for header in ('crsf_input.h', 'crsf_main.h', 'crsf_mqtt.h'):
        source = f'#include "src/CRSF/{header}"\n' * 2
        result = subprocess.run([COMPILER, '-std=gnu++11', '-fmax-errors=1', '-I' + str(work), '-I' + str(ROOT / 'src'),
                                 '-x', 'c++', '-fsyntax-only', '-'], input=source, capture_output=True, text=True)
        assert result.returncode == 0, f'{header} must be empty when disabled:\n{result.stderr}'
    for enabled in (False, True):
        flags = [COMPILER, '-std=gnu++11', '-fmax-errors=1', '-I' + str(work), '-I' + str(ROOT / 'src')]
        if enabled:
            flags.append('-DHAVE_CRSF')
        for header in ('crsf_settings.h', 'crsf_kludge.h'):
            source = f'#include "src/CRSF/{header}"\n' * 2
            result = subprocess.run(flags + ['-x', 'c++', '-fsyntax-only', '-'],
                                    input=source, capture_output=True, text=True)
            if result.returncode:
                print(f'{header}, HAVE_CRSF={enabled}:\n{result.stderr}')
                failed = True

        obj = str(Path(work) / f'input-model-{enabled}.o')
        subprocess.run(flags + ['-c', str(ROOT / 'src/src/CRSF/elrs_input_model.cpp'),
                                '-o', obj], check=True)
        symbols = subprocess.check_output([NM, '-C', '-g', '--defined-only', obj], text=True)
        if enabled:
            assert 'elrsInputModelApplyExpo' in symbols, 'Enabled input model must compile'
        elif symbols.strip():
            print('Disabled input model still defines functions:\n' + symbols)
            failed = True

assert not failed, 'CRSF feature guard checks failed'
print('CRSF headers compile twice with feature on/off; disabled input model exports no functions')
print('Disabled CRSF input, main, and MQTT headers export no code')
