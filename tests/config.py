"""Run with the pinned ESPHome Python environment, from the repository root."""
from components.danfoss_eco.climate import validate_command_timeout
from esphome.config_validation import Invalid

for value, expected in [("1ms", 1), ("10min", 600000), ("15min", 900000), ("24h", 86400000)]:
    assert validate_command_timeout(value).total_milliseconds == expected
for value in ["0s", "-1min", "25h", "never"]:
    try:
        validate_command_timeout(value)
    except Invalid:
        pass
    else:
        raise AssertionError(f"Accepted invalid timeout: {value}")
print("Timeout schema: 8 cases passed")
