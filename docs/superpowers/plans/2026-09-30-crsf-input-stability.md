# CRSF Input Stability Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox syntax for tracking.

**Goal:** Hold CRSF channels steady through small ADC fluctuations and keep throttle at idle near its calibrated minimum.

**Architecture:** Keep the existing quarter-step IIR ADC filter and its cached portal readings. Apply stateful hysteresis to control values in the core, while calibration captures continue using the filtered samples directly. Apply a continuous throttle idle deadband in the existing input model, then map to CRSF ticks.

**Tech Stack:** ESP32 Arduino C++11, existing Unity native tests, Python standard-library hardware/settings checks; no new dependencies.

**Spec:** The approved proposal in this chat, made concrete by the design and constraints below. The user explicitly requested writing the plan and then implementing it in this session.

## Global Constraints

- Continue `fix/crsf-filtered-calibration` from `dd4f564`; preserve the user's unrelated `platformio.ini` edits.
- Default hysteresis and throttle idle deadband: 5 ADC counts each; portal adjustment: 0-32 counts, where 0 disables that feature.
- Update a held axis only when its accumulated difference exceeds the configured hysteresis. Calibrated endpoints and entry into the throttle idle band bypass the hold, including reversed and descending profiles.
- First valid sample and recovery reseed held axes; failed ADC scans still enter the existing failsafe.
- Throttle deadband applies to the endpoint that maps to 1000 us, honors reversal, and remaps the remaining half-travel continuously to center. Limit it below the idle-to-center span so narrow valid profiles stay usable.
- Portal live readings and button captures remain the existing filtered ADC counts, without output hysteresis or idle clamping. Saving calibration preserves the new settings.
- Append settings to the existing `/crsfcfg` blob; retain the old profile/routing prefix and migrate old files to 5/5 defaults without losing calibration or reversal.
- No automatic flash/upload in this implementation task; hardware verification can follow separately.

## Review Focus

- Noise around a held value: slow deliberate movement must eventually accumulate enough to update; endpoint arrival cannot stick short of full travel.
- Ascending/descending and reversed throttle: idle always means the physical endpoint that produces 1000 us.
- A narrow valid calibration: a large configured idle band must not collapse a segment or produce half throttle.
- ADC failure and recovery, including a one-sample error: no hysteresis can hide a fault or retain stale throttle on recovery.
- Old, truncated, and malformed settings: no uninitialized smoothing fields; legacy calibration/routing and existing migration saves survive.

### Task 1: Stabilize control values and throttle idle

**Files:** Modify `src/src/CRSF/elrs_input_model.{h,cpp}`, `elrs_crsf_core.{h,cpp}`, `elrs_crsf.{h,cpp}`; test `test/native_elrs/test_native_elrs.cpp` and existing `test/check_crsf_adc.py`.

**Interfaces:** Produce `ELRSCrsfCoreConfig.adcHysteresis` and `.throttleIdleDeadband` (uint16_t, default 5), optional trailing arguments of the same names on `ELRSCrsfMode::begin`, and `elrsInputModelThrottleToUs(const ELRSInputAxisProfile &, int16_t, uint16_t) -> int16_t`. Core owns held-axis state; the adapter passes configuration through.

- [x] Write failing native tests: `test_hysteresis_holds_jitter_and_tracks_slow_motion` (1024 then offsets -4..5 keep 992 ticks; offset 6 changes), `test_hysteresis_reaches_endpoints_and_reseeds_after_error` (endpoint arrival bypasses a <=5-count delta; a valid recovery sample seeds directly), and `test_throttle_idle_band_handles_all_profile_directions` (ascending/descending, reverse on/off, the idle endpoint through five counts stays at 172 ticks; center=992 and full=1811).
- [x] Run `.pio/review/check-inputs.ps1`. Expected: the new assertions fail against current core behavior.
- [x] Implement held-axis updates after successful sampling, preserving separate filtered calibration values. Add throttle mapping by moving the idle endpoint toward center by a bounded count before the existing piecewise map.
- [x] Add regression cases for disabling each setting, configurable thresholds, and narrow valid spans with a 32-count band. Expected: disabled hysteresis follows every sample; disabled idle mapping preserves the old map; no collapsed profile.
- [x] Run `.pio/review/check-inputs.ps1` and `python test/check_crsf_adc.py`. Expected: all native tests and cached filtered ADC/failsafe checks pass.
- [x] Commit this tested runtime change, excluding `platformio.ini`.

### Task 2: Persist and expose the two adjustments

**Files:** Modify `src/src/CRSF/crsf_kludge.cpp`, `crsf_settings.h`, `crsf_wifi.h`, `src/remote_settings.h`, `README.md`; extend `test/check_crsf_settings.py`.

**Interfaces:** Consume Task 1's two trailing `begin` parameters. Extend `loadELRSInputConfig` with optional uint16_t output pointers and `saveELRSInputConfig` with optional const uint16_t pointers after routing; existing callers preserve smoothing settings. Add two uint16_t fields at the end of the blob, with old-size migration and range validation.

- [x] Add a behavioral settings harness using the actual blob/load/save functions and portal buffer callbacks. Check a current legacy blob retains calibration/reversal/routing and gets 5/5; a new save/load retains 0 and 32; invalid stored values are bounded; partial optional fields use defaults; portal save/load transfers both values and preserves unrelated profile fields.
- [x] Run `python test/check_crsf_settings.py`. Expected: the additional behavior fails because the fields/configuration interfaces do not yet exist.
- [x] Append and migrate the two blob fields, thread them into `crsf_begin`, and expose `chyst` and `cthid` number fields in the calibration portal (0-32, default 5). Validate using the existing parameter parser; refresh their displayed values from the persisted config.
- [x] Document the knobs, small-motion tradeoff, preserved endpoint semantics, and the portal's filtered calibration readings.
- [x] Run `.pio/review/check-inputs.ps1`, `python test/check_crsf_adc.py`, and `python test/check_crsf_settings.py`. Expected: all checks pass. Run `rtk pio run -e esp32dev`; expected: SUCCESS within the existing partition.
- [x] Use one independent Superpowers code reviewer on the complete patch, emphasizing Review Focus. Fix actionable findings with a failing regression first, then verify and commit the intended changes.

## Completion Evidence

- Independent review found one P1: hysteresis could retain throttle above idle when the filtered sample entered the idle band. Reproduced in the actual core (expected 172 ticks, got 179), then fixed by letting a current sample mapped to minimum throttle bypass the hold.
- `test_throttle_idle_entry_bypasses_hysteresis` covers movement into idle for ascending/descending, reversed, and narrow profiles, plus release from idle. Verified RED then GREEN; all 87 native tests pass.
- Both Python ADC/settings checks and the ESP32 build pass. Legacy calibration/routing and the portal's filtered readings are preserved.
- Final ruling: physical ADC noise, timing, and RF behavior require a hardware check of this revision. Host checks establish mapping and persistence only; leaving upload out of this task follows the authorized scope. Cost if wrong: tuning or a hardware-dependent defect may remain undetected until bench testing.
- Changes remain on the existing feature branch; this revision has not been uploaded to COM4.
