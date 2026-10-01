# CRSF Review Fixes and Shared Channel Routing Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fix the confirmed P1/P2 review findings and allow switches and gimbals to share the CH1–CH16 destination range.

**Architecture:** Keep the existing input model, portal builders, binary settings format, and CRSF scheduler. Validate submitted input settings before mutation, normalize corrupt stored routing as a pair, and restore main settings after a rejected ELRS save.

**Tech Stack:** ESP32 Arduino C++, PlatformIO, existing Unity native tests and Python production-code harnesses.

**Spec:** User requests in this conversation; independent review of `17b6c2b..d586f3a` found one P1 and three P2 issues.

## Global Constraints

- Preserve valid saved profiles and routing, descending/reversed calibration, and normal 1000/1500/2000 µs travel.
- ADC faults and self-test must send 1500 µs neutral on every configured gimbal destination.
- All 16 inputs must have different destinations within CH1–CH16. Keep existing defaults and settings blob size.
- Preserve the user's unrelated `platformio.ini` edits; add no dependencies or servo travel controls.
- Firmware must fit the existing 1,310,720-byte application partition.

## Review Focus

- Neutral fallback with throttle remapped to CH2.
- Return to neutral from either side with narrow, descending, and reversed profiles.
- Invalid calibration and duplicate routing must preserve previous settings and permit a corrected retry.
- A rejected ELRS save followed by an unrelated save must not persist rejected values.
- Every switch must work on CH1–CH16 without overwriting gimbals; explain ELRS RF channel-resolution limits.

### Task 1: Safe ADC fallback and reliable neutral entry

**Files:** `src/src/CRSF/elrs_crsf_core.cpp`, `src/src/CRSF/elrs_crsf_core.h`, `test/native_elrs/test_native_elrs.cpp`, `test/check_crsf_adc.py`.

- [x] Add failing neutral assertions for missing/stale ADC and self-test, including throttle on CH2; add narrow-profile return-to-center checks.
- [x] Run native tests and the ADC harness; confirm failures are the reviewed output errors.
- [x] Use neutral directly for safe gimbal outputs; allow entry into mapped neutral to bypass hysteresis while retaining the hold once neutral.
- [x] Run native tests and `python test/check_crsf_adc.py`; confirm normal endpoints and noise suppression still pass (95 native tests).

### Task 2: Reject invalid input settings atomically

**Files:** `src/src/CRSF/crsf_kludge.cpp`, `src/remote_wifi.cpp`, `test/check_crsf_settings.py`.

- [x] Add failing actual-callback checks for invalid profiles/routing, rejected main-settings changes, and corrected retries.
- [x] Run `python test/check_crsf_settings.py` to verify the failures.
- [x] Validate submitted profiles and routing before mutation; restore the main settings snapshot on ELRS save failure.
- [x] Run the settings harness; confirm migration, failure rollback, and successful saving still pass.

### Task 3: Shared CH1–CH16 routing

**Files:** `src/src/CRSF/elrs_input_model.cpp`, `src/src/CRSF/elrs_input_model.h`, `src/src/CRSF/elrs_crsf.cpp`, `src/src/CRSF/elrs_crsf_core.cpp`, `src/src/CRSF/crsf_kludge.cpp`, `src/src/CRSF/crsf_wifi.h`, `README.md`, existing native/settings tests.

**Interfaces:** Add `elrsIsValidInputRouting(const ELRSGimbalRouting &, const ELRSSwitchRouting &)` and `elrsSanitizeInputRouting(ELRSGimbalRouting &, ELRSSwitchRouting &)`. Use them at persistence and core initialization boundaries.

- [x] Add failing checks for all 16 destination rotations, mixed-map persistence, cross-input duplicates, and rendered CH1–CH16 selectors.
- [x] Run native/settings checks and confirm old range restrictions cause the failures.
- [x] Extend both destination ranges, validate the combined assignment, expand portal selectors and client validation, and update mapping documentation.
- [x] Run all host checks, review the final patch, build `esp32dev`, check partition size and `git diff --check`, then commit only intended changes.

### Final review fix pass

The fresh final review confirmed two further P2 cases. Physical ADC/ESC/RF behavior and shared-storage torn writes remain outside host verification.

**Files:** `src/src/CRSF/elrs_crsf_shared.h`, `src/src/CRSF/crsf_wifi.h`, existing ADC/native/settings harnesses.

- [x] Reproduce the integer IIR stalling short of center through the actual adapter, core, and packed UART frame; cover both directions and reversal.
- [x] Ensure the IIR converges on steady samples while the core retains the configured jitter hold. Native and ADC checks pass.
- [x] Reproduce blank/malformed calibration acceptance through the actual POST parser and persistence callback.
- [x] Parse complete calibration and gimbal-channel text without truncation or valid-default substitution; require calibration fields in the form. Settings and rendered client checks pass, including rejection and a corrected CH16 swap.

**Final verification:** 96 native tests passed using the existing MinGW runner; `python test/check_crsf_adc.py`, `python test/check_crsf_settings.py`, and `node test/check_crsf_routing.js .pio/review/channel-map.html` passed. Export the actual portal HTML with `CRSF_SWITCH_PREVIEW` when running the settings harness before the Node check. `rtk pio run -e esp32dev` succeeded: firmware binary 1,278,608 bytes, partition 1,310,720 bytes, RAM 73,144 bytes. No hardware flash or physical ESC verification was performed for this request.
