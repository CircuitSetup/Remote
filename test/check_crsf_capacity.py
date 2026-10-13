"""Report CRSF feature flash and static-RAM acceptance evidence."""
import argparse
import gzip
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

OTA_PARTITION_BYTES = 1_310_720
OTA_RESERVE_BYTES = 16_384
OTA_IMAGE_LIMIT = OTA_PARTITION_BYTES - OTA_RESERVE_BYTES
STATIC_RAM_BASELINE = 73_580
STATIC_RAM_DELTA_LIMIT = 8_192
STATIC_RAM_LIMIT = STATIC_RAM_BASELINE + STATIC_RAM_DELTA_LIMIT
PORTAL_ASSET_LIMIT = 8_192
ROOT = Path(__file__).resolve().parents[1]
PROGRAM_SECTIONS = ('.iram0.vectors', '.iram0.text', '.dram0.data', '.flash.text', '.flash.rodata')


def size_tool():
    found = shutil.which('xtensa-esp32-elf-size') or shutil.which('xtensa-esp32-elf-size.exe')
    if found:
        return found
    core = Path(os.environ.get('PLATFORMIO_CORE_DIR', str(Path.home() / '.platformio')))
    return str(core / 'packages/toolchain-xtensa-esp32/bin' / ('xtensa-esp32-elf-size.exe' if os.name == 'nt' else 'xtensa-esp32-elf-size'))


def sections(elf, tool=None):
    output = subprocess.check_output([tool or size_tool(), '-A', str(elf)], text=True)
    result = {}
    for line in output.splitlines():
        fields = line.split()
        if len(fields) == 3 and fields[0].startswith('.'):
            try:
                result[fields[0]] = int(fields[1], 0)
            except ValueError:
                continue
    if not result:
        raise ValueError(f'No ELF sections found in {elf}')
    return result


def elf_metrics(elf, tool=None):
    values = sections(elf, tool)
    missing = [name for name in ('.dram0.data', '.dram0.bss') if name not in values]
    if missing:
        raise ValueError(f'{elf} is missing required sections: {", ".join(missing)}')
    return {
        'program_bytes': sum(values.get(name, 0) for name in PROGRAM_SECTIONS),
        'static_ram_bytes': values['.dram0.data'] + values['.dram0.bss'],
        'sections': values,
    }


def asset_symbols(map_path):
    """Count explicitly named portal asset symbols from the link map when present."""
    if not map_path:
        return None, []
    # GNU ld maps contain symbol address and size. Only named portal data symbols
    # are counted; code symbols and a guessed aggregate are never reported as assets.
    rows = []
    import re
    pattern = re.compile(r'^\s*0x[0-9a-fA-F]+\s+0x([0-9a-fA-F]+)\s+([^\s]+)')
    for line in Path(map_path).read_text(errors='replace').splitlines():
        match = pattern.match(line)
        if match and re.search(r'(portal|remote.?control).*(html|asset|js)|(html|asset|js).*(portal|remote.?control)', match.group(2), re.I):
            rows.append({'symbol': match.group(2), 'bytes': int(match.group(1), 16)})
    # Link maps may repeat a symbol in section summaries; deduplicate by name.
    unique = {row['symbol']: row for row in rows}
    return sum(row['bytes'] for row in unique.values()), list(unique.values())


def asset_header_bytes(header_path):
    """Count byte literals in the generated PROGMEM asset initializer."""
    import re
    source = Path(header_path).read_text(errors='replace')
    match = re.search(r'\[[^\]]*\]\s*(?:PROGMEM\s*)?=\s*\{([^}]*)\}', source, re.S)
    if not match:
        raise ValueError(f'No initialized byte array found in {header_path}')
    literals = re.findall(r'(?<![\w])(?:0[xX][0-9a-fA-F]+|\d+)(?![\w])', match.group(1))
    if not literals:
        raise ValueError(f'No byte values found in generated asset array {header_path}')
    data = bytes(int(value, 16) if value.lower().startswith('0x') else int(value)
                 for value in literals)
    try:
        uncompressed_bytes = len(gzip.decompress(data))
    except (OSError, EOFError) as error:
        raise ValueError(f'Invalid gzip portal asset in {header_path}: {error}') from error
    return len(data), uncompressed_bytes


def ota_gate(image_size):
    return {'pass': image_size <= OTA_IMAGE_LIMIT,
            'headroom_bytes': OTA_PARTITION_BYTES - image_size}


