# Iteration Log

This file tracks the evolution of the BME680 firmware through each iteration.

## Iteration 1: Naive Driver (Baseline)

**Date**: TBD

**Goal**: Establish baseline performance with minimal error handling.

**Implementation**:
- Basic I2C init and communication
- Read temperature, humidity, pressure, gas resistance
- Default gas sensor heater configuration (320°C, 150ms)
- Forced mode measurements every 5 seconds
- Structured metric output for test harness

**Expected Metrics**:
- fail_rate: High (20-50%) - no retry logic
- temp_stddev: High - no filtering
- hum_stddev: High - no filtering
- press_stddev: Moderate - inherently stable
- gas_stddev: Very high - gas sensor unstable during warm-up
- first_valid_ms: ~300-500ms (first measurement after init)

**Test Stimulus**: Ambient air, stable environment

**Status**: Ready for implementation

---

## Iteration 2: Robust I2C Communication

**Goal**: Achieve 0% I2C failure rate

**Planned Changes**:
- Add I2C retry with exponential backoff (3 attempts: 10ms, 50ms, 200ms)
- Validate status registers before reading data
- Check for sensor errors via error registers
- Proper error handling and reporting

**Target Metrics**:
- fail_rate: → 0.00%
- Other metrics: Remain similar to iter-1

---

## Iteration 3: Sensor Lifecycle Management

**Goal**: Optimize gas sensor warm-up and heater configuration

**Planned Changes**:
- Verify chip ID (0x61) on boot
- Soft reset sequence
- 5-minute warm-up tracking for gas sensor
- Optimized heater profiles for VOC detection
- Flag early readings as `warming_up=true`

**Target Metrics**:
- first_valid_ms: More meaningful (after warm-up)
- gas_stddev: Reduce significantly

---

## Iteration 4: Signal Conditioning

**Goal**: Reduce noise across all sensors

**Planned Changes**:
- Moving average filter for temperature (N=5)
- Median filter for pressure (N=5)
- Self-heating compensation (gas heater affects temp/hum)
- IAQ index calculation from gas resistance baseline

**Target Metrics**:
- temp_stddev: ↓ 50-70%
- hum_stddev: ↓ 40-60%
- press_stddev: ↓ 30-50%
- gas_stddev: ↓ 60-80%

---

## Iteration 5: Baseline Calibration

**Goal**: Stable IAQ readings across power cycles

**Planned Changes**:
- Read gas resistance baseline after 5-min warm-up
- Store baseline to flash with timestamp
- Restore baseline on boot if < 7 days old
- Altitude compensation for pressure
- Humidity calibration if needed

**Target Metrics**:
- iaq_stddev: Reduce significantly
- temp_drift: Improve (self-heating compensation)

---

## Iteration 6: Power Optimization

**Goal**: Minimize average current draw

**Planned Changes**:
- Sleep mode between measurements
- Gas sensor heater duty cycling
- Optimize measurement intervals
- Low-power I2C wake-up

**Target Metrics**:
- power_ua: Target < 500 µA average (60s interval)
- fail_rate: Maintain 0.00%

---

## Iteration 7: Diagnostics and Self-Test

**Goal**: Production-ready monitoring and error detection

**Planned Changes**:
- Boot self-test: chip ID, calibration coefficients
- Structured error logging with timestamps
- Cumulative error statistics (histogram)
- Out-of-range detection (temp, hum, press)
- Gas sensor heater stability check
- `[DIAG]` output section

**Target Metrics**:
- boot_self_test: PASS
- Full diagnostic coverage

---

## Summary

| Iteration | Focus                  | Key Metric              |
|-----------|------------------------|-------------------------|
| 1         | Naive baseline         | Establish baseline      |
| 2         | Robust I2C             | fail_rate → 0%          |
| 3         | Lifecycle mgmt         | gas_stddev ↓            |
| 4         | Signal conditioning    | all _stddev ↓           |
| 5         | Calibration            | IAQ stability           |
| 6         | Power optimization     | power_ua ↓              |
| 7         | Diagnostics            | Production-ready        |
