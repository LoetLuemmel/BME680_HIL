# CLAUDE.md — Pico BME680 Hardware-in-the-Loop Demo

## Project purpose

This project demonstrates **gradual, measurable firmware improvement** driven by Claude Code in an automated hardware-in-the-loop cycle. The target is a Bosch BME680 environmental sensor (Temperature + Humidity + Pressure + Gas/VOC) connected to a Raspberry Pi Pico, with a Raspberry Pi Debug Probe for flashing and serial capture.

Each iteration produces a PR whose description contains before/after metrics proving the improvement. The git history **is** the demo artifact.

---

## Hardware setup

### Wiring

| BME680 Pin | Pico Pin            | Notes                                  |
|------------|---------------------|----------------------------------------|
| VCC        | 3V3 OUT (pin 36)    | 3.3 V only — never 5 V                |
| GND        | GND (pin 38)        | Common ground                          |
| SDA        | GP4 (pin 6)         | I2C0 data                              |
| SCL        | GP5 (pin 7)         | I2C0 clock                             |
| SDO        | GND (optional)      | Sets I2C address to 0x76 (default 0x77)|

### Debug probe connections

| Probe Pin | Pico Pin             |
|-----------|----------------------|
| SWCLK     | SWCLK (SWD header)   |
| SWDIO     | SWDIO (SWD header)   |
| GND       | GND (SWD header)     |
| UART TX   | GP1 / UART0 RX (pin 2) |
| UART RX   | GP0 / UART0 TX (pin 1) |

### Sensor characteristics (BME680)

- I2C address: `0x77` (SDO high) or `0x76` (SDO low/GND)
- Measurement modes: Forced mode (single-shot) or Parallel mode (continuous)
- **Temperature**: -40 to +85°C, ±1°C accuracy (typical), ±0.5°C (high-performance)
- **Humidity**: 0-100% RH, ±3% accuracy (typical)
- **Pressure**: 300-1100 hPa, ±1 hPa accuracy (typical), ±0.12 hPa (high-performance)
- **Gas sensor**: Resistance 10kΩ - 2MΩ, requires heater @ 200-400°C for VOC detection
- **IAQ (Indoor Air Quality)**: Derived metric, 0-500 scale (0=excellent, 500=hazardous)
- Initial burn-in: 48 hours for gas sensor, 5-minute warm-up on each power cycle
- Gas sensor heater: Configurable temperature (200-400°C) and duration (1-4032 ms)

---

## Repository structure

```
bme680-hil/
├── CLAUDE.md              ← you are here
├── CMakeLists.txt         ← top-level CMake (Pico SDK project)
├── src/
│   ├── main.c             ← entry point, sensor init, main loop
│   ├── drivers/
│   │   ├── bme680.h       ← driver header
│   │   └── bme680.c       ← driver implementation (evolves each iteration)
├── test/
│   ├── harness.py         ← Python script: flash → capture serial → parse metrics
│   └── metrics_log/       ← per-iteration JSON metric snapshots
├── docs/
│   └── iterations.md      ← human-readable log of what changed and why
└── .github/
    └── workflows/
        └── claude.yml     ← GitHub Actions workflow for @claude mentions
```

---

## Build and flash commands

```bash
# Configure (first time)
mkdir build && cd build
cmake -DPICO_SDK_PATH=$PICO_SDK_PATH ..

# Build
cd build && make -j$(nproc)

# Flash via debug probe (OpenOCD)
openocd -f interface/cmsis-dap.cfg -f target/rp2040.cfg \
  -c "adapter speed 5000" \
  -c "program bme680-hil.elf verify reset exit"

# Alternatively, flash via picotool over SWD
picotool load -f bme680-hil.uf2
```

---

## Test harness (test/harness.py)

The harness is the backbone of the automated loop. It:

1. Builds the firmware (`make`)
2. Flashes it via the debug probe
3. Opens the debug probe's UART serial port (typically `/dev/ttyACM0`, 115200 baud)
4. Captures 60 seconds of structured output lines
5. Parses metrics from the output
6. Writes a JSON snapshot to `test/metrics_log/iteration_NN.json`
7. Prints a summary to stdout

