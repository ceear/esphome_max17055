#include "max17055.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"

namespace esphome {
namespace max17055 {

static const char *const TAG = "max17055";

// ──────────────────────────────────────────────────────────────────────────────
// ESPHome lifecycle
// ──────────────────────────────────────────────────────────────────────────────

void MAX17055Component::setup() {
  ESP_LOGCONFIG(TAG, "Setting up MAX17055...");

  // Confirm the chip responds – read Status as a presence check.
  uint16_t status;
  if (!read_register_16(REG_STATUS, status)) {
    ESP_LOGE(TAG, "Failed to read STATUS register – is the chip connected?");
    this->mark_failed();
    return;
  }

  if (skip_initialization_) {
    ESP_LOGW(TAG, "skip_initialization=true: skipping EZ-config, reading raw registers");
    initialized_ = true;
    return;
  }

  bool por = (status & STATUS_POR) != 0;
  ESP_LOGD(TAG, "STATUS = 0x%04X  POR=%d", status, (int) por);

  if (!por && !force_init_) {
    // MAX17055 was already running (e.g. ESP32 just woke from deep sleep).
    // The SoC model is intact – no re-initialization needed.
    ESP_LOGI(TAG, "No POR detected – MAX17055 already initialized, skipping EZ-config");
    initialized_ = true;
    return;
  }

  if (force_init_) {
    ESP_LOGW(TAG, "force_init=true: running EZ-config regardless of POR bit");
  }

  if (!initialize_ez_config_()) {
    ESP_LOGE(TAG, "EZ-config initialization failed");
    this->mark_failed();
    return;
  }

  initialized_ = true;
  ESP_LOGI(TAG, "MAX17055 initialized successfully");
}

void MAX17055Component::update() {
  if (!initialized_) {
    ESP_LOGW(TAG, "Not initialized – skipping update");
    return;
  }

  if (debug_registers_) {
    log_debug_registers_();
  }

  // ── Voltage (VCELL 0x09) ───────────────────────────────────────────────────
  // LSB = 78.125 µV  (unsigned)
  if (voltage_sensor_ != nullptr) {
    uint16_t raw;
    if (read_register_16(REG_VCELL, raw)) {
      float v = (float) raw * VOLTAGE_LSB_UV / 1e6f;  // µV → V
      voltage_sensor_->publish_state(v);
    } else {
      ESP_LOGW(TAG, "Failed to read VCELL");
      voltage_sensor_->publish_state(NAN);
    }
  }

  // ── Current (0x0A) ────────────────────────────────────────────────────────
  // LSB = 1.5625 µV / Rsense_Ω  = current_lsb_ma_ mA  (signed two's-complement)
  if (current_sensor_ != nullptr) {
    int16_t raw;
    if (read_signed_register_16(REG_CURRENT, raw)) {
      float ma = (float) raw * current_lsb_ma_();
      current_sensor_->publish_state(ma / 1000.0f);  // mA → A
    } else {
      ESP_LOGW(TAG, "Failed to read Current");
      current_sensor_->publish_state(NAN);
    }
  }

  // ── Average Current (0x0B) ────────────────────────────────────────────────
  if (avg_current_sensor_ != nullptr) {
    int16_t raw;
    if (read_signed_register_16(REG_AVG_CURRENT, raw)) {
      float ma = (float) raw * current_lsb_ma_();
      avg_current_sensor_->publish_state(ma / 1000.0f);
    } else {
      ESP_LOGW(TAG, "Failed to read AvgCurrent");
      avg_current_sensor_->publish_state(NAN);
    }
  }

  // ── RepSOC (0x06) ─────────────────────────────────────────────────────────
  // LSB = 1/256 %  (unsigned; upper byte = integer part)
  if (rep_soc_sensor_ != nullptr) {
    uint16_t raw;
    if (read_register_16(REG_REP_SOC, raw)) {
      rep_soc_sensor_->publish_state((float) raw * SOC_LSB_PCT);
    } else {
      ESP_LOGW(TAG, "Failed to read RepSOC");
      rep_soc_sensor_->publish_state(NAN);
    }
  }

  // ── AvSOC (0x0E) ──────────────────────────────────────────────────────────
  if (av_soc_sensor_ != nullptr) {
    uint16_t raw;
    if (read_register_16(REG_AV_SOC, raw)) {
      av_soc_sensor_->publish_state((float) raw * SOC_LSB_PCT);
    } else {
      ESP_LOGW(TAG, "Failed to read AvSOC");
      av_soc_sensor_->publish_state(NAN);
    }
  }

  // ── RepCap / remaining capacity (0x05) ────────────────────────────────────
  // LSB = 5.0 µVh / Rsense_Ω = capacity_lsb_mah_ mAh  (unsigned)
  if (rep_cap_sensor_ != nullptr) {
    uint16_t raw;
    if (read_register_16(REG_REP_CAP, raw)) {
      rep_cap_sensor_->publish_state((float) raw * capacity_lsb_mah_());
    } else {
      ESP_LOGW(TAG, "Failed to read RepCap");
      rep_cap_sensor_->publish_state(NAN);
    }
  }

  // ── FullCapRep / full capacity (0x10) ────────────────────────────────────
  if (full_cap_sensor_ != nullptr) {
    uint16_t raw;
    if (read_register_16(REG_FULL_CAP_REP, raw)) {
      full_cap_sensor_->publish_state((float) raw * capacity_lsb_mah_());
    } else {
      ESP_LOGW(TAG, "Failed to read FullCapRep");
      full_cap_sensor_->publish_state(NAN);
    }
  }

  // ── TTE / Time to Empty (0x11) ────────────────────────────────────────────
  // LSB = 5.625 s  (unsigned). 0xFFFF = no valid estimate (no discharge current).
  if (tte_sensor_ != nullptr) {
    uint16_t raw;
    if (read_register_16(REG_TTE, raw)) {
      if (raw == TIME_INVALID) {
        tte_sensor_->publish_state(NAN);
      } else {
        tte_sensor_->publish_state((float) raw * TIME_LSB_S / 3600.0f);  // s → h
      }
    } else {
      ESP_LOGW(TAG, "Failed to read TTE");
      tte_sensor_->publish_state(NAN);
    }
  }

  // ── TTF / Time to Full (0x20) ─────────────────────────────────────────────
  if (ttf_sensor_ != nullptr) {
    uint16_t raw;
    if (read_register_16(REG_TTF, raw)) {
      if (raw == TIME_INVALID) {
        ttf_sensor_->publish_state(NAN);
      } else {
        ttf_sensor_->publish_state((float) raw * TIME_LSB_S / 3600.0f);
      }
    } else {
      ESP_LOGW(TAG, "Failed to read TTF");
      ttf_sensor_->publish_state(NAN);
    }
  }

  // ── Temperature (0x08) ────────────────────────────────────────────────────
  // LSB = 1/256 °C  (signed two's-complement)
  // Config.Tsel (bit 4) selects die temperature (0) or AIN/NTC (1).
  if (temperature_sensor_ != nullptr) {
    int16_t raw;
    if (read_signed_register_16(REG_TEMP, raw)) {
      temperature_sensor_->publish_state((float) raw * TEMP_LSB_C);
    } else {
      ESP_LOGW(TAG, "Failed to read Temp");
      temperature_sensor_->publish_state(NAN);
    }
  }

  // ── Cycles (0x17) ─────────────────────────────────────────────────────────
  // LSB = 1/100 full cycle  (unsigned)
  if (cycles_sensor_ != nullptr) {
    uint16_t raw;
    if (read_register_16(REG_CYCLES, raw)) {
      cycles_sensor_->publish_state((float) raw * CYCLES_LSB);
    } else {
      ESP_LOGW(TAG, "Failed to read Cycles");
      cycles_sensor_->publish_state(NAN);
    }
  }

  // ── Age (0x07) ────────────────────────────────────────────────────────────
  // The Age register reports FullCapNom/DesignCap as a percentage (unsigned).
  // LSB = 1/256 %.  100 % = new cell.
  if (age_sensor_ != nullptr) {
    uint16_t raw;
    if (read_register_16(REG_AGE, raw)) {
      age_sensor_->publish_state((float) raw * AGE_LSB_PCT);
    } else {
      ESP_LOGW(TAG, "Failed to read Age");
      age_sensor_->publish_state(NAN);
    }
  }
}

void MAX17055Component::dump_config() {
  ESP_LOGCONFIG(TAG, "MAX17055 Fuel Gauge:");
  LOG_I2C_DEVICE(this);
  if (this->is_failed()) {
    ESP_LOGCONFIG(TAG, "  [FAILED]");
    return;
  }
  ESP_LOGCONFIG(TAG, "  Design Capacity  : %u mAh", design_capacity_mah_);
  ESP_LOGCONFIG(TAG, "  Rsense           : %u mΩ", rsense_mohm_);
  ESP_LOGCONFIG(TAG, "  IChgTerm         : %u mA", charge_term_current_ma_);
  ESP_LOGCONFIG(TAG, "  VEmpty           : %u mV", empty_voltage_mv_);
  ESP_LOGCONFIG(TAG, "  VRecovery        : %u mV", recovery_voltage_mv_);
  ESP_LOGCONFIG(TAG, "  Charge > 4.275V  : %s", charge_voltage_high_ ? "yes" : "no");
  ESP_LOGCONFIG(TAG, "  Current LSB      : %.4f mA", current_lsb_ma_());
  ESP_LOGCONFIG(TAG, "  Capacity LSB     : %.4f mAh", capacity_lsb_mah_());
  LOG_SENSOR("  ", "Voltage",     voltage_sensor_);
  LOG_SENSOR("  ", "Current",     current_sensor_);
  LOG_SENSOR("  ", "AvgCurrent",  avg_current_sensor_);
  LOG_SENSOR("  ", "RepSOC",      rep_soc_sensor_);
  LOG_SENSOR("  ", "AvSOC",       av_soc_sensor_);
  LOG_SENSOR("  ", "RepCap",      rep_cap_sensor_);
  LOG_SENSOR("  ", "FullCap",     full_cap_sensor_);
  LOG_SENSOR("  ", "TTE",         tte_sensor_);
  LOG_SENSOR("  ", "TTF",         ttf_sensor_);
  LOG_SENSOR("  ", "Temperature", temperature_sensor_);
  LOG_SENSOR("  ", "Cycles",      cycles_sensor_);
  LOG_SENSOR("  ", "Age",         age_sensor_);
}

// ──────────────────────────────────────────────────────────────────────────────
// EZ-Config initialization sequence
//
// Source: MAX17055 Software Implementation Guide (UG6597) §3.1
//         MAX17055 ModelGauge m5 EZ User Guide (UG6595)
// ──────────────────────────────────────────────────────────────────────────────
bool MAX17055Component::initialize_ez_config_() {
  // ── Step 1.1: Wait until FStat.DNR clears ─────────────────────────────────
  // After power-on the IC performs an initial measurement; DNR is cleared when
  // data is ready.  Should complete within 500 ms under normal conditions.
  ESP_LOGD(TAG, "Waiting for FStat.DNR to clear...");
  if (!wait_for_dnr_clear_(500)) {
    ESP_LOGE(TAG, "Timeout waiting for DNR clear");
    return false;
  }

  // ── Step 1.2: Exit hibernate ───────────────────────────────────────────────
  // Save original HibCFG; force a soft-wakeup so the model loader can run;
  // clear HibCFG temporarily.
  uint16_t hib_cfg_saved;
  if (!read_register_16(REG_HIB_CFG, hib_cfg_saved)) {
    ESP_LOGE(TAG, "Failed to read HibCFG");
    return false;
  }
  ESP_LOGD(TAG, "Saved HibCFG = 0x%04X", hib_cfg_saved);

  if (!write_register_16(REG_SOFT_WAKEUP, SOFT_WAKEUP_EXIT))  return false;
  if (!write_register_16(REG_HIB_CFG,     0x0000))            return false;
  if (!write_register_16(REG_SOFT_WAKEUP, SOFT_WAKEUP_CLEAR)) return false;

  // ── Step 2: Write EZ-config parameters ────────────────────────────────────
  uint16_t design_cap = design_cap_raw_();
  uint16_t ichg_term  = ichg_term_raw_();
  uint16_t v_empty    = pack_vempty_();
  uint16_t dq_acc     = design_cap / 32;

  ESP_LOGD(TAG, "Writing DesignCap=0x%04X  IChgTerm=0x%04X  VEmpty=0x%04X  dQAcc=0x%04X",
           design_cap, ichg_term, v_empty, dq_acc);

  if (!write_register_16(REG_DESIGN_CAP, design_cap)) return false;
  if (!write_register_16(REG_ICHG_TERM,  ichg_term))  return false;
  if (!write_register_16(REG_V_EMPTY,    v_empty))    return false;
  if (!write_register_16(REG_DQ_ACC,     dq_acc))     return false;
  if (!write_register_16(REG_DP_ACC,     DP_ACC_EZ))  return false;  // 0x0C80

  // ── Step 2.1: Trigger model reload ────────────────────────────────────────
  // 0x8400 selects the high-voltage Li-ion model (charge > 4.275 V).
  // 0x8000 selects the standard Li-ion model (charge ≤ 4.275 V).
  uint16_t model_cfg = charge_voltage_high_
      ? (MODELCFG_REFRESH | MODELCFG_VCHG)  // 0x8400
      : MODELCFG_REFRESH;                   // 0x8000
  ESP_LOGD(TAG, "Writing ModelCFG = 0x%04X", model_cfg);
  if (!write_register_16(REG_MODEL_CFG, model_cfg)) return false;

  // ── Step 2.2: Wait for Refresh bit to clear ───────────────────────────────
  // The IC clears the Refresh bit when the model load is complete.
  // Typically < 500 ms; allow 1 s.
  ESP_LOGD(TAG, "Waiting for ModelCFG.Refresh to clear...");
  if (!wait_for_model_refresh_clear_(1000)) {
    ESP_LOGE(TAG, "Timeout waiting for ModelCFG.Refresh");
    return false;
  }

  // ── Step 3: Restore original HibCFG ──────────────────────────────────────
  if (!write_register_16(REG_HIB_CFG, hib_cfg_saved)) return false;
  ESP_LOGD(TAG, "Restored HibCFG = 0x%04X", hib_cfg_saved);

  // ── Step 4: Clear POR bit in Status ──────────────────────────────────────
  // Read-modify-write: clear only POR (bit 1), leave alert bits intact.
  uint16_t status;
  if (!read_register_16(REG_STATUS, status)) return false;
  status &= ~STATUS_POR;
  if (!write_register_16(REG_STATUS, status)) return false;
  ESP_LOGD(TAG, "POR bit cleared, Status = 0x%04X", status);

  return true;
}

// ──────────────────────────────────────────────────────────────────────────────
// Polling helpers
// ──────────────────────────────────────────────────────────────────────────────

bool MAX17055Component::wait_for_dnr_clear_(uint32_t timeout_ms) {
  uint32_t start = millis();
  while ((millis() - start) < timeout_ms) {
    uint16_t fstat;
    if (!read_register_16(REG_FSTAT, fstat)) return false;
    if (!(fstat & FSTAT_DNR)) return true;
    delay(10);
  }
  ESP_LOGW(TAG, "DNR still set after %u ms", timeout_ms);
  return false;
}

bool MAX17055Component::wait_for_model_refresh_clear_(uint32_t timeout_ms) {
  uint32_t start = millis();
  while ((millis() - start) < timeout_ms) {
    uint16_t cfg;
    if (!read_register_16(REG_MODEL_CFG, cfg)) return false;
    if (!(cfg & MODELCFG_REFRESH)) return true;
    delay(10);
  }
  ESP_LOGW(TAG, "ModelCFG.Refresh still set after %u ms", timeout_ms);
  return false;
}

// ──────────────────────────────────────────────────────────────────────────────
// Register value helpers
// ──────────────────────────────────────────────────────────────────────────────

uint16_t MAX17055Component::design_cap_raw_() const {
  // DesignCap register LSB = capacity_lsb_mah_ mAh
  // = design_capacity_mah_ / (5.0 / rsense_mohm_)
  // = design_capacity_mah_ * rsense_mohm_ / 5
  return (uint16_t)((uint32_t) design_capacity_mah_ * rsense_mohm_ / 5);
}

uint16_t MAX17055Component::ichg_term_raw_() const {
  // IChgTerm register LSB = current_lsb_ma_ mA
  // = charge_term_current_ma_ / (1.5625 / rsense_mohm_)
  // = charge_term_current_ma_ * rsense_mohm_ / 1.5625
  // Multiply by 16/25 (= 1/1.5625) using integer arithmetic to avoid FP in setup.
  return (uint16_t)((uint32_t) charge_term_current_ma_ * rsense_mohm_ * 16 / 25);
}

uint16_t MAX17055Component::pack_vempty_() const {
  // VEmpty register format (datasheet §Register Descriptions / VEmpty):
  //   bits[15:7] = VE  (empty voltage),    10 mV/LSB
  //   bits[ 6:0] = VR  (recovery voltage), 40 mV/LSB
  uint16_t ve = empty_voltage_mv_    / 10;   // 10 mV/LSB
  uint16_t vr = recovery_voltage_mv_ / 40;   // 40 mV/LSB
  return (uint16_t)((ve << 7) | (vr & 0x7F));
}

// ──────────────────────────────────────────────────────────────────────────────
// Low-level I2C helpers
// ──────────────────────────────────────────────────────────────────────────────

bool MAX17055Component::read_register_16(uint8_t reg, uint16_t &value) {
  uint8_t data[2];
  // MAX17055 transmits the low byte first, then the high byte (little-endian).
  i2c::ErrorCode err = this->read_register(reg, data, 2);
  if (err != i2c::ERROR_OK) {
    ESP_LOGW(TAG, "I2C read error reg=0x%02X code=%d", reg, (int) err);
    return false;
  }
  value = (uint16_t) data[0] | ((uint16_t) data[1] << 8);
  return true;
}

bool MAX17055Component::write_register_16(uint8_t reg, uint16_t value) {
  uint8_t data[2] = {(uint8_t)(value & 0xFF), (uint8_t)(value >> 8)};
  i2c::ErrorCode err = this->write_register(reg, data, 2);
  if (err != i2c::ERROR_OK) {
    ESP_LOGW(TAG, "I2C write error reg=0x%02X code=%d", reg, (int) err);
    return false;
  }
  return true;
}

bool MAX17055Component::read_signed_register_16(uint8_t reg, int16_t &value) {
  uint16_t raw;
  if (!read_register_16(reg, raw)) return false;
  value = (int16_t) raw;  // reinterpret as two's-complement signed
  return true;
}

// ──────────────────────────────────────────────────────────────────────────────
// Debug register dump (activated by debug_registers: true)
// ──────────────────────────────────────────────────────────────────────────────
void MAX17055Component::log_debug_registers_() {
  static const struct { uint8_t reg; const char *name; } REGS[] = {
    {REG_STATUS,       "STATUS      "},
    {REG_REP_CAP,      "RepCap      "},
    {REG_REP_SOC,      "RepSOC      "},
    {REG_AGE,          "Age         "},
    {REG_TEMP,         "Temp        "},
    {REG_VCELL,        "VCELL       "},
    {REG_CURRENT,      "Current     "},
    {REG_AVG_CURRENT,  "AvgCurrent  "},
    {REG_AV_SOC,       "AvSOC       "},
    {REG_FULL_CAP_REP, "FullCapRep  "},
    {REG_TTE,          "TTE         "},
    {REG_CYCLES,       "Cycles      "},
    {REG_DESIGN_CAP,   "DesignCap   "},
    {REG_ICHG_TERM,    "IChgTerm    "},
    {REG_TTF,          "TTF         "},
    {REG_FULL_CAP_NOM, "FullCapNom  "},
    {REG_V_EMPTY,      "VEmpty      "},
    {REG_FSTAT,        "FStat       "},
    {REG_HIB_CFG,      "HibCFG      "},
    {REG_MODEL_CFG,    "ModelCFG    "},
  };
  ESP_LOGD(TAG, "--- MAX17055 register dump ---");
  for (const auto &r : REGS) {
    uint16_t v;
    if (read_register_16(r.reg, v)) {
      ESP_LOGD(TAG, "  [0x%02X] %s = 0x%04X (%u)", r.reg, r.name, v, v);
    }
  }
}

}  // namespace max17055
}  // namespace esphome
