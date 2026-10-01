# Gimbal Travel Limits Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox syntax for tracking.

**Goal:** Give Aileron, Elevator, Rudder, and Throttle independent lower and upper limits on their transmitted RC output.

**Architecture:** Keep ADC calibration and the existing normalized 1000/1500/2000 mapping intact. Add one output stage that scales each side of neutral to the configured endpoint, clamps the result, and then uses the existing CRSF conversion. Persist the endpoints as a new tail on the existing settings blob and expose them in the ELRS portal.

**Tech Stack:** ESP32 Arduino C++11, existing WiFiManager, existing Unity native tests and Python settings/ADC checks. No new dependencies.

**Spec:** The user's request to create a plan for upper/lower gimbal travel limits following programs such as Betaflight. The proposed bounded design below assumes software limits on transmitted output and recommends endpoint scaling. This is a plan for review; implementation has not been requested.

## Proposed behavior and references

Betaflight exposes MIN/MID/MAX servo endpoints, with minimum and maximum expressed in microseconds. Its servo code constrains the final output after mixing and again after filtering. Adopt that separation between input processing and final output limits. [Servos tab](https://betaflight.com/docs/wiki/app/servos-tab), [servo configuration](https://betaflight.com/docs/wiki/guides/current/Mixer), [servo implementation](https://github.com/betaflight/betaflight/blob/master/src/main/flight/servos.c).

EdgeTX places channel endpoints, center, and direction in an output stage before transmission to the RF module. That placement fits this transmitter. The recommended adaptation is to scale the full stick movement into the selected endpoints, keeping useful movement across the whole stick range, and then clamp for enforcement. [EdgeTX Outputs](https://manual.edgetx.org/v2.10/color-radios/model-settings/inputs-mixes-and-outputs/outputs).

A clamp alone would also enforce limits, but output would stop changing before the stick reaches its physical endpoint. Do not add a selectable scaling/clipping mode for this initial change. Do not repurpose the existing ADC Low/Center/High calibration fields as output limits.

| Setting | Default | Allowed values |
| --- | --- | --- |
| Lower limit | 1000 | 1000 through 1500 |
| Center | 1500 | Fixed, displayed read-only |
| Upper limit | 2000 | 1500 through 2000 |

Values use the project's existing RC-equivalent microsecond convention; transport remains CRSF. Lower and upper refer to numeric output values. Reversing an axis changes which physical direction reaches each limit, without swapping the configured limits. Limits equal to 1500 are allowed to suppress movement on one side; both at 1500 hold the axis at neutral.

For example, lower=1200 and upper=1800 give:

| Existing normalized output | Limited output |
| --- | --- |
| 1000 | 1200 |
| 1250 | 1350 |
| 1500 | 1500 |
| 1750 | 1650 |
| 2000 | 1800 |

The flow is filtered ADC -> existing jitter/idle handling -> calibrated/reversed normalized output -> endpoint scaling and clamp -> CRSF ticks -> mapped channel/frame. Fault and self-test paths retain the current worktree's safe neutral output of 1500/992 ticks for all four gimbals, including this remote's throttle. This feature must not introduce flight-controller throttle-cut semantics.

## Global Constraints

- Continue a suitable feature branch containing the current calibration/neutral fixes after concurrent work settles. Fetch/prune origin and reconcile the latest remote default branch before implementation. Preserve unrelated edits, including `platformio.ini`; never reset or include another task's changes in feature commits.
- Axis storage order is Aileron=0, Elevator=1, Rudder=2, Throttle=3. Routing remains CH1-CH4; endpoints follow the physical input axis when channels are remapped. Switch outputs remain unaffected.
- Defaults of 1000/2000 must preserve existing normal output exactly. Require `1000 <= minimumUs <= 1500 <= maximumUs <= 2000` on both server and runtime boundaries.
- Preserve ADC calibration, reversal, deadbands, neutral return, safe outputs, and the existing CRSF conversion. Calibration saves retain output limits. Portal changes apply through the existing successful-save/restart flow.
- Do not change the binary layout of `ELRSInputAxisProfile`. Append four 4-byte endpoint pairs after `switchRouting` in `/crsfcfg`: current complete payload 68 bytes, new complete payload 84 bytes. Preserve every preceding field and support older payloads.
- Missing or incomplete endpoint pairs use 1000/2000 defaults. Invalid complete stored/runtime pairs use the same defaults for that axis; invalid submitted pairs reject the save before any persistent state changes. Defaults do not certify a particular servo's mechanical range.
- Remain within the existing 1,310,720-byte application partition. No new dependencies, partition changes, curves/expo, model profiles, automatic movement, upload, push, or merge are part of this plan.

## Review Focus

- Asymmetric limits, descending ADC calibration, reversal, and channel remapping preserve neutral and monotonic output within the selected endpoints.
- Narrow calibration spans and jitter tolerance still return to neutral; output scaling must not change the normalized comparisons used by ADC hysteresis and throttle idle detection.
- ADC missing/stale and self-test paths preserve safe neutral output and existing Stop/switch behavior; normal output during calibration remains limited.
- Old blobs and every partial new tail preserve calibration, tolerances, and a complete custom switch map; partial endpoint pairs never combine one saved bound with one default bound.
- Malformed HTTP values, missing fields, oversized input, and write failures cannot reset valid settings, report success, or schedule a reboot; a healthy retry succeeds.

### Task 1: Add the final output stage

**Files:** Modify `src/src/CRSF/elrs_input_model.h`, `src/src/CRSF/elrs_input_model.cpp`, `src/src/CRSF/elrs_crsf_core.h`, `src/src/CRSF/elrs_crsf_core.cpp`, `src/src/CRSF/elrs_crsf.h`, and `src/src/CRSF/elrs_crsf.cpp`. Extend `test/native_elrs/test_native_elrs.cpp`.

**Interfaces:** Add `ELRSOutputLimits { uint16_t minimumUs; uint16_t maximumUs; }`, `elrsDefaultOutputLimits() -> ELRSOutputLimits`, `elrsIsValidOutputLimits(const ELRSOutputLimits &) -> bool`, `elrsSanitizeOutputLimits(const ELRSOutputLimits &) -> ELRSOutputLimits`, and `elrsApplyOutputLimits(const ELRSOutputLimits &, int16_t us) -> int16_t`. Add `ELRSCrsfCoreConfig.outputLimits[ELRS_GIMBAL_AXIS_COUNT]` and a trailing optional `const ELRSOutputLimits *outputLimits = NULL` argument to `ELRSCrsfMode::begin`; omitted limits use defaults.

- [ ] Add failing native tests `test_output_limits_scale_each_side_of_neutral` and `test_output_limits_validate_and_clamp`. Pin all five rows of the example table above; additionally assert `{1100,1800}` maps 1250 to 1300 and 1750 to 1650, out-of-range normalized inputs stop at their selected endpoint, and defaults return every integer 1000..2000 unchanged. Allow `{1500,1500}`; reject `{999,2000}`, `{1000,2001}`, `{1501,1800}`, and `{1200,1499}`.
- [ ] Run the native check below. Expected: failures from the missing new interfaces, with the existing suite green on a coherent starting snapshot.
- [ ] Implement the type/helpers using existing `mapAxisSegment` and `clampLong`. Map 1000..1500 to minimumUs..1500 and 1500..2000 to 1500..maximumUs, using the existing integer rounding. Sanitize configured limits in `ELRSCrsfCore::begin`, pass them through the adapter, and apply the helper inside `axisToTicks` immediately before `elrsInputUsToCrsfTicks`. Keep input-model mapping functions and normalized boundary detection unchanged.
- [ ] Add `test_output_limits_follow_axes_through_reverse_and_routing`: cover ascending/descending profiles and reversal on/off, assign four different endpoint pairs, permute CH1-CH4, and verify endpoint/neutral values in both `channelAt` and an emitted packed frame. Add `test_output_limits_preserve_safe_neutral_and_idle`: custom throttle limits apply in normal idle mapping, while missing/stale ADC and self-test retain 992 ticks on every mapped gimbal channel. Retain the existing narrow-profile neutral-return regression.
- [ ] Run the native check below and `python test/check_crsf_adc.py`. Expected: zero failures, unchanged switches, and default limits preserving existing frames.
- [ ] Commit only the tested runtime/model/adapter changes with `feat: add gimbal output travel limits`.

### Task 2: Persist limits and add portal controls

**Files:** Modify `src/src/CRSF/crsf_kludge.cpp`, `src/src/CRSF/crsf_settings.h`, `src/src/CRSF/crsf_wifi.h`, `src/remote_settings.h`, and `README.md`. Extend `test/check_crsf_settings.py`.

**Interfaces:** Consume Task 1's type/helpers. Append `ELRSOutputLimits outputLimits[ELRS_GIMBAL_AXIS_COUNT]` to `ELRSCrsfSettingsBlob`. Extend `loadELRSInputConfig` and `saveELRSInputConfig` with trailing optional `ELRSOutputLimits *outputLimits = NULL` and `const ELRSOutputLimits *outputLimits = NULL`, respectively; each pointer addresses four pairs regardless of the profile count. Add portal buffers `settings.elrsOutputMin[4][5]` and `settings.elrsOutputMax[4][5]`, builder `wmBuildCRSFOutputLimits(const char *dest, int op)`, and reader `crsfReadOutputLimitParams()`.

- [ ] Add failing migration/save checks to the existing behavioral harness: assert the old prefix remains 68 bytes and the new payload is 84 bytes; round-trip four distinct pairs; reload legacy calibration-only, profile/routing, tolerance, and complete switch-map payloads with default endpoints. For new-tail lengths 0..15, preserve complete four-byte pairs and default each incomplete pair while retaining all 68 preceding bytes, including a custom switch map. Corrupt a complete pair and check only that axis defaults.
- [ ] Run `python test/check_crsf_settings.py`. Expected: the new migration/interface assertions fail.
- [ ] Append the limits and update default/sanitization/load/save/boot pass-through. Decode optional fields by their own complete boundaries. In particular, change the existing partial-switch-map guard to compare against the end of `switchRouting`, not `sizeof(crsfSettings)`, so a valid 68-byte blob or partial endpoint tail cannot erase a custom switch map. Validate all submitted pairs before mutating settings; omitted optional arguments and physical calibration saves preserve existing limits and retry behavior.
- [ ] Add the four-row **Travel Limits** block to the existing ELRS portal. Each row shows the input name, labeled Lower limit and Upper limit number fields, and read-only Center 1500. Use `cout0lo`/`cout0hi` through `cout3lo`/`cout3hi`, with axis indices matching storage order; native min/max attributes enforce the ranges above. Follow the existing builder length/allocation/destroy contract. Refresh buffers on page open and from stored limits before reading each POST; absent fields retain their stored values.
- [ ] Wire the reader into `crsf_wifi_saveParamsCallback` and parse submitted bounds with checked `strtol` before narrowing. Reject empty, signed, fractional, trailing-garbage, embedded-NUL, oversized, and out-of-range values without truncating them into validity. Reuse the existing save rejection/HTTP 400/no-reboot path. Update calibration help and README text to distinguish captured ADC points from output travel limits and explain reversal, fixed neutral, defaults, and restart behavior.
- [ ] Extend the harness with actual builder/callback checks: each name/label/value/bound appears on its correct axis; missing fields retain stored limits; invalid requests preserve blob and runtime settings; rejected/failed writes report HTTP 400 without reboot and a healthy retry reports success. Verify physical calibration and Flash/SD migration retain custom endpoints, tolerances, and switch mapping. Update existing payload-size assertions for the appended 16 bytes rather than weakening migration tests.
- [ ] Run all verification commands below. Expected: all native/ADC/settings checks pass, `git diff --check` is clean, and the ESP32 build succeeds below 1,310,720 bytes. Record the actual firmware size and delta from a fresh pre-change build; visually inspect generated portal controls at desktop and phone widths.
- [ ] Commit only the verified feature changes and updated documentation with `feat: configure gimbal travel limits in ELRS portal`.

## Verification commands

The current workspace has `.pio/review/check-inputs.ps1`; it compiles and runs the native suite directly. For reproducible execution without that ignored helper, use its equivalent commands below after the existing native Unity dependency is available:

```powershell
$nativeBin = Join-Path $env:USERPROFILE '.platformio\packages\toolchain-gccmingw32\bin'
New-Item -ItemType Directory -Force '.pio\review' | Out-Null
& "$nativeBin\g++.exe" -std=gnu++11 -DHAVE_CRSF -Isrc -Isrc/src/CRSF -I.pio/libdeps/native_elrs/Unity/src src/src/CRSF/elrs_crsf_core.cpp src/src/CRSF/elrs_crsf_transport.cpp src/src/CRSF/elrs_input_model.cpp test/native_elrs/test_native_elrs.cpp .pio/libdeps/native_elrs/Unity/src/unity.c -o .pio/review/native_tests.exe
if ($LASTEXITCODE -ne 0) { throw 'Native compilation failed' }
$env:PATH = "$nativeBin;$env:PATH"
& '.\.pio\review\native_tests.exe'
if ($LASTEXITCODE -ne 0) { throw 'Native tests failed' }
python test/check_crsf_adc.py
python test/check_crsf_settings.py
rtk git diff --check
rtk pio run -e esp32dev
```

On this workspace, the refreshed native baseline checked during planning was **95 tests, zero failures**. Concurrent neutral/calibration edits were present; rerun on the settled implementation snapshot before adding feature tests. Direct native PlatformIO builds currently fail on missing `unity_config.h`, so the plan uses the existing direct compiler check rather than assuming that target works. No feature code or ESP32 build was produced by this planning task.

After implementation is verified, bench-check receiver output at both endpoints and center, then repeat with reversal and channel remapping. Start with a conservative travel range and approach the actual servo/linkage limits gradually; software tests cannot establish physical clearance. Upload and hardware testing require a separate implementation/deployment request.
