# MAX17055 Register Reference

Source: MAX17055 Datasheet Rev 4 (DS18590); MAX17055 User Guide UG6595 Rev 4.

## I2C Address

| Address | Note |
|---------|------|
| 0x36 (7-bit) | Factory fixed; cannot be changed |

---

## Register Map (subset used by this component)

| Hex  | Name         | Access | Width | Description |
|------|--------------|--------|-------|-------------|
| 0x00 | STATUS       | R/W    | 16    | Alert flags, POR bit |
| 0x05 | RepCap       | R      | 16    | Reported remaining capacity |
| 0x06 | RepSOC       | R      | 16    | Reported state of charge |
| 0x07 | Age          | R      | 16    | Cell age (FullCapNom/DesignCap %) |
| 0x08 | Temp         | R      | 16    | Temperature (signed) |
| 0x09 | VCELL        | R      | 16    | Cell voltage (unsigned) |
| 0x0A | Current      | R      | 16    | Instantaneous current (signed) |
| 0x0B | AvgCurrent   | R      | 16    | Average current (signed) |
| 0x0D | MixSOC       | R      | 16    | Mix-algorithm SoC |
| 0x0E | AvSOC        | R      | 16    | Average SoC |
| 0x10 | FullCapRep   | R      | 16    | Full capacity (reported) |
| 0x11 | TTE          | R      | 16    | Time to empty |
| 0x17 | Cycles       | R      | 16    | Cycle count |
| 0x18 | DesignCap    | R/W    | 16    | Design capacity (written at init) |
| 0x1D | Config       | R/W    | 16    | Configuration |
| 0x1E | IChgTerm     | R/W    | 16    | Charge-termination current |
| 0x20 | TTF          | R      | 16    | Time to full |
| 0x23 | FullCapNom   | R      | 16    | Nominal full capacity |
| 0x3A | VEmpty       | R/W    | 16    | Empty / recovery voltage |
| 0x3D | FStat        | R      | 16    | Fuel-gauge status (DNR) |
| 0x45 | dQAcc        | R/W    | 16    | Coulomb accumulator step |
| 0x46 | dPAcc        | R/W    | 16    | Percentage accumulator step |
| 0x60 | SoftWakeup   | W      | 16    | Soft-wakeup command |
| 0xBA | HibCFG       | R/W    | 16    | Hibernate configuration |
| 0xBB | Config2      | R/W    | 16    | Configuration 2 |
| 0xDB | ModelCFG     | R/W    | 16    | ModelGauge m5 model config |

---

## LSB Scaling

### Wire Byte Order

The MAX17055 sends and receives **16-bit registers LSB-first** (little-endian)
over I2C.  The driver reads `byte[0]` as the low byte and `byte[1]` as the high
byte.

### Voltage (all voltage registers except MaxMinVCell)

```
Voltage [V] = raw_unsigned × 78.125 µV
            = raw_unsigned × 78.125e-6
```

Example: raw = 0xB800 (47104) → 47104 × 78.125 µV = 3.680 V

### Current (registers 0x0A, 0x0B) — Rsense-dependent

```
Current [mA] = raw_signed × 1.5625 / Rsense_mΩ
```

Where `raw_signed` is the 16-bit value reinterpreted as two's-complement.

| Rsense | Current LSB |
|--------|-------------|
| 5 mΩ   | 0.3125 mA   |
| 10 mΩ  | 0.15625 mA  |
| 20 mΩ  | 0.078125 mA |

Positive current = discharge; negative current = charge.

### Capacity (registers 0x05, 0x10, 0x18, 0x23) — Rsense-dependent

```
Capacity [mAh] = raw_unsigned × 5.0 / Rsense_mΩ
```

| Rsense | Capacity LSB |
|--------|--------------|
| 5 mΩ   | 1.0 mAh      |
| 10 mΩ  | 0.5 mAh      |
| 20 mΩ  | 0.25 mAh     |

### Temperature (register 0x08)

```
Temperature [°C] = raw_signed / 256
```

The upper byte is the integer part; the lower byte is the fractional part.
Default source: internal die temperature.  Set `Config.Tsel` (bit 4) to use
the AIN pin (external NTC).

### State of Charge (registers 0x06, 0x0D, 0x0E)

```
SoC [%] = raw_unsigned / 256
```

The upper byte is the integer percentage (0–100).

### Time (registers 0x11, 0x20)

```
Time [h] = raw_unsigned × 5.625 / 3600
```

A raw value of 0xFFFF means "no valid estimate" (no current flowing or
estimate not converged).  The driver publishes `NaN` in this case.

### Cycles (register 0x17)

```
Cycles = raw_unsigned / 100
```

Each LSB represents 1% of a full charge/discharge cycle.

### Age (register 0x07)

```
Age [%] = raw_unsigned / 256
```

100% = new cell at designed capacity.  Decreases as the cell ages.

### IChgTerm (register 0x1E) — conversion from mA

```
IChgTerm_raw = charge_term_current_mA × Rsense_mΩ × 16 / 25
             = charge_term_current_mA × Rsense_mΩ / 1.5625
```

### DesignCap (register 0x18) — conversion from mAh

```
DesignCap_raw = design_capacity_mAh × Rsense_mΩ / 5
```

### VEmpty (register 0x3A) — packed format

```
bits[15:7] = VE  (empty voltage),    10 mV/LSB, range 0–5120 mV
bits[ 6:0] = VR  (recovery voltage), 40 mV/LSB, range 0–5080 mV
```

Example: 3000 mV empty / 3800 mV recovery:
```
VE = 3000/10 = 300 = 0x12C
VR = 3800/40 =  95 = 0x5F
VEmpty = (0x12C << 7) | 0x5F = 0x9600 | 0x5F = 0x965F
```

---

## Key Status / Config Bits

### STATUS (0x00)

| Bit | Name | Meaning |
|-----|------|---------|
| 1   | POR  | Power-on reset occurred; init required |
| 3   | BST  | Battery not present (0 = present) |

### FStat (0x3D)

| Bit | Name | Meaning |
|-----|------|---------|
| 0   | DNR  | Data not ready (cleared by HW after first measurement) |

### ModelCFG (0xDB)

| Bit | Name    | Meaning |
|-----|---------|---------|
| 15  | Refresh | Set by SW to start model reload; cleared by HW when done |
| 10  | VChg    | 1 = high-voltage cell (charge > 4.275 V) |
