"""MAX17055 ModelGauge m5 EZ Fuel Gauge – ESPHome external component."""
# The component class and shared constants are defined here;
# sensor platform configuration lives in sensor.py.

import esphome.codegen as cg
from esphome.components import i2c

CODEOWNERS = ["@ceear"]
DEPENDENCIES = ["i2c"]
AUTO_LOAD = ["sensor"]

max17055_ns = cg.esphome_ns.namespace("max17055")
MAX17055Component = max17055_ns.class_(
    "MAX17055Component", cg.PollingComponent, i2c.I2CDevice
)
