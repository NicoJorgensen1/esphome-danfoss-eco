import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import climate, ble_client, sensor, binary_sensor, number, switch
from esphome.const import (
    CONF_ID,
    CONF_NAME,
    
    CONF_TEMPERATURE,
    CONF_BATTERY_LEVEL,
    
    CONF_ENTITY_CATEGORY,
    ENTITY_CATEGORY_DIAGNOSTIC,
    
    STATE_CLASS_MEASUREMENT,
    UNIT_PERCENT,
    UNIT_CELSIUS,
    
    CONF_DEVICE_CLASS,
    DEVICE_CLASS_BATTERY,
    DEVICE_CLASS_TEMPERATURE,
    DEVICE_CLASS_PROBLEM
)

CODEOWNERS = ["@dmitry-cherkas"]
DEPENDENCIES = ["ble_client"]
# load zero-configuration dependencies automatically
AUTO_LOAD = ["sensor", "binary_sensor", "esp32_ble_tracker", "number", "switch"]

CONF_PIN_CODE = 'pin_code'
CONF_SECRET_KEY = 'secret_key'
CONF_PROBLEMS = 'problems'
CONF_TEMPERATURE_MIN = 'temperature_min'
CONF_TEMPERATURE_MAX = 'temperature_max'
CONF_FROST_PROTECTION_TEMPERATURE = 'frost_protection_temperature'
CONF_VACATION_TEMPERATURE = 'vacation_temperature'
CONF_CHILD_SAFETY = 'child_safety'
CONF_ADAPTIVE_LEARNING = 'adaptive_learning'
CONF_MAC_ADDRESS = 'mac_address'
CONF_HARDWARE_REVISION = 'hardware_revision'
CONF_FIRMWARE_REVISION = 'firmware_revision'

eco_ns = cg.esphome_ns.namespace("danfoss_eco")
DanfossEco = eco_ns.class_(
    "Device", climate.Climate, ble_client.BLEClientNode, cg.PollingComponent
)

def validate_secret(value):
    value = cv.string_strict(value)
    if len(value) != 32:
        raise cv.Invalid("Secret key should be exactly 16 bytes (32 chars)")
    return value

def validate_pin(value):
    value = cv.string_strict(value)
    if len(value) != 4:
        raise cv.Invalid("PIN code should be exactly 4 chars")
    if not value.isnumeric():
        raise cv.Invalid("PIN code should be numeric")
    return value

CONFIG_SCHEMA = (
    climate.climate_schema(DanfossEco).extend(
        {
            cv.GenerateID(): cv.declare_id(DanfossEco),
            cv.Optional(CONF_SECRET_KEY): validate_secret,
            cv.Optional(CONF_PIN_CODE): validate_pin,
            cv.Optional(CONF_BATTERY_LEVEL): sensor.sensor_schema(
                unit_of_measurement=UNIT_PERCENT,
                accuracy_decimals=0,
                device_class=DEVICE_CLASS_BATTERY,
                state_class=STATE_CLASS_MEASUREMENT,
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC
            ),
            cv.Optional(CONF_TEMPERATURE): sensor.sensor_schema(
                unit_of_measurement=UNIT_CELSIUS,
                accuracy_decimals=1,
                device_class=DEVICE_CLASS_TEMPERATURE,
                state_class=STATE_CLASS_MEASUREMENT,
            ),
            cv.Optional(CONF_PROBLEMS): binary_sensor.binary_sensor_schema().extend({
                cv.Optional(CONF_NAME): cv.string,
                cv.Optional(CONF_ENTITY_CATEGORY, default=ENTITY_CATEGORY_DIAGNOSTIC): cv.entity_category,
                cv.Optional(CONF_DEVICE_CLASS, default=DEVICE_CLASS_PROBLEM): binary_sensor.validate_device_class
            }),
            cv.Optional(CONF_TEMPERATURE_MIN): number.number_schema(
                number.Number,
                unit_of_measurement=UNIT_CELSIUS,
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC
            ),
            cv.Optional(CONF_TEMPERATURE_MAX): number.number_schema(
                number.Number,
                unit_of_measurement=UNIT_CELSIUS,
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC
            ),
            cv.Optional(CONF_FROST_PROTECTION_TEMPERATURE): number.number_schema(
                number.Number,
                unit_of_measurement=UNIT_CELSIUS,
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC
            ),
            cv.Optional(CONF_VACATION_TEMPERATURE): number.number_schema(
                number.Number,
                unit_of_measurement=UNIT_CELSIUS,
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC
            ),
            cv.Optional(CONF_CHILD_SAFETY): switch.switch_schema(
                switch.Switch,
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC
            ),
            cv.Optional(CONF_ADAPTIVE_LEARNING): switch.switch_schema(
                switch.Switch,
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC
            ),
            cv.Optional(CONF_MAC_ADDRESS): sensor.sensor_schema(
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC
            ),
            cv.Optional(CONF_HARDWARE_REVISION): sensor.sensor_schema(
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC
            ),
            cv.Optional(CONF_FIRMWARE_REVISION): sensor.sensor_schema(
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC
            ),
        }
    )
    .extend(ble_client.BLE_CLIENT_SCHEMA)
    .extend(cv.polling_component_schema("60s"))
)

