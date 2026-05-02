# esphome_max17055

ESPHome external component for the **Maxim / Analog Devices MAX17055**
ModelGauge m5 EZ fuel gauge IC.

---

> ⚠️ **Safety Warning**
> The MAX17055 is designed for use with Li-ion / LiPo cells.  Li-ion cells can
> catch fire or explode if overcharged, over-discharged, short-circuited, or
> physically damaged.  Always use a dedicated protection circuit (BMS) and never
> rely solely on firmware for cell protection.  This component is for monitoring
> only — it does not protect the cell.

---

## Features

- ModelGauge m5 EZ initialization (datasheet-based, not copied from Arduino)
- POR-aware setup: skips re-initialization on ESP32 deep-sleep wakeup
- All key sensors: voltage, current, SoC, capacity, TTE/TTF, temperature, cycles, age
- Rsense-dependent scaling computed at runtime
- Robust I2C error handling (NaN on read failure)
- Deep sleep compatible (Variant A: MAX17055 remains powered)
- `debug_registers: true` for development

---

## Supported Sensors

| YAML key | Unit | Description |
|----------|------|-------------|
| `battery_voltage` | V | Cell voltage (VCELL, 78.125 µV/LSB) |
| `state_of_charge` | % | Reported SoC – **use this in HA** |
| `average_state_of_charge` | % | Slower-moving average SoC |
| `current` | A | Instantaneous current (+ = discharge) |
| `average_current` | A | Average current |
| `remaining_capacity` | mAh | Remaining charge |
| `full_capacity` | mAh | Learned full capacity |
| `time_to_empty` | h | Estimated time to empty (NaN if not discharging) |
| `time_to_full` | h | Estimated time to full (NaN if not charging) |
| `temperature` | °C | Die temperature (default) or NTC via AIN |
| `cycle_count` | cycles | Accumulated charge cycles |
| `age` | % | Cell health: 100 % = new, decreases with age |

All sensors are optional.

---

## Configuration Options

```yaml
sensor:
  - platform: max17055
    address: 0x36                        # only valid address; do not change
    update_interval: 60s                 # 60 s recommended; avoid < 10 s

    design_capacity_mah: 3000            # cell design capacity in mAh
    sense_resistor_mohm: 10              # shunt resistance in mΩ (0.010 Ω)
    charge_termination_current_ma: 150   # IChgTerm in mA (~C/20 typical)
    empty_voltage_mv: 3000               # VEmpty: below this = empty
    recovery_voltage_mv: 3800            # VRecovery: hysteresis recovery point
    # Supported values: li_ion (default) | li_ion_hv
    #   li_ion    – standard Li-ion / LiPo, charge ≤ 4.2 V  (ModelCFG 0x8000)
    #   li_ion_hv – high-voltage Li-ion e.g. NMC/NCA, charge ≤ 4.35 V  (ModelCFG 0x8400)
    # LiFePO4 / LFP is NOT supported – see Known Limitations.
    battery_chemistry: li_ion

    skip_initialization: false           # true = skip EZ-config (debug only)
    force_init: false                    # true = always run EZ-config
    debug_registers: false               # true = log all registers on every update
    enable_sleep_mode: false             # reserved for future use
```

---

## Hardware Connection

### Schematic Overview

```
   ┌─────────── Battery (+) ───────────────────────────────────────────────┐
   │                                                                        │
   │    ┌──────────────────┐                                               │
   │    │   MAX17055       │                                               │
   └───►│ BATT / VBAT      │                                               │
        │                  │                                               │
        │ VDD ◄────────────┼──── 3.3V (from regulator or BATT via LDO)    │
        │                  │                                               │
        │ SDA ◄────────────┼──── ESP32 SDA (e.g. GPIO21) + 4.7kΩ pull-up │
        │ SCL ◄────────────┼──── ESP32 SCL (e.g. GPIO22) + 4.7kΩ pull-up │
        │                  │                                               │
        │ GND ─────────────┼──── GND (common ground)                      │
        │                  │                          ┌─── CSN+ (to BAT+) │
        │ CSP ◄────────────┼──── ──────── Rsense ────┤                    │
        │ CSN ◄────────────┼──── ──────────────────────── Load / Charger  │
        └──────────────────┘                                               │
                                                                           │
   Battery (−) ──────────────────────────────────────────────────── GND ──┘
```

### Sense Resistor Placement

**Critical**: The shunt resistor must be placed in the **complete cell current path**.
Every milliamp flowing into or out of the cell must flow through it.

```
Battery (+) ──┬─► MAX17055 CSP
              │
             [Rsense, e.g. 10 mΩ]
              │
              └─► MAX17055 CSN ──► Regulator / Charger / Load

Battery (−) ──────────────────────► GND
```

### Breakout Board Variants

| Board type | Current measurement | Notes |
|------------|--------------------|----|
| MAX17055 bare IC on your PCB | ✅ Full | You control shunt placement |
| Breakout with onboard shunt (e.g. SparkFun MAX17055) | ✅ Full | Verify shunt is in the cell path |
| Breakout without shunt / shunt not in load path | ❌ Voltage only | Current/capacity reads will be wrong |
| Chip used only with BATT wired, no CSP/CSN | ❌ Voltage only | ModelGauge works in voltage-only mode |