### Expected serial output format

Every firmware iteration **must** print metrics in this parseable format on UART0:

```
[METRIC] read_ok=1 temp=23.4 hum=45.2 press=1013 gas=125000 iaq=50 ts_ms=12345
[METRIC] read_fail=1 err=I2C_TIMEOUT ts_ms=12400
[SUMMARY] reads=120 fails=3 fail_rate=2.50 temp_mean=23.5 temp_stddev=0.3 hum_mean=45.0 hum_stddev=2.1 press_mean=1013 press_stddev=0.5 gas_mean=120000 gas_stddev=15000 iaq_mean=52 iaq_stddev=8 uptime_s=60
```

- `[METRIC]` lines: one per read attempt (success or failure)
- `[SUMMARY]` line: emitted once at the end of the capture window
- All values are plain integers or fixed-point decimals — no floats with exponents

### Metrics we track across iterations

| Metric            | Unit    | Goal       | Notes                                    |
|-------------------|---------|------------|------------------------------------------|
| `fail_rate`       | %       | → 0        | I2C read failures / total attempts       |
| `temp_stddev`     | °C      | ↓ lower    | Noise on temperature stream (60 s window)|
| `hum_stddev`      | %RH     | ↓ lower    | Noise on humidity stream                 |
| `press_stddev`    | hPa     | ↓ lower    | Noise on pressure stream                 |
| `gas_stddev`      | Ω       | ↓ lower    | Noise on gas resistance stream           |
| `iaq_stddev`      | index   | ↓ lower    | IAQ stability                            |
| `first_valid_ms`  | ms      | ↓ lower    | Time from boot to first valid reading    |
| `temp_drift`      | °C      | ↓ lower    | Self-heating compensation quality        |
| `power_ua`        | µA avg  | ↓ lower    | Estimated average current draw           |
| `boot_self_test`  | pass/fail | → pass   | Hardware self-test on startup            |

---

## Iteration plan

Each iteration is a separate branch and PR. The PR description **must** include a metrics table comparing before/after.

### Iteration 1 — Naive driver
- Bare-bones I2C init + read of temperature, humidity, pressure, gas resistance
- No error handling, no warm-up logic, no filtering
- No gas sensor heater configuration (use default)
- Expect: high `fail_rate`, high noise on all sensors, no `boot_self_test`

### Iteration 2 — Robust I2C communication
- Add status register check before reading data
- Add I2C retry with exponential backoff (3 attempts, 10/50/200 ms)
- Validate data-ready flags before reading
- Check for sensor errors via error registers
- Expect: `fail_rate` drops dramatically

### Iteration 3 — Sensor lifecycle management
- Implement correct power-on sequence (soft reset, check chip ID 0x61)
- Configure gas sensor heater profile (320°C, 150ms for VOC detection)
- Add 5-minute warm-up awareness for gas sensor: flag readings as `warming_up=true`
- Implement forced mode (single-shot) measurements
- Expect: `first_valid_ms` becomes meaningful, gas readings stabilize

### Iteration 4 — Signal conditioning
- Add moving-average filter for temperature (N=5)
- Add median filter for pressure (N=5) to reject outliers
- Implement self-heating compensation (gas sensor heats chip, distorts temp/hum)
- Calculate IAQ index from gas resistance baseline
- Expect: all `_stddev` metrics drop significantly
- Measure step-response latency (breathe on sensor → time to peak)

### Iteration 5 — Baseline calibration
- Read gas resistance baseline after 5-min warm-up
- Store baseline to Pico flash (use flash_range_program)
- Restore baseline on boot if stored value exists and is < 7 days old
- Implement altitude compensation for pressure readings
- Humidity calibration (if needed)
- Expect: `iaq_stddev` and `temp_drift` improve across power cycles

### Iteration 6 — Power optimization
- Implement sleep between measurements (forced mode with deep sleep)
- Optimize gas sensor duty cycle (heater on only during measurement)
- Use low-power I2C wake-up if available
- Expect: `power_ua` drops significantly, `fail_rate` stays at 0

