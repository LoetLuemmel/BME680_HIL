# BME680 Environmental Sensor - Hardware-in-the-Loop Demo

A demonstration of **iterative firmware improvement** for the Bosch BME680 environmental sensor (Temperature, Humidity, Pressure, Gas/VOC) on Raspberry Pi Pico, driven by automated hardware-in-the-loop testing.

## Quick Start

### Prerequisites

- Raspberry Pi Pico with Pico SDK installed (`PICO_SDK_PATH` set)
- Raspberry Pi Debug Probe (or compatible CMSIS-DAP debugger)
- BME680 sensor breakout board
- Python 3.8+ with `pyserial` installed
- `arm-none-eabi-gcc` toolchain
- OpenOCD or picotool

### Hardware Wiring

| BME680 Pin | Pico Pin     | Notes                    |
|------------|--------------|--------------------------|
| VCC        | 3V3 (pin 36) | 3.3V only, never 5V      |
| GND        | GND (pin 38) | Common ground            |
| SDA        | GP4 (pin 6)  | I2C0 data                |
| SCL        | GP5 (pin 7)  | I2C0 clock               |
| SDO        | GND or Float | Sets I2C addr: 0x76/0x77 |

### Build and Flash

```bash
# Clone or create the project
cd /Users/pitforster/Documents/Dev/Edge_HIL/BME680

# Configure and build
mkdir build && cd build
cmake ..
make -j$(nproc)

# Flash via OpenOCD (with sudo if needed on Linux/macOS)
openocd -f interface/cmsis-dap.cfg -f target/rp2040.cfg \
  -c "adapter speed 5000" \
  -c "program bme680_hil.elf verify reset exit"

# Monitor live output
cd ..
python3 monitor.py
```

### Run Test Harness

```bash
cd test
python3 harness.py --port /dev/cu.usbmodemXXXX --duration 60 --iteration 1
```

## Project Structure

```
BME680/
├── CLAUDE.md              # Detailed project instructions for Claude Code
├── README.md              # This file
├── CMakeLists.txt         # Build configuration
├── main.c                 # Firmware entry point
├── src/
│   └── drivers/
│       ├── bme680.h       # BME680 driver header
│       └── bme680.c       # BME680 driver implementation
├── test/
│   ├── harness.py         # Automated test harness
│   └── metrics_log/       # Per-iteration metrics (JSON)
└── monitor.py             # Simple serial monitor

```

## Iteration Roadmap

This project follows a 7-iteration improvement cycle:

| Iteration | Focus                        | Goal                                |
|-----------|------------------------------|-------------------------------------|
| **1**     | Naive driver                 | Baseline (expect high fail_rate)    |
| **2**     | Robust I2C + retry           | fail_rate → 0%                      |
| **3**     | Sensor lifecycle mgmt        | Gas sensor heater config + warm-up  |
| **4**     | Signal conditioning          | Reduce noise (temp/hum/press/gas)   |
| **5**     | Calibration + baseline       | IAQ stability, altitude comp        |
| **6**     | Power optimization           | Sleep mode, heater duty cycling     |
| **7**     | Diagnostics + self-test      | Production-ready monitoring         |

Each iteration is a separate git branch and pull request with before/after metrics.

## Metrics Tracked

- **fail_rate**: I2C communication failures (%)
- **temp_stddev**: Temperature noise (°C)
- **hum_stddev**: Humidity noise (%RH)
- **press_stddev**: Pressure noise (hPa)
- **gas_stddev**: Gas resistance noise (Ω)
- **first_valid_ms**: Time to first valid gas reading (ms)
- **temp_drift**: Self-heating compensation quality (°C)
- **power_ua**: Average current draw (µA)
- **boot_self_test**: Hardware validation (pass/fail)

## Output Format

The firmware outputs structured metrics for automated parsing:

```
[METRIC] read_ok=1 temp=23.4 hum=45.2 press=1013 gas=125000 gas_valid=1 ts_ms=12345
[SUMMARY] reads=120 fails=0 fail_rate=0.00 temp_mean=23.5 temp_stddev=0.3 ...
```

## Test Stimulus

- **Temperature**: Touch sensor with finger, measure self-heating
- **Humidity**: Breathe on sensor
- **Pressure**: Move sensor up/down 1 meter
- **Gas/VOC**: Breathe on sensor, use marker pen, rubbing alcohol

## Documentation

See **[CLAUDE.md](./CLAUDE.md)** for:
- Complete hardware specifications
- Pin configuration
- Register map reference
- Iteration-by-iteration implementation guide
- Rules for Claude Code automation

## Comparison with CCS811 Project

This project builds on the CCS811 air quality sensor HIL demo:

| Aspect            | CCS811        | BME680              |
|-------------------|---------------|---------------------|
| Sensors           | 2 (eCO2, TVOC)| 4 (T, H, P, Gas)    |
| Warm-up time      | 20 minutes    | 5 minutes           |
| Derived metrics   | None          | IAQ from gas        |
| Self-heating      | Minimal       | Significant (heater)|
| Complexity        | Moderate      | High                |

## License

This is a demonstration project. Use at your own risk.

## Author

Created with Claude Code for demonstrating iterative firmware improvement via automated HIL testing.
