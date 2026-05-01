# ADC to CRSF Bench Results

Baseline provenance: `serial_capture_baseline_adc_crsf.log`, captured on `COM4 @ 115200` during the `2026-05-01` Task 1 validation session (artifact timestamp `2026-05-01 10:44` local). The capture reflects the CRSF runtime source state at commit `2ece7703370d86d9c139d5134a45f61a528681d9` (`add 500Hz option and fix module communication errors`), before any new ADC-to-CRSF instrumentation for this validation flow. BatMon startup lines in that capture are unrelated baseline boot noise, not CRSF failure.

## Runtime Path

- ADC probe: `ELRSCrsfMode::initAds1015()`
- ADC sample loop: `ELRSCrsfMode::sampleAxes()`
- Channel mapping owner: `ELRSCrsfCore::updateChannels()`
- RC frame transmit path: `ELRSCrsfTransport::setChannels()` -> `ELRSCrsfTransport::loop()`

## Expected Mapping

- CH1 = Roll
- CH2 = Pitch
- CH3 = Throttle
- CH4 = Yaw

## Bench Matrix

| Test | Raw ADC evidence | Mapped channel evidence | TX frame evidence | Result |
|---|---|---|---|---|
| Roll low/center/high | | | | |
| Pitch low/center/high | | | | |
| Throttle low/center/high | | | | |
| Yaw low/center/high | | | | |
| Dual-axis move | | | | |
| Idle stability | | | | |
| ADC missing | | | | |
