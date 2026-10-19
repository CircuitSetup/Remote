"""Exercise snapshots of current radio preferences without old numeric settings."""
from pathlib import Path
import re
from check_crsf_adc import compile_and_run

ROOT = Path(__file__).resolve().parents[1]
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


source = r'''
#include <cassert>
#include <cstring>
#include <cstdlib>
#include "crsf_settings.h"
struct {
    char opMode[2] = "2", crsfap[2] = "1", elrsPktRate[2] = "4", elrsSpdUnit[2] = "1";
} settings;
ELRSRadioPreferences remoteRadioPreferences;
ELRSPortalPreferences crsfPortalPreferences;
'''
source += function(KLUDGE, r"ELRSPortalPreferences snapshotELRSPortalPreferences\(\)")
source += r'''
int main() {
    auto p = snapshotELRSPortalPreferences();
    assert(p.opMode == 2 && p.cadence == 4 && p.speedUnits == 1);
    assert(!strcmp(p.radio.telemetry, "Std") && !strcmp(p.radio.maxPower.label, "250") && !strcmp(p.radio.dynamic, "Off"));
    remoteRadioPreferences.modelId = 12;
    strcpy(remoteRadioPreferences.telemetry, "1:64");
    strcpy(remoteRadioPreferences.maxPower.label, "25/1000");
    strcpy(remoteRadioPreferences.dynamic, "Dyn");
    crsfPortalPreferences.radioReview = 1;
    p = snapshotELRSPortalPreferences();
    assert(!memcmp(&p.radio, &remoteRadioPreferences, sizeof(p.radio)));
    assert(p.radioReview == 1);
}
'''
compile_and_run(source, [], ["-I" + str(ROOT / "src/src/CRSF")])
print("Current radio preference snapshots pass")
