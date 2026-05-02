#pragma once

#include "esphome/core/component.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/i2c/i2c.h"

namespace esphome {
namespace max17055 {

// ──────────────────────────────────────────────────────────────────────────────
// Register map  (MAX17055 datasheet Rev 4, Table 1 / User Guide UG6595)
// ──────────────────────────────────────────────────────────────────────────────
enum Max17055Reg : uint8_t {
  REG_STATUS       = 0x00,  // Status / alert flags, POR bit
  REG_REP_CAP      = 0x05,  // Reported remaining capacity
  REG_REP_SOC      = 0x06,  // Reported state of charge (%)
  REG_AGE          = 0x07,  // Age / capacity-fade percentage
  REG_TEMP         = 0x08,  // Temperature (die or NTC, see Config Tsel)
  REG_VCELL        = 0x09,  // Cell voltage
  REG_CURRENT      = 0x0A,  // Instantaneous current (signed)
  REG_AVG_CURRENT  = 0x0B,  // Average current (signed)
  REG_MIX_SOC      = 0x0D,  // Mix-algorithm state of charge
  REG_AV_SOC       = 0x0E,  // Average state of charge
  REG_FULL_CAP_REP = 0x10,  // Full capacity (reported)
  REG_TTE          = 0x11,  // Time to empty
  REG_CYCLES       = 0x17,  // Cycle counter
  REG_DESIGN_CAP   = 0x18,  // Design capacity (written during init)
  REG_CONFIG       = 0x1D,  // Configuration register
  REG_ICHG_TERM    = 0x1E,  // Charge-termination current
  REG_TTF          = 0x20,  // Time to full
  REG_FULL_CAP_NOM = 0x23,  // Full capacity nominal (used for age calc)
  REG_V_EMPTY      = 0x3A,  // Empty / recovery voltage thresholds
  REG_FSTAT        = 0x3D,  // Fuel-gauge status (DNR bit)
  REG_DQ_ACC       = 0x45,  // dQAcc  (set during EZ init)
  REG_DP_ACC       = 0x46,  // dPAcc  (set during EZ init)
  REG_SOFT_WAKEUP  = 0x60,  // Soft-wakeup command (exit hibernate)
  REG_HIB_CFG      = 0xBA,  // Hibernate configuration
  REG_CONFIG2      = 0xBB,  // Configuration 2
  REG_MODEL_CFG    = 0xDB,  // ModelGauge m5 model configuration
};

// ── Status register bit masks ─────────────────────────────────────────────────
// Bit 1 (Powe-on Reset) – the key flag for initialization gating
static constexpr uint16_t STATUS_POR  = (1u << 1);

// ── FStat register bit masks ──────────────────────────────────────────────────
// Bit 0: Data Not Ready – cleared by hardware when first measurement completes
static constexpr uint16_t FSTAT_DNR   = (1u << 0);

// ── ModelCFG register ─────────────────────────────────────────────────────────
// Bit 15: Refresh – set by firmware to trigger model reload; cleared by HW when done
static constexpr uint16_t MODELCFG_REFRESH = (1u << 15);
// Bit 10: VChg – set when charge voltage > 4.275 V (Li-ion high-voltage cell)
static constexpr uint16_t MODELCFG_VCHG   = (1u << 10);

// ── SoftWakeup command values (register 0x60) ─────────────────────────────────
// Exit hibernate sequence (SW Implementation Guide §1.2)
static constexpr uint16_t SOFT_WAKEUP_EXIT    = 0x0090;
static constexpr uint16_t SOFT_WAKEUP_CLEAR   = 0x0000;

// ── dPAcc for EZ-Config ──────────────────────────────────────────────────────
// Fixed value from MAX17055 SW Implementation Guide §2 for EZ config.
// 0x0C80 = 3200 = 51200 / 16; corresponds to resolution step used by ModelGauge m5.
static constexpr uint16_t DP_ACC_EZ = 0x0C80;

// ──────────────────────────────────────────────────────────────────────────────
// LSB scaling constants  (datasheet Table 1, all register descriptions)
// ──────────────────────────────────────────────────────────────────────────────
// Voltage: 78.125 µV / LSB  (all voltage registers except MaxMinVCell)
static constexpr float VOLTAGE_LSB_UV = 78.125f;

// Temperature: 1/256 °C / LSB  (signed two's-complement)
static constexpr float TEMP_LSB_C = 1.0f / 256.0f;

// RepSOC / AvSOC / MixSOC: 1/256 % / LSB  (unsigned; upper byte = integer %)
static constexpr float SOC_LSB_PCT = 1.0f / 256.0f;

// Time (TTE/TTF): 5.625 s / LSB. 0xFFFF means "no valid estimate".
static constexpr float TIME_LSB_S = 5.625f;
static constexpr uint16_t TIME_INVALID = 0xFFFF;

// Cycles: 1/100 of a full charge cycle per LSB
static constexpr float CYCLES_LSB = 1.0f / 100.0f;

// Age: 1/256 % per LSB  (same encoding as SOC)
static constexpr float AGE_LSB_PCT = 1.0f / 256.0f;

// Current and capacity LSB are both Rsense-dependent:
//   Current LSB  = 1.5625 µV / Rsense_Ω  →  1.5625 mA / Rsense_mΩ
//   Capacity LSB = 5.0    µVh / Rsense_Ω →  5.0    mAh / Rsense_mΩ
// (computed at runtime from rsense_mohm_, see helper methods below)

// ──────────────────────────────────────────────────────────────────────────────
// Component class
// ──────────────────────────────────────────────────────────────────────────────
class MAX17055Component : public PollingComponent, public i2c::I2CDevice {
 public:
  // ── Configuration setters (called from sensor.py / to_code) ─────────────────
  void set_design_capacity(uint16_t mah)      { design_capacity_mah_     = mah; }
  void set_charge_term_current(uint16_t ma)   { charge_term_current_ma_  = ma;  }
  void set_empty_voltage(uint16_t mv)         { empty_voltage_mv_        = mv;  }
  void set_recovery_voltage(uint16_t mv)      { recovery_voltage_mv_     = mv;  }
  void set_rsense_mohm(uint16_t mohm)         { rsense_mohm_             = mohm; }
  void set_charge_voltage_high(bool high)     { charge_voltage_high_     = high; }
  void set_skip_initialization(bool skip)     { skip_initialization_     = skip; }
  void set_force_init(bool force)             { force_init_              = force; }
  void set_debug_registers(bool dbg)          { debug_registers_         = dbg;  }
  void set_enable_sleep_mode(bool en)         { enable_sleep_mode_       = en;   }

