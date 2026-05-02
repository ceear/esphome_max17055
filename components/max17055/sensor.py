"""MAX17055 sensor platform – validates YAML and generates C++ setup code."""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor, i2c
from esphome.const import (
    CONF_ID,
    CONF_ADDRESS,
    CONF_UPDATE_INTERVAL,
    DEVICE_CLASS_BATTERY,
    DEVICE_CLASS_CURRENT,
    DEVICE_CLASS_TEMPERATURE,
    DEVICE_CLASS_VOLTAGE,
    DEVICE_CLASS_DURATION,
    STATE_CLASS_MEASUREMENT,
    STATE_CLASS_TOTAL_INCREASING,
    UNIT_AMPERE,
    UNIT_CELSIUS,
    UNIT_HOUR,
    UNIT_PERCENT,
    UNIT_VOLT,
    ICON_EMPTY,
)
from . import MAX17055Component, max17055_ns

DEPENDENCIES = ["i2c"]

# ── Custom units not in esphome.const ────────────────────────────────────────
UNIT_MILLIAMPERE_HOUR = "mAh"

# ── Battery chemistry values ──────────────────────────────────────────────────
# These are the only chemistries whose OCV curves are embedded in the
# MAX17055 ModelGauge m5 EZ pre-loaded models (datasheet / UG6595).
CHEMISTRY_LI_ION    = "li_ion"     # standard Li-ion / LiPo, charge ≤ 4.2 V
CHEMISTRY_LI_ION_HV = "li_ion_hv"  # high-voltage Li-ion (NMC/NCA), charge ≤ 4.35 V
# The following chemistry is recognised in YAML so we can emit a clear error:
CHEMISTRY_LIFEPO4   = "lifepo4"

_SUPPORTED_CHEMISTRIES = [CHEMISTRY_LI_ION, CHEMISTRY_LI_ION_HV]
_ALL_CHEMISTRIES       = _SUPPORTED_CHEMISTRIES + [CHEMISTRY_LIFEPO4]

# ── YAML config key constants ─────────────────────────────────────────────────
CONF_BATTERY_CHEMISTRY        = "battery_chemistry"
CONF_BATTERY_VOLTAGE          = "battery_voltage"
CONF_CURRENT                  = "current"
CONF_AVG_CURRENT              = "average_current"
CONF_STATE_OF_CHARGE          = "state_of_charge"
CONF_AVG_STATE_OF_CHARGE      = "average_state_of_charge"
CONF_REMAINING_CAPACITY       = "remaining_capacity"
CONF_FULL_CAPACITY            = "full_capacity"
CONF_TIME_TO_EMPTY            = "time_to_empty"
CONF_TIME_TO_FULL             = "time_to_full"
CONF_TEMPERATURE              = "temperature"
CONF_CYCLE_COUNT              = "cycle_count"
CONF_AGE                      = "age"

CONF_DESIGN_CAPACITY_MAH      = "design_capacity_mah"
CONF_CHARGE_TERM_CURRENT_MA   = "charge_termination_current_ma"
CONF_EMPTY_VOLTAGE_MV         = "empty_voltage_mv"
CONF_RECOVERY_VOLTAGE_MV      = "recovery_voltage_mv"
CONF_RSENSE_MOHM              = "sense_resistor_mohm"
CONF_SKIP_INITIALIZATION      = "skip_initialization"
CONF_FORCE_INIT               = "force_init"
CONF_DEBUG_REGISTERS          = "debug_registers"
CONF_ENABLE_SLEEP_MODE        = "enable_sleep_mode"


# ── Per-field validators ──────────────────────────────────────────────────────

def _validate_chemistry(value):
    """Accept supported chemistries; reject known-unsupported ones with a clear message."""
    value = str(value).lower()
    if value == CHEMISTRY_LIFEPO4:
        raise cv.Invalid(
            "LiFePO4 / LFP is not supported by the MAX17055 ModelGauge m5 EZ "
            "algorithm.  The EZ config provides only two pre-loaded OCV models: "
            "'li_ion' (standard Li-ion / LiPo, charge ≤ 4.2 V) and 'li_ion_hv' "
            "(high-voltage Li-ion e.g. NMC/NCA, charge ≤ 4.35 V).  LFP has a "
            "fundamentally different, very flat OCV curve — using either Li-ion "
            "model would produce severely wrong SoC readings across most of the "
            "useful state-of-charge range.  Supporting LFP requires custom model "
            "loading (writing the OCV table to the chip), which is not "
            "implemented in this component."
        )
    return cv.one_of(*_SUPPORTED_CHEMISTRIES, lower=True)(value)


# ── Cross-field validators ────────────────────────────────────────────────────

