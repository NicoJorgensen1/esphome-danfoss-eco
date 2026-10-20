"""Validate the public setup-timeout option using ESPHome's real schema."""
from components.danfoss_eco.climate import CONFIG_SCHEMA
from esphome.config_validation import Invalid
from esphome.core import CORE

base = {"name": "Test Thermostat", "ble_client_id": "eco_client"}
def validate(config):
    CORE.reset()
    return CONFIG_SCHEMA(config)

for value, expected in [("1s", 1000), ("150s", 150000), ("10min", 600000)]:
    config = validate({**base, "connection_setup_timeout": value})
    assert config["connection_setup_timeout"].total_milliseconds == expected
assert validate(base)["connection_setup_timeout"].total_milliseconds == 60000
for value in ["0s", "1ms", "-1s", "11min", "never"]:
    try:
        validate({**base, "connection_setup_timeout": value})
    except Invalid:
        pass
    else:
        raise AssertionError(f"Accepted invalid setup timeout: {value}")
print("Connection setup timeout schema: 9 cases passed")