> **MAX17055 vs LC709203F**:
> The LC709203F is a simpler voltage-based fuel gauge.  The MAX17055 adds
> Coulomb counting (via the shunt), temperature compensation, learned capacity,
> and the ModelGauge m5 EZ adaptive algorithm.  With a correctly placed shunt
> the MAX17055 is significantly more accurate across varying loads and
> temperatures.

---

## Installation

```yaml
external_components:
  - source:
      type: git
      url: https://github.com/ceear/esphome_max17055
      ref: main
    components: [max17055]
```

Add to your `sensor:` platform:

```yaml
sensor:
  - platform: max17055
    state_of_charge:
      name: "Battery"
    battery_voltage:
      name: "Battery Voltage"
```

---

## Basic Example (1S LiPo, 2000 mAh, 10 mΩ shunt)

```yaml
i2c:
  sda: GPIO21
  scl: GPIO22

sensor:
  - platform: max17055
    update_interval: 60s
    design_capacity_mah: 2000
    sense_resistor_mohm: 10
    charge_termination_current_ma: 100
    empty_voltage_mv: 3000
    battery_chemistry: li_ion

    state_of_charge:
      name: "Battery"
    battery_voltage:
      name: "Battery Voltage"
    current:
      name: "Battery Current"
    time_to_empty:
      name: "Battery TTE"
```

See `examples/` for full examples including deep sleep.

---

## Deep Sleep

### Variant A – MAX17055 Always Powered (Recommended)

The MAX17055 is powered from the cell rail directly.  The ESP32 sleeps; the
chip continues running its SoC model.  On wakeup, POR is clear and the
component reads values immediately without re-initializing.

```
ESP32 deep sleep cycle:
  boot → setup() (POR=0, skip init) → update() → publish → sleep
```

Hardware requirement: MAX17055 BATT and VDD remain powered while ESP32 sleeps.

### Variant B – MAX17055 Also Powered Off

Not recommended.  Every wakeup triggers full EZ-config reinitiation.  SoC
accuracy degrades until the ModelGauge m5 algorithm re-converges (several
cycles).  See `docs/initialization.md` for details.

---

## Debugging

Enable register dump:
```yaml
sensor:
  - platform: max17055
    debug_registers: true
```

Set log level:
```yaml
logger:
  level: DEBUG
```

The dump logs all key registers on every `update()` call.  Check that:
- `DesignCap` matches `design_capacity_mah × sense_resistor_mohm / 5`
- `ModelCFG` shows Refresh bit clear after boot
- `STATUS` shows POR = 0 after first successful init

---

## Known Limitations

- **LiFePO4 / LFP is not supported.** The MAX17055 ModelGauge m5 EZ algorithm
  ships with exactly two pre-loaded OCV models: standard Li-ion (`li_ion`) and
  high-voltage Li-ion (`li_ion_hv`). LFP has a fundamentally different, very
  flat discharge curve — using either Li-ion model produces severely wrong SoC
  readings across most of the useful SoC range. Supporting LFP requires custom
  model loading (writing the full OCV table to the chip), which is not
  implemented in this component. Attempting to set `battery_chemistry: lifepo4`
  will produce a clear error at ESPHome config-parse time.
- NTC/AIN temperature requires manually setting `Config.Tsel` bit; not exposed
  as a YAML option in this version.
- Learned parameter persistence across Variant B power cycles (NVS save/restore
  of FullCapNom, RComp0, TempCo, Cycles) is not implemented.
- `enable_sleep_mode` flag is reserved; HibCFG management beyond the init
  sequence is not yet implemented.

---

## Sources and References

### Primary (normative)

- [MAX17055 Datasheet](https://www.analog.com/media/en/technical-documentation/data-sheets/max17055.pdf)  
  Analog Devices / Maxim Integrated DS18590 Rev 4
- [MAX17055 ModelGauge m5 EZ User Guide (UG6595)](https://www.analog.com/media/en/technical-documentation/user-guides/max17055-user-guide.pdf)
- [MAX17055 Software Implementation Guide (UG6597)](https://www.analog.com/media/en/technical-documentation/user-guides/max17055-software-implementation-guide.pdf)
- [ESPHome External Components Documentation](https://esphome.io/components/external_components.html)

### Reference implementations (consulted, not copied)

The following open-source implementations were reviewed for cross-checking
register values and initialization sequences.  All scalar and initialization
values in this component were independently verified against the datasheet and
application notes.

| Library | License | Used for |
|---------|---------|----------|
| [AwotG/Arduino-MAX17055_Driver](https://github.com/AwotG/Arduino-MAX17055_Driver) | MIT | Init sequence cross-check |
| [ThingPulse/MAX17055-fuel-gauge](https://github.com/ThingPulse/MAX17055-fuel-gauge) | MIT | Register map cross-check |
| [electricimp/MAX17055](https://github.com/electricimp/MAX17055) | MIT | dQAcc/dPAcc value verification |

---

## License

MIT — see [LICENSE](LICENSE).
