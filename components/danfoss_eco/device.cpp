#include "device.h"
#include <cmath>
#include "esphome/core/hal.h"

#ifdef USE_ESP32

namespace esphome
{
  namespace danfoss_eco
  {
    void Device::setup()
    {
      shared_ptr<MyComponent> sp_this(this);

      this->p_pin = make_shared<WritableProperty>(sp_this, xxtea, SERVICE_SETTINGS, CHARACTERISTIC_PIN);
      this->p_battery = make_shared<BatteryProperty>(sp_this, xxtea);
      this->p_temperature = make_shared<TemperatureProperty>(sp_this, xxtea);
      this->p_settings = make_shared<SettingsProperty>(sp_this, xxtea);
      this->p_errors = make_shared<ErrorsProperty>(sp_this, xxtea);
      this->p_secret_key = make_shared<SecretKeyProperty>(sp_this, xxtea);

      this->properties = {this->p_pin, this->p_battery, this->p_temperature, this->p_settings, this->p_errors, this->p_secret_key};
      // pretend, we have already discovered the device
      copy_address(this->parent()->get_address(), this->parent()->get_remote_bda());
      this->parent()->set_state(ClientState::INIT);
      this->publish_command_status();
    }

    void Device::loop()
    {
      this->expire_controls();
      if (this->node_state != ClientState::ESTABLISHED || this->request_pending_ || this->recovering_)
        return;
      if (this->in_flight_ && !this->current_control(*this->in_flight_))
        this->in_flight_.reset();
      if (!this->in_flight_)
        this->in_flight_ = this->commands_.pop();
      if (!this->in_flight_)
      {
        this->batch_active_ = false;
        this->cancel_timeout("session");
        this->disconnect();
        return;
      }
      if (!this->in_flight_->execute(this->parent()))
      {
        this->fail_session("request rejected by Bluetooth stack");
        return;
      }
      this->request_pending_ = true;
      this->set_timeout("request", 15000, [this]() { this->fail_session("response timed out"); });
    }

    void Device::start_work()
    {
      if (!this->batch_active_)
      {
        this->batch_active_ = true;
        this->retries_ = 0;
        if (this->node_state != ClientState::ESTABLISHED)
          this->set_timeout("session", 60000, [this]() { this->fail_session("connection setup timed out"); });
      }
      if (!this->recovering_)
        this->connect();
    }

    void Device::update()
    {
      if (this->xxtea->status() == XXTEA_STATUS_SUCCESS)
      {
        this->commands_.push(new Command(CommandType::READ, this->p_battery));
        this->commands_.push(new Command(CommandType::READ, this->p_temperature));
        this->commands_.push(new Command(CommandType::READ, this->p_settings));
        this->commands_.push(new Command(CommandType::READ, this->p_errors));
      }
      this->start_work();
    }

    void Device::control(const ClimateCall &call)
    {
      // Validate the entire call before queuing either setting.
      if (call.get_target_temperature().has_value())
      {
        const float target = *call.get_target_temperature();
        if (!std::isfinite(target) || target < 5.0f || target > 35.0f || target * 2 != std::floor(target * 2))
        {
          ESP_LOGW(TAG, "[%s] invalid target temperature", this->get_name().c_str());
          this->status_set_warning();
          this->delivery_seen_ = this->delivery_failed_ = true;
          this->publish_command_status();
          return;
        }
      }
      if (call.get_mode().has_value())
      {
        const auto mode = *call.get_mode();
        if (mode != CLIMATE_MODE_AUTO && mode != CLIMATE_MODE_HEAT)
        {
          this->status_set_warning();
          this->delivery_seen_ = this->delivery_failed_ = true;
          this->publish_command_status();
          return;
        }
      }
      if (!call.get_target_temperature().has_value() && !call.get_mode().has_value())
        return;
      // Publish only after this request is accepted: a synchronous status
      // callback may submit a newer request which must win over this one.
      this->expire_controls(false);
      this->delivery_seen_ = true;
      this->delivery_failed_ = false;
      if (call.get_target_temperature().has_value())
        this->accept_control(std::make_unique<Command>(this->p_temperature, *call.get_target_temperature()));
      if (call.get_mode().has_value())
        this->accept_control(std::make_unique<Command>(this->p_settings, *call.get_mode()));
      this->publish_command_status();
      // HA reports the last observed state until the thermostat readback arrives.
      this->start_work();
    }

    void Device::accept_control(std::unique_ptr<Command> command)
    {
      command->delivery_id = ++this->next_delivery_id_;
      this->pending_controls_[command->property.get()] = {command->delivery_id, millis()};
      this->commands_.push(std::move(command));
    }

