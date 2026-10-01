# ELRS Switch Routing Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox syntax for tracking.

**Goal:** Configure the outgoing channels for Stop, FakePower, O.O, RESET, and ButtonPack 1-8 in the ELRS portal without exceeding the existing firmware partition.

**Architecture:** Add a 12-byte switch routing value to the existing input model, runtime configuration, and saved blob. Replace the portal's fixed switch list with one generated block of selectors. Reuse the current channel writer and settings callbacks; add no dependencies.

**Tech Stack:** ESP32 Arduino C++11, existing Unity native tests, Python hardware/settings harnesses.

**Spec:** The user's request to plan and execute configurable switch mappings, following the compact implementation discussed in this chat. The bounded design presented here preserves gimbal channels CH1-CH4 and assigns each switch a unique channel in CH5-CH16.

## Global Constraints

- Continue `fix/crsf-filtered-calibration` from `9e84bf7`; preserve unrelated `platformio.ini` edits. Execute inline, then one independent final review.
- Input order: Stop, FakePower, O.O, RESET, then ButtonPack 1-8. Default channels: 5 through 16 in that order.
- Each switch channel must be 5-16 and unique. Reject invalid portal/save mappings before altering input settings; corrupted runtime or stored mappings revert as a whole to defaults.
- Keep gimbals on CH1-CH4, switch polarity/local effects, ADC filtering/calibration, display telemetry, and existing failsafe behavior. Self-test must assert the configured Stop channel.
- Append exactly 12 mapping bytes to `/crsfcfg`. Old and partially written new mappings keep default switch routing while preserving complete older calibration and tolerance fields. Existing calibration saves preserve mappings.
- One portal builder produces twelve labeled selectors with explicit `csw0` through `csw11` names, selected saved values, and client-side duplicate validation. Server-side validation also rejects malformed/duplicate values.
- Current firmware image baseline: 1,274,224 bytes; application partition: 1,310,720 bytes. Record the final image size and difference. No partition changes, upload, push, or merge are requested.

## Review Focus

- Swapping Stop/FakePower/ButtonPack channels preserves polarity, local effects, and ADC-safe outputs; no leftover signal remains on an old channel.
- Duplicate, out-of-range, or malformed portal assignments cannot overwrite a valid map or calibration, including requests that bypass browser validation.
- Old blobs and every partial appended map retain calibration/reversal/routing/tolerances and use a complete default switch map.
- Calibration saves and storage migration preserve a custom map; a failed persistence operation remains retryable and does not report success.
- Self-test and missing/stale ButtonPack paths address mapped channels; compact portal generation respects its allocation/length/destroy contract.

### Task 1: Route switch outputs in the core

**Files:** Modify `src/src/CRSF/elrs_input_model.{h,cpp}`, `elrs_crsf_core.{h,cpp}`, `elrs_crsf.{h,cpp}`; extend `test/native_elrs/test_native_elrs.cpp`.

**Interfaces:** Produce `ELRS_SWITCH_INPUT_COUNT = 12`, `ELRSSwitchRouting { uint8_t channels[ELRS_SWITCH_INPUT_COUNT]; }`, `elrsDefaultSwitchRouting()`, `elrsIsValidSwitchRouting(const ELRSSwitchRouting &) -> bool`, and `elrsSanitizeSwitchRouting(const ELRSSwitchRouting &)`. Add `ELRSCrsfCoreConfig.switchRouting` with default routing and an optional trailing `const ELRSSwitchRouting *switchRouting = NULL` to `ELRSCrsfMode::begin`.

- [x] Add a failing runtime regression: rotate the twelve destination channels; activate each input individually and check exactly its selected channel is 1811, the other switch channels are 172, and gimbals retain existing values. Exercise ADC fault, missing/stale ButtonPack, and self-test with Stop mapped away from CH5.
- [x] Run `.pio/review/check-inputs.ps1`. Expected: missing mapping interface or old fixed-channel assertions fail.
- [x] Implement the routing type and validation, core configuration sanitization, mapped writes including self-test, and adapter pass-through. Invalid maps use defaults; old callers remain valid.
- [x] Add duplicate/out-of-range mapping validation cases and check defaults. Run `.pio/review/check-inputs.ps1` and `python test/check_crsf_adc.py`. Expected: all tests pass.
- [x] Commit the tested runtime change, excluding `platformio.ini`.

### Task 2: Persist and expose switch mappings

**Files:** Modify `src/src/CRSF/crsf_kludge.cpp`, `crsf_settings.h`, `crsf_wifi.h`, `src/remote_settings.h`, `README.md`; extend `test/check_crsf_settings.py`.

**Interfaces:** Consume Task 1's type, helpers, and adapter argument. Extend `loadELRSInputConfig` with an optional trailing `ELRSSwitchRouting *switchRouting = NULL`, and `saveELRSInputConfig` with optional `const ELRSSwitchRouting *switchRouting = NULL`. Existing callers preserve mappings. Append routing to the binary blob and add `settings.elrsSwitchCh[12][3]` portal buffers.

- [x] Add failing behavioral tests using actual persistence/callback/builder code: old 56-byte blob defaults, all partial mappings, custom routing round trip, calibration preservation, duplicate/range/malformed save rejection, failed write/retry, and rendered labels/names/selected values/CH5-CH16 limits. Adjust the existing malformed-tolerance case to target its field offset rather than the new blob end.
- [x] Run `python test/check_crsf_settings.py`. Expected: tests fail because mappings are absent.
- [x] Append/migrate the mapping, validate before writes, load it into `crsf_begin`, and implement the twelve selectors inside the existing ELRS section with client and server validation. Refresh saved values on portal open; document defaults, unique assignments, restart behavior, and actual gimbal limits.
- [x] Run `.pio/review/check-inputs.ps1`, `python test/check_crsf_adc.py`, `python test/check_crsf_settings.py`, and `git diff --check`. Expected: all pass. Run `rtk pio run -e esp32dev`; expected: SUCCESS and `firmware.bin` below 1,310,720 bytes. Record exact size delta.
- [ ] Commit the portal/persistence change and plan. Run one independent whole-branch Superpowers review; address actionable P0-P2 findings with RED-to-GREEN regressions, rerun checks, and commit fixes.

## Verification Record

- 90 native tests and both Python ADC/settings harnesses pass. Migration covers legacy calibration-only blobs, previous full blobs, all partial switch mappings, and corrupt mapping fields. Failed writes retain the previous input configuration and remain retryable.
- Actual generated selectors were checked in Chrome: twelve saved assignments render correctly, selecting a duplicate gives validity errors on both affected inputs, and completing a swap clears every error.
- ESP32 build succeeds: static RAM 73,120 bytes; firmware image 1,277,120 bytes, an increase of 2,896 bytes. The existing 1,310,720-byte application partition has 33,600 bytes (32.8 KiB) free.
- No hardware upload was performed. Portal rendering and runtime host checks are verified; receiver behavior requires bench testing of this revision.