async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await climate.register_climate(var, config)
    await ble_client.register_ble_node(var, config)
    
    cg.add(var.set_secret_key(config.get(CONF_SECRET_KEY, "")))
    cg.add(var.set_pin_code(config.get(CONF_PIN_CODE, "")))
    
    if CONF_BATTERY_LEVEL in config:
        sens = await sensor.new_sensor(config[CONF_BATTERY_LEVEL])
        cg.add(var.set_battery_level(sens))
    if CONF_TEMPERATURE in config:
        sens = await sensor.new_sensor(config[CONF_TEMPERATURE])
        cg.add(var.set_temperature(sens))
    if CONF_PROBLEMS in config:
        b_sens = await binary_sensor.new_binary_sensor(config[CONF_PROBLEMS])
        cg.add(var.set_problems(b_sens))
    
    if CONF_TEMPERATURE_MIN in config:
        num = await number.new_number(config[CONF_TEMPERATURE_MIN], min_value=5.0, max_value=30.0, step=0.5)
        cg.add(var.set_temperature_min(num))
        templ = cg.RawExpression("[{}](float value) {{ {}->write_temperature_min(value); }}".format(var, var))
        cg.add(num.add_on_state_callback(templ))
    
    if CONF_TEMPERATURE_MAX in config:
        num = await number.new_number(config[CONF_TEMPERATURE_MAX], min_value=5.0, max_value=30.0, step=0.5)
        cg.add(var.set_temperature_max(num))
        templ = cg.RawExpression("[{}](float value) {{ {}->write_temperature_max(value); }}".format(var, var))
        cg.add(num.add_on_state_callback(templ))
    
    if CONF_FROST_PROTECTION_TEMPERATURE in config:
        num = await number.new_number(config[CONF_FROST_PROTECTION_TEMPERATURE], min_value=5.0, max_value=10.0, step=0.5)
        cg.add(var.set_frost_protection_temperature(num))
        templ = cg.RawExpression("[{}](float value) {{ {}->write_frost_protection_temperature(value); }}".format(var, var))
        cg.add(num.add_on_state_callback(templ))
    
    if CONF_VACATION_TEMPERATURE in config:
        num = await number.new_number(config[CONF_VACATION_TEMPERATURE], min_value=5.0, max_value=20.0, step=0.5)
        cg.add(var.set_vacation_temperature(num))
        templ = cg.RawExpression("[{}](float value) {{ {}->write_vacation_temperature(value); }}".format(var, var))
        cg.add(num.add_on_state_callback(templ))
    
    if CONF_CHILD_SAFETY in config:
        sw = await switch.new_switch(config[CONF_CHILD_SAFETY])
        cg.add(var.set_child_safety(sw))
        templ = cg.RawExpression("[{}](bool value) {{ {}->write_child_safety(value); }}".format(var, var))
        cg.add(sw.add_on_state_callback(templ))
    
    if CONF_ADAPTIVE_LEARNING in config:
        sw = await switch.new_switch(config[CONF_ADAPTIVE_LEARNING])
        cg.add(var.set_adaptive_learning(sw))
        templ = cg.RawExpression("[{}](bool value) {{ {}->write_adaptive_learning(value); }}".format(var, var))
        cg.add(sw.add_on_state_callback(templ))
    
    if CONF_MAC_ADDRESS in config:
        sens = await sensor.new_sensor(config[CONF_MAC_ADDRESS])
        cg.add(var.set_mac_address(sens))
    
    if CONF_HARDWARE_REVISION in config:
        sens = await sensor.new_sensor(config[CONF_HARDWARE_REVISION])
        cg.add(var.set_hardware_revision(sens))
    
    if CONF_FIRMWARE_REVISION in config:
        sens = await sensor.new_sensor(config[CONF_FIRMWARE_REVISION])
        cg.add(var.set_firmware_revision(sens))
    