  // ── Sensor setters ───────────────────────────────────────────────────────────
  void set_voltage_sensor(sensor::Sensor *s)       { voltage_sensor_      = s; }
  void set_current_sensor(sensor::Sensor *s)       { current_sensor_      = s; }
  void set_avg_current_sensor(sensor::Sensor *s)   { avg_current_sensor_  = s; }
  void set_rep_soc_sensor(sensor::Sensor *s)       { rep_soc_sensor_      = s; }
  void set_av_soc_sensor(sensor::Sensor *s)        { av_soc_sensor_       = s; }
  void set_rep_cap_sensor(sensor::Sensor *s)       { rep_cap_sensor_      = s; }
  void set_full_cap_sensor(sensor::Sensor *s)      { full_cap_sensor_     = s; }
  void set_tte_sensor(sensor::Sensor *s)           { tte_sensor_          = s; }
  void set_ttf_sensor(sensor::Sensor *s)           { ttf_sensor_          = s; }
  void set_temperature_sensor(sensor::Sensor *s)   { temperature_sensor_  = s; }
  void set_cycles_sensor(sensor::Sensor *s)        { cycles_sensor_       = s; }
  void set_age_sensor(sensor::Sensor *s)           { age_sensor_          = s; }

  // ── ESPHome lifecycle ────────────────────────────────────────────────────────
  void setup() override;
  void update() override;
  void dump_config() override;

  float get_setup_priority() const override { return setup_priority::DATA; }

 protected:
  // ── Low-level I2C helpers ────────────────────────────────────────────────────
  // MAX17055 sends/receives 16-bit words LSB-first on the wire (datasheet §8).
  bool read_register_16(uint8_t reg, uint16_t &value);
  bool write_register_16(uint8_t reg, uint16_t value);
  bool read_signed_register_16(uint8_t reg, int16_t &value);

  // ── Initialization helpers ───────────────────────────────────────────────────
  bool detect_por_();
  bool initialize_ez_config_();
  bool wait_for_dnr_clear_(uint32_t timeout_ms = 500);
  bool wait_for_model_refresh_clear_(uint32_t timeout_ms = 1000);

  // ── Scaling helpers (Rsense-dependent) ───────────────────────────────────────
  // current_lsb_ma_ = 1.5625 / rsense_mohm_
  float current_lsb_ma_() const { return 1.5625f / (float) rsense_mohm_; }
  // capacity_lsb_mah_ = 5.0 / rsense_mohm_
  float capacity_lsb_mah_() const { return 5.0f / (float) rsense_mohm_; }

  // ── Register-value helpers ───────────────────────────────────────────────────
  // Pack VEmpty register: bits[15:7] = VE (10 mV/LSB), bits[6:0] = VR (40 mV/LSB)
  uint16_t pack_vempty_() const;
  // Convert design capacity / charge-termination to register LSBs
  uint16_t design_cap_raw_() const;
  uint16_t ichg_term_raw_() const;

  void log_debug_registers_();

  // ── Configuration ─────────────────────────────────────────────────────────────
  uint16_t design_capacity_mah_    {3000};
  uint16_t charge_term_current_ma_ {200};
  uint16_t empty_voltage_mv_       {3300};
  uint16_t recovery_voltage_mv_    {3880};
  uint16_t rsense_mohm_            {10};
  bool     charge_voltage_high_    {true};   // true  → ModelCFG = 0x8400
  bool     skip_initialization_    {false};
  bool     force_init_             {false};
  bool     debug_registers_        {false};
  bool     enable_sleep_mode_      {false};
  bool     initialized_            {false};

  // ── Sensors ───────────────────────────────────────────────────────────────────
  sensor::Sensor *voltage_sensor_     {nullptr};
  sensor::Sensor *current_sensor_     {nullptr};
  sensor::Sensor *avg_current_sensor_ {nullptr};
  sensor::Sensor *rep_soc_sensor_     {nullptr};
  sensor::Sensor *av_soc_sensor_      {nullptr};
  sensor::Sensor *rep_cap_sensor_     {nullptr};
  sensor::Sensor *full_cap_sensor_    {nullptr};
  sensor::Sensor *tte_sensor_         {nullptr};
  sensor::Sensor *ttf_sensor_         {nullptr};
  sensor::Sensor *temperature_sensor_ {nullptr};
  sensor::Sensor *cycles_sensor_      {nullptr};
  sensor::Sensor *age_sensor_         {nullptr};
};

}  // namespace max17055
}  // namespace esphome