def build_report(args):
    ota_limit = getattr(args, 'ota_image_limit', OTA_IMAGE_LIMIT)
    ram_limit = getattr(args, 'static_ram_limit', STATIC_RAM_LIMIT)
    asset_limit = getattr(args, 'portal_asset_limit', PORTAL_ASSET_LIMIT)
    report = {
        'budgets': {
            'ota_partition_bytes': OTA_PARTITION_BYTES,
            'ota_reserve_bytes': OTA_RESERVE_BYTES,
            'ota_image_limit_bytes': ota_limit,
            'static_ram_baseline_bytes': STATIC_RAM_BASELINE,
            'static_ram_delta_limit_bytes': STATIC_RAM_DELTA_LIMIT,
            'static_ram_limit_bytes': ram_limit,
            'compiled_portal_asset_limit_bytes': asset_limit,
        },
        'evidence': {'candidate_bin': str(args.candidate_bin) if args.candidate_bin else None,
                     'candidate_elf': str(args.candidate_elf) if args.candidate_elf else None,
                     'candidate_map': str(args.map_file) if args.map_file else None},
        'gates': {},
    }
    if args.candidate_bin:
        size = Path(args.candidate_bin).stat().st_size
        report['candidate_image_bytes'] = size
        report['gates']['ota_image'] = {'pass': size <= ota_limit,
                                        'headroom_bytes': OTA_PARTITION_BYTES - size}
    else:
        report['gates']['ota_image'] = {'pass': False, 'status': 'actual .bin not supplied'}
    if args.candidate_elf:
        candidate = elf_metrics(args.candidate_elf)
        report['candidate_elf_metrics'] = candidate
        report['gates']['static_ram'] = {'pass': candidate['static_ram_bytes'] <= ram_limit,
                                         'limit_bytes': ram_limit}
        if args.baseline_elf:
            baseline = elf_metrics(args.baseline_elf)
            report['baseline_elf_metrics'] = baseline
            report['static_ram_delta_bytes'] = candidate['static_ram_bytes'] - baseline['static_ram_bytes']
    else:
        report['gates']['static_ram'] = {'pass': False, 'status': 'candidate ELF not supplied'}
    if args.baseline_bin:
        report['baseline_image_bytes'] = Path(args.baseline_bin).stat().st_size
        if args.candidate_bin:
            report['image_delta_bytes'] = report['candidate_image_bytes'] - report['baseline_image_bytes']
    asset_path = getattr(args, 'asset_header', None)
    source_paths = getattr(args, 'asset_sources', None) or [
        path for path in (ROOT / 'src/src/CRSF/crsf_config.html', ROOT / 'src/src/CRSF/crsf_config.js') if path.exists()]
    if asset_path:
        count, uncompressed_bytes = asset_header_bytes(asset_path)
        symbols = []
        asset_status = 'generated byte-array length'
    elif args.map_file:
        count, symbols = asset_symbols(args.map_file)
        uncompressed_bytes = None
        asset_status = 'link-map symbols' if count is not None else 'symbols not found'
    else:
        count, symbols, asset_status = None, [], 'generated header or link map not supplied'
        uncompressed_bytes = None
    report['compiled_portal_assets'] = {'bytes': count, 'uncompressed_bytes': uncompressed_bytes,
                                       'symbols': symbols, 'status': asset_status,
                                       'limit_bytes': asset_limit}
    report['asset_sources'] = [{'path': str(path), 'bytes': Path(path).stat().st_size}
                               for path in source_paths]
    report['gates']['compiled_portal_assets'] = {
        'pass': count is not None and count <= asset_limit,
        'limit_bytes': asset_limit,
    }
    report['evidence']['baseline_bin'] = str(args.baseline_bin) if args.baseline_bin else None
    report['evidence']['baseline_elf'] = str(args.baseline_elf) if args.baseline_elf else None
    return report


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--candidate-bin', type=Path)
    parser.add_argument('--candidate-elf', type=Path)
    parser.add_argument('--baseline-bin', type=Path)
    parser.add_argument('--baseline-elf', type=Path)
    parser.add_argument('--map', dest='map_file', type=Path)
    parser.add_argument('--asset-header', type=Path,
                        help='generated header containing the compressed PROGMEM asset byte array')
    parser.add_argument('--asset-source', dest='asset_sources', type=Path, action='append', default=[],
                        help='source HTML/JS file; repeat to report each file size separately')
    parser.add_argument('--ota-image-limit', type=int, default=OTA_IMAGE_LIMIT)
    parser.add_argument('--static-ram-limit', type=int, default=STATIC_RAM_LIMIT)
    parser.add_argument('--portal-asset-limit', type=int, default=PORTAL_ASSET_LIMIT)
    parser.add_argument('--output', type=Path, help='also write the JSON report to this path')
    args = parser.parse_args(argv)
    if not args.candidate_bin and not args.candidate_elf:
        parser.error('supply --candidate-bin and/or --candidate-elf')
    report = build_report(args)
    rendered = json.dumps(report, indent=2, sort_keys=True)
    print(rendered)
    if args.output:
        args.output.write_text(rendered + '\n')
    return 0 if all(gate.get('pass', False) for gate in report['gates'].values()) else 1


if __name__ == '__main__':
    sys.exit(main())