def _validate_rsense_for_current_sensors(config):
    """Error if current/capacity sensors are requested without a sense resistor."""
    needs_rsense = (
        CONF_CURRENT in config
        or CONF_AVG_CURRENT in config
        or CONF_REMAINING_CAPACITY in config
        or CONF_FULL_CAPACITY in config
    )
    if needs_rsense and config.get(CONF_RSENSE_MOHM, 0) == 0:
        raise cv.Invalid(
            "current, average_current, remaining_capacity, and full_capacity "
            "require a correctly placed sense resistor.  Set sense_resistor_mohm "
            "to the actual shunt value (e.g. 10 for 10 mΩ).  Without a shunt in "
            "the charge/discharge path these values will be incorrect."
        )
    return config


# ── Main schema ───────────────────────────────────────────────────────────────

CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(MAX17055Component),

            # ── Battery chemistry ──────────────────────────────────────────
            # Determines which pre-loaded OCV model the MAX17055 uses.
            # li_ion    → ModelCFG = 0x8000  (standard Li-ion / LiPo, ≤ 4.2 V)
            # li_ion_hv → ModelCFG = 0x8400  (high-voltage Li-ion, ≤ 4.35 V)
            cv.Optional(CONF_BATTERY_CHEMISTRY, default=CHEMISTRY_LI_ION):
                _validate_chemistry,

            # ── Hardware parameters ────────────────────────────────────────
            cv.Optional(CONF_DESIGN_CAPACITY_MAH, default=3000):
                cv.int_range(min=1, max=32767),
            cv.Optional(CONF_CHARGE_TERM_CURRENT_MA, default=200):
                cv.int_range(min=1, max=32767),
            cv.Optional(CONF_EMPTY_VOLTAGE_MV, default=3300):
                cv.int_range(min=0, max=5120),
            cv.Optional(CONF_RECOVERY_VOLTAGE_MV, default=3880):
                cv.int_range(min=0, max=5120),
            cv.Optional(CONF_RSENSE_MOHM, default=10):
                cv.int_range(min=1, max=1000),

            # ── Behaviour flags ────────────────────────────────────────────
            cv.Optional(CONF_SKIP_INITIALIZATION, default=False): cv.boolean,
            cv.Optional(CONF_FORCE_INIT,          default=False): cv.boolean,
            cv.Optional(CONF_DEBUG_REGISTERS,     default=False): cv.boolean,
            cv.Optional(CONF_ENABLE_SLEEP_MODE,   default=False): cv.boolean,

            # ── Sensors (all optional) ─────────────────────────────────────
            cv.Optional(CONF_BATTERY_VOLTAGE): sensor.sensor_schema(
                unit_of_measurement=UNIT_VOLT,
                accuracy_decimals=3,
                device_class=DEVICE_CLASS_VOLTAGE,
                state_class=STATE_CLASS_MEASUREMENT,
                icon="mdi:battery",
            ),
            cv.Optional(CONF_CURRENT): sensor.sensor_schema(
                unit_of_measurement=UNIT_AMPERE,
                accuracy_decimals=3,
                device_class=DEVICE_CLASS_CURRENT,
                state_class=STATE_CLASS_MEASUREMENT,
                icon="mdi:current-dc",
            ),
            cv.Optional(CONF_AVG_CURRENT): sensor.sensor_schema(
                unit_of_measurement=UNIT_AMPERE,
                accuracy_decimals=3,
                device_class=DEVICE_CLASS_CURRENT,
                state_class=STATE_CLASS_MEASUREMENT,
                icon="mdi:current-dc",
            ),
            cv.Optional(CONF_STATE_OF_CHARGE): sensor.sensor_schema(
                unit_of_measurement=UNIT_PERCENT,
                accuracy_decimals=1,
                device_class=DEVICE_CLASS_BATTERY,
                state_class=STATE_CLASS_MEASUREMENT,
                icon=ICON_EMPTY,
            ),
            cv.Optional(CONF_AVG_STATE_OF_CHARGE): sensor.sensor_schema(
                unit_of_measurement=UNIT_PERCENT,
                accuracy_decimals=1,
                device_class=DEVICE_CLASS_BATTERY,
                state_class=STATE_CLASS_MEASUREMENT,
                icon=ICON_EMPTY,
            ),
            cv.Optional(CONF_REMAINING_CAPACITY): sensor.sensor_schema(
                unit_of_measurement=UNIT_MILLIAMPERE_HOUR,
                accuracy_decimals=1,
                device_class=DEVICE_CLASS_BATTERY,
                state_class=STATE_CLASS_MEASUREMENT,
                icon="mdi:battery-charging",
            ),
            cv.Optional(CONF_FULL_CAPACITY): sensor.sensor_schema(
                unit_of_measurement=UNIT_MILLIAMPERE_HOUR,
                accuracy_decimals=1,
                device_class=DEVICE_CLASS_BATTERY,
                state_class=STATE_CLASS_MEASUREMENT,
                icon="mdi:battery-charging-100",
            ),
            cv.Optional(CONF_TIME_TO_EMPTY): sensor.sensor_schema(
                unit_of_measurement=UNIT_HOUR,
                accuracy_decimals=2,
                device_class=DEVICE_CLASS_DURATION,
                state_class=STATE_CLASS_MEASUREMENT,
                icon="mdi:battery-clock",
            ),
            cv.Optional(CONF_TIME_TO_FULL): sensor.sensor_schema(
                unit_of_measurement=UNIT_HOUR,
                accuracy_decimals=2,
                device_class=DEVICE_CLASS_DURATION,
                state_class=STATE_CLASS_MEASUREMENT,
                icon="mdi:battery-charging-100",
            ),
            cv.Optional(CONF_TEMPERATURE): sensor.sensor_schema(
                unit_of_measurement=UNIT_CELSIUS,
                accuracy_decimals=1,
                device_class=DEVICE_CLASS_TEMPERATURE,
                state_class=STATE_CLASS_MEASUREMENT,
                icon="mdi:thermometer",
            ),
            cv.Optional(CONF_CYCLE_COUNT): sensor.sensor_schema(
                unit_of_measurement="cycles",
                accuracy_decimals=2,
                state_class=STATE_CLASS_TOTAL_INCREASING,
                icon="mdi:counter",
            ),
            cv.Optional(CONF_AGE): sensor.sensor_schema(
                unit_of_measurement=UNIT_PERCENT,
                accuracy_decimals=1,
                state_class=STATE_CLASS_MEASUREMENT,
                icon="mdi:battery-heart-variant",
            ),
        }
    )
    .extend(cv.polling_component_schema("60s"))
    .extend(i2c.i2c_device_schema(0x36)),
    _validate_rsense_for_current_sensors,
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await i2c.register_i2c_device(var, config)

    # ── Hardware parameters ──────────────────────────────────────────────────
    cg.add(var.set_design_capacity(config[CONF_DESIGN_CAPACITY_MAH]))
    cg.add(var.set_charge_term_current(config[CONF_CHARGE_TERM_CURRENT_MA]))
    cg.add(var.set_empty_voltage(config[CONF_EMPTY_VOLTAGE_MV]))
    cg.add(var.set_recovery_voltage(config[CONF_RECOVERY_VOLTAGE_MV]))
    cg.add(var.set_rsense_mohm(config[CONF_RSENSE_MOHM]))

    # Map chemistry string → charge_voltage_high bool for the C++ layer
    chemistry = config[CONF_BATTERY_CHEMISTRY]
    cg.add(var.set_charge_voltage_high(chemistry == CHEMISTRY_LI_ION_HV))

    # ── Behaviour flags ──────────────────────────────────────────────────────
    cg.add(var.set_skip_initialization(config[CONF_SKIP_INITIALIZATION]))
    cg.add(var.set_force_init(config[CONF_FORCE_INIT]))
    cg.add(var.set_debug_registers(config[CONF_DEBUG_REGISTERS]))
    cg.add(var.set_enable_sleep_mode(config[CONF_ENABLE_SLEEP_MODE]))

    # ── Sensors ──────────────────────────────────────────────────────────────
    _SENSOR_MAP = [
        (CONF_BATTERY_VOLTAGE,     "set_voltage_sensor"),
        (CONF_CURRENT,             "set_current_sensor"),
        (CONF_AVG_CURRENT,         "set_avg_current_sensor"),
        (CONF_STATE_OF_CHARGE,     "set_rep_soc_sensor"),
        (CONF_AVG_STATE_OF_CHARGE, "set_av_soc_sensor"),
        (CONF_REMAINING_CAPACITY,  "set_rep_cap_sensor"),
        (CONF_FULL_CAPACITY,       "set_full_cap_sensor"),
        (CONF_TIME_TO_EMPTY,       "set_tte_sensor"),
        (CONF_TIME_TO_FULL,        "set_ttf_sensor"),
        (CONF_TEMPERATURE,         "set_temperature_sensor"),
        (CONF_CYCLE_COUNT,         "set_cycles_sensor"),
        (CONF_AGE,                 "set_age_sensor"),
    ]
    for conf_key, setter in _SENSOR_MAP:
        if conf_key in config:
            sens = await sensor.new_sensor(config[conf_key])
            cg.add(getattr(var, setter)(sens))
