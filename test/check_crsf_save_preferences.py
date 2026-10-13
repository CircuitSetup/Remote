"""Regression coverage for preference-safe ordinary CRSF portal saves."""

import re
import unittest

from check_crsf_adc import ROOT, compile_and_run


def production_function(path, expression):
    source = (ROOT / path).read_text()
    match = re.search(expression, source)
    if not match:
        raise AssertionError(f"production function not found: {expression}")
    opening = source.index("{", match.start())
    depth = 0
    for offset in range(opening, len(source)):
        if source[offset] == "{":
            depth += 1
        elif source[offset] == "}":
            depth -= 1
            if depth == 0:
                return source[match.start():offset + 1] + "\n"
    raise AssertionError(f"unterminated production function: {expression}")


request_update = production_function(
    "src/src/CRSF/crsf_kludge.cpp",
    r"void requestELRSModuleConfigUpdate\(uint8_t telemetryRatio, uint8_t maxPower, uint8_t dynamicPower\)\s*\{",
)
portal_parser = production_function(
    "src/src/CRSF/crsf_wifi.h", r"static (?:bool|void) crsf_wifi_saveParamsCallback\(\)\s*\{"
)
legacy_conversion = production_function(
    "src/src/CRSF/elrs_device_config.cpp",
    r"ELRSRadioPreferences elrsRadioPreferencesFromLegacy\(uint8_t telemetry, uint8_t power, uint8_t dynamic\)\s*\{",
)

FIXTURE = r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include "src/CRSF/elrs_crsf_shared.h"

struct Settings {
    char crsfap[2] = "0";
    char elrsRollRev[2] = "0", elrsPitchRev[2] = "0", elrsThrRev[2] = "0", elrsYawRev[2] = "0";
    char elrsTlmRatio[16] = "0", elrsMaxPower[16] = "0", elrsDynPower[16] = "0";
    char opMode[16] = "0", elrsPktRate[16] = "0", elrsSpdUnit[16] = "0";
    char elrsRollCh[16] = "1", elrsPitchCh[16] = "2", elrsThrCh[16] = "3", elrsYawCh[16] = "4";
    char elrsAdcHysteresis[16] = "5", elrsThrIdleDeadband[16] = "5";
    char elrsRollLow[16] = "0", elrsRollCtr[16] = "1024", elrsRollHigh[16] = "2047";
    char elrsPitchLow[16] = "0", elrsPitchCtr[16] = "1024", elrsPitchHigh[16] = "2047";
    char elrsThrLow[16] = "0", elrsThrCtr[16] = "1024", elrsThrHigh[16] = "2047";
    char elrsYawLow[16] = "0", elrsYawCtr[16] = "1024", elrsYawHigh[16] = "2047";
} settings;

bool opModeCRSF = true;
bool remoteRadioVersioned = false;
ELRSRadioPreferences remoteRadioPreferences;
int preferenceUpdateCalls = 0, parserReadCalls = 0;
struct TestMode {
    void requestModuleConfigUpdate(uint8_t, uint8_t, uint8_t) { preferenceUpdateCalls++; }
    void setRadioPreferences(const ELRSRadioPreferences &, bool) { preferenceUpdateCalls++; }
} elrsMode;
bool remoteRadioReviewPending() { return false; }
struct { void println(const char *) {} } Serial;

void getServerParam(const char *name, char *dest, size_t length, int, int, int defaultValue);
void crsfReadGimbalChannelParam(const char *, char *) { parserReadCalls++; }
void crsfReadSwitchParams() { parserReadCalls++; }
void crsfReadOutputLimitParams() { parserReadCalls++; }
void crsfReadExpoParams() { parserReadCalls++; }
void crsfReadDisplayParams() { parserReadCalls++; }
void crsfReadLogicParams() { parserReadCalls++; }
void crsfReadMixParams() { parserReadCalls++; }
void crsfReadInputParam(const char *, char *, size_t, int, int, int) { parserReadCalls++; }
const int DEF_ELRSPKTRATE = 3, DEF_ELRSSPDUNIT = 0, DEF_ELRSTLMRATIO = 0;
const int DEF_ELRSMAXPOWER = 3, DEF_ELRSDYNPWR = 0, ELRS_INPUT_TOLERANCE_MAX = 50;
std::map<std::string, std::string> posted;
struct TestServer {
    bool hasArg(const char *name) const { return posted.count(name) != 0; }
    std::string arg(const char *name) const {
        auto value = posted.find(name);
        return value == posted.end() ? std::string() : value->second;
    }
};
TestServer testServer;
struct { TestServer *server = &testServer; } wm;
void getServerParam(const char *name, char *dest, size_t length, int, int, int defaultValue) {
    parserReadCalls++;
    auto value = posted.find(name);
    snprintf(dest, length + 1, "%s", value == posted.end() ? std::to_string(defaultValue).c_str() : value->second.c_str());
}
'''

def preference(model_id=12):
    return f'''