### Iteration 7 — Diagnostics and self-test
- Automated boot self-test: verify chip ID (0x61), calibration coefficients
- Structured error logging with timestamps and error codes
- Track cumulative error statistics (histogram of error types)
- Add a `[DIAG]` output section for the test harness
- Out-of-range detection (temp: -40 to +85°C, hum: 0-100%, press: 300-1100 hPa)
- Gas sensor heater stability check
- Expect: `boot_self_test` = pass, full error coverage

---

## Rules for Claude Code

### Do
- Always run the test harness after flashing and include metrics in your PR description
- Keep each iteration focused on one improvement area — small, reviewable diffs
- Use the Pico SDK's hardware I2C API (`hardware/i2c.h`), not bit-banged I2C
- Use `printf()` over UART0 for all output (the debug probe captures this)
- Write C (not C++) — this is a bare-metal Pico SDK project
- Commit with descriptive messages: `iter-03: add gas sensor heater configuration`
- Always check that the firmware compiles before committing

### Don't
- Don't skip iterations or combine multiple improvement areas into one PR
- Don't use `stdio_init_all()` with USB — we use UART only (debug probe)
- Don't hardcode I2C pins — use `#define` constants in the header
- Don't assume the sensor is ready immediately after power-on
- Don't write to flash on every boot — only update baseline when it's stale
- Don't use floating-point in ISRs or time-critical paths

### PR description template

```markdown
## Iteration N: [Short title]

### What changed
[1–3 sentences describing the improvement]

### Metrics (60 s capture window)

| Metric         | Before (iter N-1) | After (iter N) | Change   |
|----------------|--------------------:|---------------:|---------:|
| fail_rate      |              X.XX% |          X.XX% | ↓ X.XX%  |
| temp_stddev    |              X.XX  |          X.XX  | ↓ X.XX   |
| hum_stddev     |              X.XX  |          X.XX  | ↓ X.XX   |
| press_stddev   |              X.XX  |          X.XX  | ↓ X.XX   |
| gas_stddev     |            XXXXX   |        XXXXX   | ↓ XXXXX  |
| first_valid_ms |              XXXX  |          XXXX  | ↓ XXXX   |

### Test stimulus
[Describe test conditions: ambient air, breath test, marker pen near sensor, etc.]

### Next iteration plan
[What should iteration N+1 tackle?]
```

---

## Quick reference: BME680 register map

| Register      | Addr | R/W | Description                        |
|---------------|------|-----|------------------------------------|
| CHIP_ID       | 0xD0 | R   | Should read 0x61                   |
| RESET         | 0xE0 | W   | Write 0xB6 for soft reset          |
| CTRL_GAS_1    | 0x71 | R/W | Gas sensor control                 |
| CTRL_HUM      | 0x72 | R/W | Humidity oversampling              |
| STATUS        | 0x73 | R   | Measurement status flags           |
| CTRL_MEAS     | 0x74 | R/W | Pressure/temp oversampling + mode  |
| CONFIG        | 0x75 | R/W | IIR filter coefficient             |
| MEAS_STATUS   | 0x1D | R   | Measurement in progress flag       |
| PRESS_MSB     | 0x1F | R   | Pressure data (3 bytes)            |
| TEMP_MSB      | 0x22 | R   | Temperature data (3 bytes)         |
| HUM_MSB       | 0x25 | R   | Humidity data (2 bytes)            |
| GAS_R_MSB     | 0x2A | R   | Gas resistance data (2 bytes)      |
| GAS_RANGE     | 0x2B | R   | Gas resistance range               |

Gas sensor heater control registers: 0x5A-0x69 (target temp, duration, wait time)

---

## Environment assumptions

- Pico SDK installed, `PICO_SDK_PATH` set
- `arm-none-eabi-gcc` toolchain installed
- OpenOCD or picotool installed and on PATH
- Debug probe appears as `/dev/ttyACM0` (Linux) or `/dev/cu.usbmodemXXXX` (macOS) — adjust in harness.py if needed
- Python 3.8+ with `pyserial` installed for the test harness
- `gh` CLI authenticated for PR creation

---

