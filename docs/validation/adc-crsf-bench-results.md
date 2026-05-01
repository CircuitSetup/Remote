# ADC to CRSF Bench Results

Baseline provenance: `serial_capture_baseline_adc_crsf.log`, captured on `COM4 @ 115200` during this `2026-05-01` Task 1 validation session (artifact timestamp `2026-05-01 10:44` local) from the current `CRSF` branch baseline before any new ADC-to-CRSF instrumentation for this validation flow. BatMon startup lines in that capture are unrelated baseline boot noise, not CRSF failure.

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
