# ESPHome Integration Guide

## Which SoC value to use in Home Assistant?

| Sensor         | Register | Description |
|----------------|----------|-------------|
| `state_of_charge`      | RepSOC (0x06) | **Recommended.** Fully filtered, validated output of the ModelGauge algorithm. Use this for the HA battery card and automations. |
| `average_state_of_charge` | AvSOC (0x0E) | Slower moving average; useful for cross-checking. |
| MixSOC (0x0D) | not exposed by default | Intermediate algorithm value; not normally useful to end users. |

Use **`state_of_charge`** (RepSOC) for everything in Home Assistant.

---

## Sensor Availability Matrix

| Sensor | Needs Rsense wired? | Note |
|--------|--------------------|----|
| battery_voltage | No | Accurate with just the chip |
| state_of_charge | No (voltage-based fallback) | More accurate with Rsense |
| average_state_of_charge | No | As above |
| current | **Yes** | 0 without shunt |
| average_current | **Yes** | 0 without shunt |
| remaining_capacity | **Yes** | Incorrect without shunt |
| full_capacity | **Yes** | Incorrect without shunt |
| time_to_empty | **Yes** | Based on current; NaN without |
| time_to_full | **Yes** | Based on current; NaN without |
| temperature | No | Die temperature by default |
| cycle_count | **Yes** | Incremented only when current flows |
| age | **Yes** | Ratio of learned vs. design capacity |

---

## Minimum ESPHome Version

Requires ESPHome **2023.6** or later (introduced `DEVICE_CLASS_DURATION` and the
current `sensor.sensor_schema()` keyword-argument API).

---

## update_interval Guidance

| Use case | Recommended interval |
|----------|---------------------|
| Normal monitoring | 60 s |
| Active charging/discharging watch | 15–30 s |
| Deep-sleep node | Equal to or less than `run_duration` |

The MAX17055 measures continuously regardless of the ESPHome poll rate.  A
faster `update_interval` does not improve accuracy; it only increases I2C bus
traffic and logging.

---

## Logging Levels

| Level | Content |
|-------|---------|
| ERROR | I2C failures, init failures |
| WARN  | Timeout during init, failed register reads |
| INFO  | Init success/skip messages |
| DEBUG | Step-by-step init, register values if `debug_registers: true` |

Set `logger: level: DEBUG` and `debug_registers: true` during first-time setup
to verify the initialization sequence and check raw register values.

---

## Current Sign Convention

The MAX17055 reports:
- **Positive current** = current flowing **from** the battery (discharge)
- **Negative current** = current flowing **into** the battery (charge)

This is the conventional "battery discharging is positive" convention.  In Home
Assistant the current sensor will show negative values during charging and
positive during discharging.

---

## Home Assistant Energy Dashboard

The MAX17055 does not directly produce kWh values.  To use the HA energy
dashboard:
1. Create a `template` sensor that converts mAh × voltage to Wh.
2. Alternatively use the `integration` platform sensor on the current sensor.

Example template (in HA configuration.yaml):
```yaml
template:
  - sensor:
      - name: "Battery Power"
        unit_of_measurement: "W"
        state: >
          {{ (states('sensor.battery_current') | float(0))
             * (states('sensor.battery_voltage') | float(0)) }}
        device_class: power
        state_class: measurement
```

---

## External Component Installation

```yaml
external_components:
  - source:
      type: git
      url: https://github.com/ceear/esphome-max17055
      ref: main
    components: [max17055]
```

For local development:
```yaml
external_components:
  - source:
      type: local
      path: /path/to/esphome-max17055/components
    components: [max17055]
```
