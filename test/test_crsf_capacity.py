import gzip
from argparse import Namespace
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

import check_crsf_capacity as capacity


class CapacityReportTests(unittest.TestCase):
    @patch('check_crsf_capacity.shutil.which', return_value=None)
    def test_size_tool_respects_configured_platformio_runtime(self, which):
        with patch.dict('os.environ', {'PLATFORMIO_CORE_DIR': 'D:/platformio-runtime'}):
            self.assertIn('D:/platformio-runtime', capacity.size_tool().replace('\\', '/'))

    def test_portal_asset_budget_matches_approved_ledger_limit(self):
        self.assertEqual(capacity.PORTAL_ASSET_LIMIT, 8192)

    def test_default_portal_sources_follow_crsf_owned_ui(self):
        report = capacity.build_report(Namespace(candidate_bin=None, candidate_elf=None,
                                                  baseline_bin=None, baseline_elf=None,
                                                  map_file=None, asset_header=None,
                                                  asset_sources=None))
        self.assertEqual([Path(row['path']).name for row in report['asset_sources']],
                         ['crsf_config.html', 'crsf_config.js'])

    def test_actual_bin_limit_includes_exact_reserve(self):
        self.assertTrue(capacity.ota_gate(capacity.OTA_IMAGE_LIMIT)['pass'])
        self.assertFalse(capacity.ota_gate(capacity.OTA_IMAGE_LIMIT + 1)['pass'])

    @patch('check_crsf_capacity.subprocess.check_output')
    def test_section_measurement_reports_program_and_static_ram(self, output):
        output.return_value = ('.iram0.vectors 100 0x0\n.iram0.text 200 0x64\n'
                               '.dram0.data 30 0x12c\n.dram0.bss 70 0x14a\n'
                               '.flash.text 400 0x190\n.flash.rodata 50 0x320\n')
        measured = capacity.elf_metrics('candidate.elf', 'size')
        self.assertEqual(measured['program_bytes'], 780)
        self.assertEqual(measured['static_ram_bytes'], 100)

    def test_map_counts_named_portal_data_and_deduplicates(self):
        with tempfile.TemporaryDirectory() as directory:
            link_map = Path(directory) / 'candidate.map'
            link_map.write_text('  0x3f000000 0x00000400 remote_control_html\n'
                                '  0x3f000000 0x00000400 remote_control_html\n'
                                '  0x40000000 0x00000100 portalHandler\n')
            count, symbols = capacity.asset_symbols(link_map)
            self.assertEqual(count, 1024)
            self.assertEqual(len(symbols), 1)

    def test_generated_asset_header_counts_compressed_byte_array(self):
        with tempfile.TemporaryDirectory() as directory:
            header = Path(directory) / 'ui.h'
            payload = gzip.compress(b'<html/>', mtime=0)
            values = ', '.join(f'0x{byte:02x}' for byte in payload)
            header.write_text(f'static const uint8_t remoteConfigUiGzip[] PROGMEM = {{ {values} }};\n'
                              'static constexpr size_t remoteConfigUiGzipLength = sizeof(remoteConfigUiGzip);\n')
            self.assertEqual(capacity.asset_header_bytes(header), (len(payload), 7))


if __name__ == '__main__':
    unittest.main()
