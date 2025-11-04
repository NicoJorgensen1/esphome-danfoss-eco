#pragma once

#include "esphome/core/component.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/number/number.h"
#include "esphome/components/switch/switch.h"
#include "esphome/components/climate/climate.h"

#include "helpers.h"

namespace esphome
{
    namespace danfoss_eco
    {
        using namespace std;
        using namespace esphome::climate;
        using namespace esphome::sensor;
        using namespace esphome::binary_sensor;
        using namespace esphome::number;
        using namespace esphome::switch_;

        class MyComponent : public Climate, public PollingComponent, public enable_shared_from_this<MyComponent>
        {
        public:
            float get_setup_priority() const override { return setup_priority::DATA; }

            ClimateTraits traits() override
            {
                auto traits = ClimateTraits();
                traits.set_supports_current_temperature(true);

                traits.set_supported_modes(set<ClimateMode>({ClimateMode::CLIMATE_MODE_HEAT, ClimateMode::CLIMATE_MODE_AUTO, ClimateMode::CLIMATE_MODE_OFF}));
                set<ClimatePreset> presets = {ClimatePreset::CLIMATE_PRESET_HOME, ClimatePreset::CLIMATE_PRESET_AWAY, ClimatePreset::CLIMATE_PRESET_SLEEP};
                traits.set_supported_presets(presets);
                traits.set_visual_temperature_step(0.5);

                traits.set_supports_current_temperature(true); // supports reporting current temperature
                traits.set_supports_action(true);              // supports reporting current action
                return traits;
            }

            void set_battery_level(Sensor *battery_level) { battery_level_ = battery_level; }
            void set_temperature(Sensor *temperature) { temperature_ = temperature; }
            void set_problems(BinarySensor *problems) { problems_ = problems; }
            
            void set_temperature_min(Number *temperature_min) { temperature_min_ = temperature_min; }
            void set_temperature_max(Number *temperature_max) { temperature_max_ = temperature_max; }
            void set_frost_protection_temperature(Number *frost_protection_temperature) { frost_protection_temperature_ = frost_protection_temperature; }
            void set_vacation_temperature(Number *vacation_temperature) { vacation_temperature_ = vacation_temperature; }
            
            void set_child_safety(Switch *child_safety) { child_safety_ = child_safety; }
            void set_adaptive_learning(Switch *adaptive_learning) { adaptive_learning_ = adaptive_learning; }
            
            void set_mac_address(Sensor *mac_address) { mac_address_ = mac_address; }
            void set_hardware_revision(Sensor *hardware_revision) { hardware_revision_ = hardware_revision; }
            void set_firmware_revision(Sensor *firmware_revision) { firmware_revision_ = firmware_revision; }

            Sensor *battery_level() { return this->battery_level_; }
            Sensor *temperature() { return this->temperature_; }
            BinarySensor *problems() { return this->problems_; }
            
            Number *temperature_min() { return this->temperature_min_; }
            Number *temperature_max() { return this->temperature_max_; }
            Number *frost_protection_temperature() { return this->frost_protection_temperature_; }
            Number *vacation_temperature() { return this->vacation_temperature_; }
            
            Switch *child_safety() { return this->child_safety_; }
            Switch *adaptive_learning() { return this->adaptive_learning_; }
            
            Sensor *mac_address() { return this->mac_address_; }
            Sensor *hardware_revision() { return this->hardware_revision_; }
            Sensor *firmware_revision() { return this->firmware_revision_; }

            virtual void set_secret_key(uint8_t *, bool) = 0;

        protected:
            Sensor *battery_level_{nullptr};
            Sensor *temperature_{nullptr};
            BinarySensor *problems_{nullptr};
            
            Number *temperature_min_{nullptr};
            Number *temperature_max_{nullptr};
            Number *frost_protection_temperature_{nullptr};
            Number *vacation_temperature_{nullptr};
            
            Switch *child_safety_{nullptr};
            Switch *adaptive_learning_{nullptr};
            
            Sensor *mac_address_{nullptr};
            Sensor *hardware_revision_{nullptr};
            Sensor *firmware_revision_{nullptr};
        };

    } // namespace danfoss_eco
} // namespace esphome