## Serial port safety rules (macOS)

### CRITICAL — read this before any serial access

Follow these rules strictly to avoid crashes:

1. **NEVER** use `cat`, `timeout cat`, or `head` on serial ports — they don't set baud rate and cause hangs
2. **NEVER** open a port without first checking it exists: `test -e /dev/cu.usbmodemXXXX`
3. **ALWAYS** use pyserial with a `timeout` parameter on the Serial object
4. **ALWAYS** wrap serial access in try/except — the port may be busy, disconnected, or locked
5. **ALWAYS** close the port in a finally block
6. If `pyserial` is not installed, run: `pip3 install pyserial`
7. If a serial capture returns zero lines, do NOT retry more than twice — report the issue instead
8. The baud rate is **115200** — never omit this

### Finding the correct port

```bash
# List all available serial ports
ls /dev/cu.usbmodem* 2>/dev/null || echo "No USB serial ports found"

# Check which port is accessible
for port in /dev/cu.usbmodem*; do
  echo "=== $port ==="
  stty -f "$port" 2>/dev/null && echo "accessible" || echo "busy/locked"
done
```

The correct UART port is typically `/dev/cu.usbmodem4302` or similar.

---

## Pin Configuration

| Function | Pico Pin | GPIO | Notes                    |
|----------|----------|------|--------------------------|
| I2C SDA  | GP4      | 4    | I2C0 data                |
| I2C SCL  | GP5      | 5    | I2C0 clock               |
| LED      | Onboard  | 25   | Visual feedback (blink)  |
| UART TX  | GP0      | 0    | Debug output (115200 baud)|
| UART RX  | GP1      | 1    | Debug input (unused)     |

---

## Test Stimulus Ideas

### Iteration 1-3: Basic functionality
- Ambient air (stable environment)
- Verify all sensors respond

### Iteration 4: Signal conditioning
- **Temperature**: Touch sensor with finger (measure self-heating)
- **Humidity**: Breathe on sensor (measure RH spike)
- **Pressure**: Move sensor up/down 1 meter (±0.12 hPa change)
- **Gas/IAQ**: Breathe on sensor, use marker pen, rubbing alcohol nearby

### Iteration 5: Calibration
- Power cycle test: flash → capture → power off → power on → compare baselines
- Long-term drift: leave running for 1 hour, measure IAQ drift

### Iteration 6: Power optimization
- Measure current draw with multimeter during sleep vs active
- Calculate duty cycle from wake cycles and avg wake time

### Iteration 7: Diagnostics
- Inject errors (disconnect I2C wire briefly)
- Boot with invalid chip ID (wrong sensor)
- Verify error histogram and structured logging

---

## Comparison with CCS811 project

| Aspect              | CCS811 (Air Quality) | BME680 (Environmental) |
|---------------------|----------------------|------------------------|
| Sensor count        | 2 (eCO2, TVOC)       | 4 (T, H, P, Gas/IAQ)   |
| Derived metrics     | None                 | IAQ from gas baseline  |
| Warm-up time        | 20 minutes           | 5 minutes (gas sensor) |
| Heater control      | N/A                  | Required for gas sensor|
| Self-heating        | Minimal              | Significant (gas heater)|
| Calibration         | Baseline register    | Flash + altitude comp  |
| I2C complexity      | Moderate             | High (many registers)  |
| Power optimization  | WAKE pin             | Sleep mode + duty cycle|

---

## Success Criteria

By iteration 7, the firmware should demonstrate:
- ✅ 0.00% I2C failure rate
- ✅ Temperature stability ±0.1°C (ambient conditions)
- ✅ Humidity stability ±1% RH
- ✅ Pressure stability ±0.1 hPa
- ✅ IAQ stability ±5 index points
- ✅ Self-heating compensation working (temp drift < 0.2°C after 5 min)
- ✅ Power consumption < 1 mA average (60s measurement interval)
- ✅ Boot self-test passing (chip ID, calibration valid)
- ✅ Full diagnostic coverage (structured errors, histogram)

All previous CCS811 learnings apply: robust I2C, signal conditioning, baseline calibration, power management, diagnostics.