    bool Device::current_control(const Command &command) const
    {
      if (command.type == CommandType::READ)
        return true;
      const auto it = this->pending_controls_.find(command.property.get());
      return it != this->pending_controls_.end() && it->second.id == command.delivery_id;
    }

    void Device::publish_command_status()
    {
      const char *state = !this->pending_controls_.empty() ? "pending" :
                          this->delivery_failed_ ? "failed" : this->delivery_seen_ ? "confirmed" : "unknown";
      if (this->last_command_status_ == nullptr || strcmp(state, this->last_command_status_) != 0)
      {
        this->last_command_status_ = state;
        if (this->command_status_ != nullptr)
          this->command_status_->publish_state(state);
      }
    }

    bool Device::expire_controls(bool publish)
    {
      bool expired = false;
      const uint32_t now = millis();
      for (auto it = this->pending_controls_.begin(); it != this->pending_controls_.end();)
      {
        if (uint32_t(now - it->second.started) < this->command_timeout_)
        {
          ++it;
          continue;
        }
        this->commands_.remove_write(it->first);
        it = this->pending_controls_.erase(it);
        expired = true;
      }
      if (!expired)
        return false;
      ESP_LOGW(TAG, "[%s] command delivery deadline expired without confirmation", this->get_name().c_str());
      this->delivery_failed_ = true;
      this->status_set_warning();
      // Drain the old connection before sending anything else. Late replies
      // cannot revive expired intent or be mistaken for a new transaction.
      this->cancel_timeout("request");
      this->cancel_timeout("pin");
      this->cancel_timeout("session");
      this->cancel_timeout("retry");
      this->cancel_timeout("reconnect");
      this->request_pending_ = this->pin_pending_ = this->recovering_ = false;
      if (this->in_flight_ && this->current_control(*this->in_flight_))
        this->commands_.retry(std::move(this->in_flight_));
      this->in_flight_.reset();
      this->batch_active_ = false;
      this->disconnect();
      // control() will restart after accepting its replacement. Do not enter
      // connect() (which can expire another setting) in the middle of that call.
      if (publish && !this->commands_.is_empty())
        this->start_work();
      if (publish)
        this->publish_command_status();
      return true;
    }

    bool Device::response_matches(bool write, uint16_t handle) const
    {
      return this->request_pending_ && this->in_flight_ &&
             this->in_flight_->is_write_step() == write && this->in_flight_->property->handle == handle;
    }

    void Device::fail_session(const char *reason)
    {
      if (this->expire_controls())
        return;
      if (this->recovering_)
        return;
      ESP_LOGW(TAG, "[%s] %s", this->get_name().c_str(), reason);
      this->status_set_warning();
      this->cancel_timeout("request");
      this->cancel_timeout("pin");
      this->cancel_timeout("session");
      this->pin_pending_ = false;
      this->cancel_timeout("reconnect");
      this->request_pending_ = false;
      if (this->in_flight_ && this->current_control(*this->in_flight_))
        this->commands_.retry(std::move(this->in_flight_));
      this->in_flight_.reset();
      this->recovering_ = true;
      this->disconnect();
      if (this->retries_ < 6)
        ++this->retries_;
      if (this->retries_ > 4 && this->pending_controls_.empty())
      {
        ESP_LOGW(TAG, "[%s] retry limit reached; pending work discarded", this->get_name().c_str());
        this->commands_.clear();
        this->batch_active_ = false;
        this->recovering_ = false;
        return;
      }
      const uint32_t delay = std::min(60000u, 2000u << (this->retries_ - 1));
      this->set_timeout("retry", delay, [this]() {
        if (this->expire_controls())
          return;
        this->recovering_ = false;
        this->set_timeout("session", 60000, [this]() { this->fail_session("connection setup timed out"); });
        this->connect();
      });
    }

