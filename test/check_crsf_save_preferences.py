"""Protect saved radio preferences when the CRSF settings form omits legacy fields."""
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
WIFI = (ROOT / "src/src/CRSF/crsf_wifi.h").read_text(encoding="utf-8")
KLUDGE = (ROOT / "src/src/CRSF/crsf_kludge.cpp").read_text(encoding="utf-8")


def function(source, signature):
    match = re.search(signature, source)
    if not match:
        raise AssertionError(f"missing function matching {signature}")
    opening = source.index("{", match.start())
    depth = 0
    for offset in range(opening, len(source)):
        if source[offset] == "{":
            depth += 1
        elif source[offset] == "}":
            depth -= 1
            if not depth:
                return source[match.start():offset + 1]
    raise AssertionError(f"unterminated function matching {signature}")


class CRSFSavePreferenceTests(unittest.TestCase):
    def test_top_level_radio_ratio_and_power_controls_are_removed(self):
        params = function(WIFI, r"static bool crsf_wifi_saveParamsCallback\(\)")
        rendered = WIFI[WIFI.index("WiFiManagerParameter * const crsfParmArray[]"):WIFI.index("NULL", WIFI.index("WiFiManagerParameter * const crsfParmArray[]"))]
        for name in ("custom_crsftlm", "custom_crsfpwr", "custom_crsfdyn"):
            self.assertNotIn(name, rendered)
        self.assertIn('custom_crsfpr', rendered)
        self.assertIn('custom_crsfsu', rendered)
        self.assertIn('"ctlmr","cmpwr","cdynp"', params)

    def test_omitted_legacy_controls_do_not_overwrite_versioned_radio_values(self):
        parser = function(WIFI, r"static bool crsf_wifi_saveParamsCallback\(\)")
        versioned = parser.index("if(remoteRadioVersioned)")
        legacy = parser.index("if(!remoteRadioVersioned)")
        self.assertLess(versioned, legacy)
        guard = parser[versioned:legacy]
        self.assertIn('wm.server->hasArg(names[i])', guard)
        self.assertIn('wm.server->arg(names[i])!=values[i]', guard)
        self.assertIn('wm.setParamSaveError(409);return false', guard)
        for name, setting in (("ctlmr", "elrsTlmRatio"), ("cmpwr", "elrsMaxPower"), ("cdynp", "elrsDynPower")):
            self.assertRegex(parser[legacy:], rf'hasArg\("{name}"\)\)getServerParam\("{name}", settings\.{setting}')

    def test_snapshot_keeps_saved_profile_id_even_when_legacy_preferences_seed_values(self):
        snapshot = function(KLUDGE, r"ELRSPortalPreferences snapshotELRSPortalPreferences\(\)")
        self.assertIn("remoteRadioVersioned?remoteRadioPreferences", snapshot)
        self.assertIn("p.radio.modelId=remoteRadioPreferences.modelId", snapshot)
        self.assertLess(snapshot.index("p.radio=remoteRadioVersioned?"), snapshot.index("p.radio.modelId=remoteRadioPreferences.modelId"))

    def test_settings_save_passes_the_unchanged_snapshot_into_checked_single_file_save(self):
        save = function(WIFI, r"static bool crsf_wifi_loop_settings\(\)")
        self.assertIn("snapshotELRSPortalPreferences()", save)
        self.assertIn("saveELRSSettings(candidate,preferences)", save)
        self.assertIn("wm.setParamSaveError(500)", save)
        self.assertIn("syncCRSFPortalBuffers()", save)


if __name__ == "__main__":
    unittest.main()
