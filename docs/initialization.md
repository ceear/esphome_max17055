# MAX17055 Initialization Sequence

Source: MAX17055 Software Implementation Guide UG6597 Rev 3;
        MAX17055 ModelGauge m5 EZ User Guide UG6595 Rev 4.

---

## Overview

The MAX17055 uses the **ModelGauge m5 EZ** algorithm.  At power-on the chip
needs to be told the battery's design parameters.  After that one-time setup the
algorithm runs autonomously — including during ESP32 deep sleep.

The initialization is gated by the **POR** (Power-on Reset) bit in the STATUS
register (bit 1).  This bit is set by the chip only after a full power cycle of
the MAX17055 itself.  An ESP32 reset or deep-sleep wakeup does **not** set POR
if the chip remained powered.

---

## Step-by-Step Sequence

### Step 0 – Check device presence

Read STATUS (0x00).  If the I2C transaction fails, the chip is not wired
correctly.

### Step 1 – Check POR bit

```
if (STATUS & 0x0002) == 0:
    skip to "No-init path" below
```

If POR is clear, the chip already holds a valid model and learned
parameters.  Do not overwrite them — re-initialization degrades accuracy.

### Step 1.1 – Wait for FStat.DNR to clear

After power-on the chip performs its first measurement.  `FStat.DNR` (bit 0 of
register 0x3D) is 1 until the measurement is complete.  Poll with 10 ms
intervals; normally clears within 500 ms.

```
while (FStat & 0x0001) != 0:
    wait 10 ms
```

### Step 1.2 – Exit hibernate mode

The ModelGauge m5 loader does not run in hibernate mode.  The recommended
sequence (UG6597 §1.2):

```
HibCFG_saved = read(0xBA)          ; save original HibCFG
write(0x60, 0x0090)                ; SoftWakeup: exit hibernate
write(0xBA, 0x0000)                ; clear HibCFG (disable hibernate)
write(0x60, 0x0000)                ; clear SoftWakeup command
```

### Step 2 – Write EZ-config parameters

All values must be in **register LSBs** (not raw mAh/mA):

| Register | Value formula |
|----------|---------------|
| DesignCap (0x18) | `design_capacity_mAh × Rsense_mΩ / 5` |
| IChgTerm (0x1E)  | `charge_term_mA × Rsense_mΩ / 1.5625` |
| VEmpty (0x3A)    | `(VE_raw << 7) \| VR_raw`  (see registers.md) |
| dQAcc (0x45)     | `DesignCap_raw / 32` |
| dPAcc (0x46)     | `0x0C80` (EZ fixed value per UG6597) |

Write order: DesignCap, IChgTerm, VEmpty, dQAcc, dPAcc.

### Step 2.1 – Trigger model reload

```
ModelCFG = 0x8000          ; standard Li-ion (charge ≤ 4.275 V)
ModelCFG = 0x8400          ; high-voltage Li-ion (charge > 4.275 V)
write(0xDB, ModelCFG)
```

The Refresh bit (bit 15) is set implicitly by writing either value since both
0x8000 and 0x8400 have bit 15 set.

### Step 2.2 – Wait for model reload to complete

The chip clears bit 15 when done.  Poll with 10 ms intervals; normally
completes within 500 ms.

```
while (read(0xDB) & 0x8000) != 0:
    wait 10 ms
```

### Step 3 – Restore HibCFG

```
write(0xBA, HibCFG_saved)
```

### Step 4 – Clear POR bit

Read-modify-write; clear only bit 1, preserve alert bits:

```
STATUS = read(0x00)
STATUS &= ~0x0002
write(0x00, STATUS)
```

---

## No-Init Path (POR = 0)

This path is taken on every ESP32 wakeup from deep sleep when the MAX17055
remained powered:

1. Read STATUS → POR = 0 → skip all initialization.
2. Proceed directly to reading sensor registers.

The chip's learned model, cycle count, and accumulated SoC are all preserved.
This is the correct behaviour for Variant A deep-sleep deployments.

---

## dPAcc Value Rationale

The value 0x0C80 (3200) comes from the MAX17055 SW Implementation Guide and
represents a fixed resolution step optimized for the EZ configuration.  It
corresponds to `51200 / 16 = 3200`.  Multiple published reference implementations
(Electric Imp, AwotG Arduino, Mbed) use the same value or close variants.  For
full custom model configurations a different dPAcc may apply; EZ config always
uses 0x0C80.

---

## Hibernate / Sleep Mode

### What hibernate does

HibCFG controls the current threshold below which the chip enters its low-power
hibernate state (~5 µA vs ~7 µA active).  In hibernate:
- ADC conversion rate is reduced.
- The algorithm still tracks SoC but with lower time resolution.
- TTE/TTF estimates may be less accurate.

### Recommended setting for deep-sleep nodes (Variant A)

Leave HibCFG at its default value.  The chip will enter hibernate automatically
during long idle periods (when battery current is very low), maintaining good
power economy without sacrificing SoC tracking.

### Should hibernate be disabled?

Only disable HibCFG (write 0x0000) if you require continuous high-rate
measurements.  For a deep-sleep node this is counterproductive — the battery
current during ESP32 sleep is typically well below the hibernate threshold
anyway.

---

## Power-on Timing

After the MAX17055 powers on it requires approximately 500 ms for the first
valid measurement (FStat.DNR must be 0).  During this time:
- VCELL may read 0 or a stale value.
- RepSOC may read 0.

Do not trust sensor values until DNR has cleared and, if initialization was
required, until the ModelCFG.Refresh bit has cleared.

For deep-sleep nodes the ESP32 typically takes 1–2 s to boot and connect to
WiFi, which is more than enough time.  The 100 ms `on_boot` delay in the
example YAML is a conservative safety margin.

---

## Variant B – MAX17055 Re-initialized Every Wakeup

**Not recommended** for accurate coulomb counting.

If the MAX17055 is also powered off between ESP32 wakeups (e.g. via a load
switch or a combined power domain):
- POR is set on every wakeup.
- The EZ-config initialization runs every time.
- The chip re-starts the model with default parameters.
- Learned capacity and SoC must re-converge, taking multiple cycles.
- Coulomb counting accuracy is degraded until re-convergence.

If Variant B cannot be avoided:
- Accept ~5–10 % SoC error until the model converges.
- Store and restore learned parameters (FullCapNom, Cycles, RComp0, TempCo)
  in NVS between wakeups using ESPHome's `preferences` API.  This is not
  implemented in the current version of this component.