    void Device::gattc_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if, esp_ble_gattc_cb_param_t *param)
    {
      switch (event)
      {
      case ESP_GATTC_CONNECT_EVT:
        if (memcmp(param->connect.remote_bda, this->parent()->get_remote_bda(), 6) != 0)
          return; // event does not belong to this client, exit gattc_event_handler

        this->set_timeout("session", 30000, [this]() { this->fail_session("discovery/authentication timed out"); });
        ESP_LOGD(TAG, "[%s] connect, conn_id=%d", this->get_name().c_str(), param->connect.conn_id);
        break;

      case ESP_GATTC_OPEN_EVT:
        if (param->open.status == ESP_GATT_OK)
          ESP_LOGV(TAG, "[%s] open, conn_id=%d", this->get_name().c_str(), param->open.conn_id);
        else
          this->fail_session("connection failed");
        break;

      case ESP_GATTC_CLOSE_EVT:
        if (!this->closing_ && !this->recovering_ && (this->in_flight_ || !this->commands_.is_empty()))
          this->fail_session("connection closed before work completed");
        if (param->close.status == ESP_GATT_OK)
          ESP_LOGV(TAG, "[%s] close, conn_id=%d, reason=%d", this->get_name().c_str(), param->close.conn_id, param->close.reason);
        else
          ESP_LOGW(TAG, "[%s] failed to close, conn_id=%d, status=%#04x", this->get_name().c_str(), param->close.conn_id, param->close.status);
        break;

      case ESP_GATTC_DISCONNECT_EVT:
        if (!this->closing_ && !this->recovering_ && (this->in_flight_ || !this->commands_.is_empty()))
          this->fail_session("connection interrupted");
        ESP_LOGD(TAG, "[%s] disconnect, conn_id=%d, reason=%#04x", this->get_name().c_str(), param->disconnect.conn_id, (int)param->disconnect.reason);
        break;

      case ESP_GATTC_SEARCH_CMPL_EVT:
        if (this->recovering_ || this->closing_)
          return;
        if (param->search_cmpl.status != ESP_GATT_OK)
        {
          this->fail_session("service discovery failed");
          break;
        }
        for (auto p : this->properties)
        {
          if (!p->init_handle(this->parent()) && p != this->p_secret_key)
          {
            this->fail_session("required characteristic missing");
            return;
          }
        }
        write_pin();
        break;

      case ESP_GATTC_WRITE_CHAR_EVT:
        if (param->write.handle == this->p_pin->handle)
          this->on_write_pin(param->write);
        else
          this->on_write(param->write);
        break;

      case ESP_GATTC_READ_CHAR_EVT:
        this->on_read(param->read);
        break;

      default:
        ESP_LOGV(TAG, "[%s] unhandled event: event=%d, gattc_if=%d", this->get_name().c_str(), (int)event, gattc_if);
        break;
      }
    }

    void Device::write_pin()
    {
      ESP_LOGD(TAG, "[%s] writing pin", this->get_name().c_str());

      uint8_t pin_bytes[sizeof(uint32_t)];
      write_int(pin_bytes, 0, this->pin_code_);

      if (!this->p_pin->write_request(this->parent(), pin_bytes, sizeof(pin_bytes)))
        this->fail_session("PIN write rejected");
      else
      {
        this->pin_pending_ = true;
        this->set_timeout("pin", 15000, [this]() { this->fail_session("PIN response timed out"); });
      }
    }

    void Device::on_read(esp_ble_gattc_cb_param_t::gattc_read_char_evt_param param)
    {
      if (this->expire_controls())
        return;
      if (!this->response_matches(false, param.handle))
        return;
      this->cancel_timeout("request");
      this->request_pending_ = false;
      if (!this->current_control(*this->in_flight_))
      {
        this->in_flight_.reset();
        return;
      }
      const Command *responding = this->in_flight_.get();
      const bool valid = param.status == ESP_GATT_OK &&
                         responding->property->update_state(param.value, param.value_len);
      // State publication invokes ESPHome automations synchronously. Those
      // callbacks can replace intent or expire/reset this very transaction.
      if (this->in_flight_.get() != responding || this->expire_controls())
        return;
      if (!this->current_control(*this->in_flight_))
      {
        this->in_flight_.reset();
        return;
      }
      if (!valid)
      {
        if (this->in_flight_->type == CommandType::READ && this->in_flight_->property != this->p_secret_key)
        {
          // An explicit failure of one poll must not discard unrelated controls.
          ESP_LOGW(TAG, "[%s] skipping failed poll, handle=%#04x, status=%u, length=%u", this->get_name().c_str(), param.handle, param.status, param.value_len);
          this->status_set_warning();
          this->in_flight_.reset();
          return;
        }
        this->fail_session("invalid or failed read response");
        return;
      }
      if (this->in_flight_->type == CommandType::READ)
      {
        const bool learned_key = this->in_flight_->property == this->p_secret_key;
        this->in_flight_.reset();
        if (learned_key)
          this->update();
      }
      else if (this->in_flight_->phase == CommandPhase::PREPARE)
        this->in_flight_->phase = CommandPhase::WRITE;
      else if (!this->in_flight_->verified())
        this->fail_session("thermostat readback did not match command");
      else
      {
        ESP_LOGI(TAG, "[%s] command confirmed by thermostat readback", this->get_name().c_str());
        this->pending_controls_.erase(this->in_flight_->property.get());
        this->in_flight_.reset();
        if (!this->delivery_failed_)
          this->status_clear_warning();
        // Mode changes can change the scheduled setpoint too.
        this->commands_.push(new Command(CommandType::READ, this->p_temperature));
        this->commands_.push(new Command(CommandType::READ, this->p_settings));
        this->publish_command_status();
      }
    }