static ELRSRadioPreferences seededPreferences(bool composite = false) {{
    ELRSRadioPreferences p;
    p.modelId = {model_id};
    snprintf(p.telemetry, sizeof(p.telemetry), "1:8");
    snprintf(p.maxPower.label, sizeof(p.maxPower.label), "%s", composite ? "25/1000" : "1000");
    snprintf(p.maxPower.unit, sizeof(p.maxPower.unit), "mW");
    snprintf(p.dynamic, sizeof(p.dynamic), "AUX9");
    return p;
}}
static bool samePreferences(const ELRSRadioPreferences &a, const ELRSRadioPreferences &b) {{
    return a.modelId == b.modelId && !strcmp(a.telemetry, b.telemetry) &&
        !strcmp(a.maxPower.label, b.maxPower.label) && !strcmp(a.maxPower.unit, b.maxPower.unit) &&
        !strcmp(a.dynamic, b.dynamic);
}}
'''


def harness(body, include_parser=False):
    source = "#include <stdint.h>\n" + FIXTURE + preference()
    source += legacy_conversion + request_update
    if include_parser:
        source += portal_parser
    source += "\nint main() {\n" + body + "\n}\n"
    return source


class CRSFSavePreferenceTests(unittest.TestCase):
    def test_local_save_preserves_versioned_radio(self):
        # An old form's legacy values cannot overwrite an authoritative semantic record.
        compile_and_run(harness(r'''
    const auto original = seededPreferences();
    remoteRadioPreferences = original; remoteRadioVersioned = true;
    requestELRSModuleConfigUpdate(0, 3, 0);
    assert(preferenceUpdateCalls == 0);
    assert(remoteRadioVersioned);
    assert(samePreferences(remoteRadioPreferences, original));
'''), [])
        compile_and_run(harness(r'''
    const auto original = seededPreferences(true);
    remoteRadioPreferences = original; remoteRadioVersioned = true;
    requestELRSModuleConfigUpdate(0, 3, 0);
    assert(preferenceUpdateCalls == 0);
    assert(samePreferences(remoteRadioPreferences, original));
'''), [])

    def test_legacy_edit_preserves_model_id(self):
        compile_and_run(harness(r'''
    remoteRadioPreferences.modelId = 12;
    remoteRadioVersioned = false;
    requestELRSModuleConfigUpdate(5, 5, 1);
    assert(preferenceUpdateCalls == 1);
    assert(remoteRadioPreferences.modelId == 12);
    auto expected = elrsRadioPreferencesFromLegacy(5, 5, 1);
    expected.modelId = 12;
    assert(samePreferences(remoteRadioPreferences, expected));
'''), [])

    def test_old_form_conflicting_rf_edit_is_rejected_before_persistence(self):
        function_signature = re.search(r"static (bool|void) crsf_wifi_saveParamsCallback\(\)", portal_parser).group(1)
        self.assertEqual(function_signature, "bool", "CRSF form parser must return rejection status")
        compile_and_run(harness(r'''
    remoteRadioPreferences = seededPreferences(true); remoteRadioVersioned = true;
    posted["ctlmr"] = "5";  // Old form changes the saved telemetry selection.
    posted["cmpwr"] = "3";
    posted["cdynp"] = "0";
    assert(!crsf_wifi_saveParamsCallback());
    assert(preferenceUpdateCalls == 0);
    assert(parserReadCalls == 0); // Reject before reading or applying any form fields.
    assert(remoteRadioVersioned);
    assert(remoteRadioPreferences.modelId == 12);
''', include_parser=True), [])

    def test_omitted_unversioned_legacy_radio_fields_preserve_saved_values(self):
        compile_and_run(harness(r'''
    remoteRadioVersioned = false;
    strcpy(settings.elrsTlmRatio, "5");
    strcpy(settings.elrsMaxPower, "4");
    strcpy(settings.elrsDynPower, "1");
    posted.clear();
    assert(crsf_wifi_saveParamsCallback());
    assert(!strcmp(settings.elrsTlmRatio, "5"));
    assert(!strcmp(settings.elrsMaxPower, "4"));
    assert(!strcmp(settings.elrsDynPower, "1"));
    assert(preferenceUpdateCalls == 0);
    posted["ctlmr"] = "2"; posted["cmpwr"] = "3"; posted["cdynp"] = "0";
    assert(crsf_wifi_saveParamsCallback());
    assert(!strcmp(settings.elrsTlmRatio, "2"));
    assert(!strcmp(settings.elrsMaxPower, "3"));
    assert(!strcmp(settings.elrsDynPower, "0"));
''', include_parser=True), [])

    def test_unchanged_or_omitted_legacy_radio_fields_are_accepted(self):
        compile_and_run(harness(r'''
    const auto original = seededPreferences(true);
    remoteRadioPreferences = original; remoteRadioVersioned = true;
    posted.clear();
    assert(crsf_wifi_saveParamsCallback());
    assert(samePreferences(remoteRadioPreferences, original));
    posted["ctlmr"] = "0"; posted["cmpwr"] = "0"; posted["cdynp"] = "0";
    assert(crsf_wifi_saveParamsCallback());
    assert(samePreferences(remoteRadioPreferences, original));
    assert(remoteRadioVersioned);
''', include_parser=True), [])


if __name__ == "__main__":
    unittest.main()
