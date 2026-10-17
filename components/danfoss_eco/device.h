#pragma once

#include <set>
#include <map>

#include "esphome/components/ble_client/ble_client.h"
#include "esphome/components/climate/climate.h"

#include "esphome/core/preferences.h"
#include "esphome/components/text_sensor/text_sensor.h"

#include "helpers.h"
#include "command.h"
#include "properties.h"
#include "my_component.h"
#include "xxtea.h"

#ifdef USE_ESP32

#include <esp_gattc_api.h>

namespace esphome
{
  namespace danfoss_eco
  {
    using namespace std;
    using namespace climate;

    class Device : public MyComponent, public esphome::ble_client::BLEClientNode
    {
    public:
      Device() : xxtea(make_shared<Xxtea>()){};

      void dump_config() override
      {
        LOG_CLIMATE("", "Danfoss Eco eTRV", this);
        ESP_LOGCONFIG(TAG, "  MAC Address: %s", this->parent()->address_str());
        LOG_SENSOR("", "Battery Level", this->battery_level_);
        LOG_SENSOR("", "Room Temperature", this->temperature_);
        LOG_BINARY_SENSOR("", "Problems", this->problems_);
      }

      void setup() override;
      void loop() override;
      void update() override;
      void gattc_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if, esp_ble_gattc_cb_param_t *param) override;

      void set_secret_key(uint8_t *, bool) override;

      void set_secret_key(const string &);
      void set_pin_code(const string &);
      void set_command_timeout(uint32_t timeout) { command_timeout_ = timeout; }
      void set_command_status(text_sensor::TextSensor *sensor) { command_status_ = sensor; }

    protected:
      void control(const ClimateCall &call) override;

      void connect();
      void disconnect();

      void write_pin();
      void on_write_pin(esp_ble_gattc_cb_param_t::gattc_write_evt_param);

      void on_read(esp_ble_gattc_cb_param_t::gattc_read_char_evt_param);
      void on_write(esp_ble_gattc_cb_param_t::gattc_write_evt_param);

      shared_ptr<Xxtea> xxtea;

      shared_ptr<WritableProperty> p_pin{nullptr};
      shared_ptr<BatteryProperty> p_battery{nullptr};
      shared_ptr<TemperatureProperty> p_temperature{nullptr};
      shared_ptr<SettingsProperty> p_settings{nullptr};
      shared_ptr<ErrorsProperty> p_errors{nullptr};
      shared_ptr<SecretKeyProperty> p_secret_key{nullptr};

      set<shared_ptr<DeviceProperty>> properties{nullptr};

    private:
      ESPPreferenceObject secret_pref_;
      uint32_t pin_code_ = 0;

      void fail_session(const char *reason);
      void start_work();
      void accept_control(std::unique_ptr<Command> command);
      bool current_control(const Command &command) const;
      bool expire_controls(bool publish = true);
      void publish_command_status();
      struct PendingControl { uint64_t id; uint32_t started; };
      std::map<const DeviceProperty *, PendingControl> pending_controls_;
      uint64_t next_delivery_id_{0};
      uint32_t command_timeout_{900000};
      text_sensor::TextSensor *command_status_{nullptr};
      const char *last_command_status_{nullptr};
      bool delivery_seen_{false};
      bool delivery_failed_{false};
      bool response_matches(bool write, uint16_t handle) const;
      std::unique_ptr<Command> in_flight_;
      bool request_pending_{false};
      bool recovering_{false};
      bool batch_active_{false};
      bool closing_{false};
      bool pin_pending_{false};
      uint8_t retries_{0};
      CommandQueue commands_;
    };

  } // namespace danfoss_eco
} // namespace esphome

#endif // USE_ESP32