    void Device::on_write(esp_ble_gattc_cb_param_t::gattc_write_evt_param param)
    {
      if (this->expire_controls())
        return;
      if (!this->response_matches(true, param.handle))
        return;
      this->cancel_timeout("request");
      this->request_pending_ = false;
      if (param.status != ESP_GATT_OK)
        this->fail_session("write failed");
      else
        this->in_flight_->phase = CommandPhase::VERIFY;
    }

    void Device::on_write_pin(esp_ble_gattc_cb_param_t::gattc_write_evt_param param)
    {
      if (this->expire_controls())
        return;
      if (!this->pin_pending_ || this->recovering_ || this->closing_ ||
          this->parent()->state() == ClientState::DISCONNECTING)
        return;
      this->pin_pending_ = false;
      this->cancel_timeout("pin");
      this->cancel_timeout("session");
      if (param.status != ESP_GATT_OK)
      {
        this->fail_session("PIN write failed");
        return;
      }
      this->node_state = ClientState::ESTABLISHED;
      if (this->xxtea->status() == XXTEA_STATUS_NOT_INITIALIZED)
      {
        if (this->p_secret_key->handle == INVALID_HANDLE)
        {
          this->fail_session("thermostat key unavailable; press thermostat button to pair");
          return;
        }
        this->commands_.push_prerequisite(std::make_unique<Command>(CommandType::READ, this->p_secret_key));
      }
    }

    void Device::connect()
    {
      if (this->expire_controls())
        return;
      // A command can arrive before the preceding connection finishes closing.
      // Keep it queued and reconnect once the controller has released the link.
      if (this->parent()->state() == ClientState::DISCONNECTING)
      {
        this->set_timeout("reconnect", 250, [this]() { this->connect(); });
        return;
      }
      if (this->parent()->state() != ClientState::IDLE)
      {
        return;
      }

      this->closing_ = false;
      if (this->xxtea->status() == XXTEA_STATUS_NOT_INITIALIZED)
        ESP_LOGI(TAG, "[%s] Short press Danfoss Eco hardware button NOW in order to allow reading the secret key", this->get_name().c_str());

      if (!parent()->enabled)
      {
        ESP_LOGD(TAG, "[%s] re-enabling ble_client", this->get_name().c_str());
        parent()->set_enabled(true);
      }
      // Let the tracker stop scanning, serialize connection attempts, and
      // prioritize Bluetooth over Wi-Fi before it opens the connection.
      this->parent()->set_state(ClientState::DISCOVERED);
    }

    void Device::disconnect()
    {
      this->closing_ = true;
      this->parent()->set_enabled(false);
    }

    void Device::set_pin_code(const string &str)
    {
      if (str.length() > 0)
        this->pin_code_ = atoi((const char *)str.c_str());

    }

    void Device::set_secret_key(const string &str)
    {
      // initialize the preference object
      uint32_t hash = fnv1_hash("danfoss_eco_secret__" + this->get_name());
      this->secret_pref_ = global_preferences->make_preference<SecretKeyValue>(hash, true);

      if (str.length() > 0)
      {
        uint8_t buff[SECRET_KEY_LENGTH];
        ESP_LOGD(TAG, "[%s] secret_key was passed via config", this->get_name().c_str());
        parse_hex_str(str.c_str(), 32, buff);
        this->set_secret_key(buff, false);
      }
      else
      {
        auto key_buff = SecretKeyValue();
        if (this->secret_pref_.load(&key_buff))
        {
          // use persisted secret value
          ESP_LOGD(TAG, "[%s] secret_key was loaded from flash", this->get_name().c_str());
          this->set_secret_key(key_buff.value, false);
        }
      }
    }

    void Device::set_secret_key(uint8_t *key, bool persist)
    {

      int status = this->xxtea->set_key(key, SECRET_KEY_LENGTH);
      if (status != XXTEA_STATUS_SUCCESS)
      {
        ESP_LOGE(TAG, "xxtea initialization failed, status: %d", status);
        this->mark_failed();
      }
      else if (persist)
      {
        // if xxtea was initialized successfully and secret_key should be persisted
        auto key_buff = SecretKeyValue(key);
        this->secret_pref_.save(&key_buff);
        global_preferences->sync();

        ESP_LOGI(TAG, "[%s] secret_key was saved to flash", this->get_name().c_str());
      }
    }

  } // namespace danfoss_eco
} // namespace esphome

#endif
