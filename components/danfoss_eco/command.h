#pragma once

#include <deque>
#include <optional>
#include <vector>
#include "properties.h"

namespace esphome::danfoss_eco
{
    enum class CommandType { READ, WRITE };
    enum class CommandPhase { PREPARE, WRITE, VERIFY };

    struct Command
    {
        Command(CommandType t, const std::shared_ptr<DeviceProperty> &p) : type(t), property(p) {}
        Command(const std::shared_ptr<DeviceProperty> &p, float target) : type(CommandType::WRITE), property(p), temperature(target) {}
        Command(const std::shared_ptr<DeviceProperty> &p, climate::ClimateMode value) : type(CommandType::WRITE), property(p), mode(value) {}

        CommandType type;
        std::shared_ptr<DeviceProperty> property;
        std::optional<float> temperature;
        std::optional<climate::ClimateMode> mode;
        CommandPhase phase{CommandPhase::PREPARE};
        bool prerequisite{false};
        uint64_t delivery_id{0};

        bool is_write_step() const { return type == CommandType::WRITE && phase == CommandPhase::WRITE; }
        bool same_intent(const Command &other) const { return type == other.type && property == other.property; }
        void restart() { phase = CommandPhase::PREPARE; }

        bool execute(ble_client::BLEClient *client)
        {
            if (!is_write_step())
                return property->read_request(client);
            if (!property->data || !property->data->valid)
                return false;
            // Pack against fresh observed settings, restoring the observed values
            // immediately. User intent lives in this command, never in read state.
            std::vector<uint8_t> bytes(property->data->length, 0);
            if (temperature.has_value())
            {
                auto *data = static_cast<TemperatureData *>(property->data.get());
                const float old = data->target_temperature;
                data->target_temperature = *temperature;
                data->pack(bytes.data());
                data->target_temperature = old;
            }
            else if (mode.has_value())
            {
                auto *data = static_cast<SettingsData *>(property->data.get());
                const auto old = data->device_mode;
                data->device_mode = *mode;
                data->pack(bytes.data());
                data->device_mode = old;
            }
            else
                return false;
            return static_cast<WritableProperty *>(property.get())->write_request(client, bytes.data(), bytes.size());
        }

        bool verified() const
        {
            if (!property->data || !property->data->valid)
                return false;
            if (temperature.has_value())
                return static_cast<TemperatureData *>(property->data.get())->target_temperature == *temperature;
            if (mode.has_value())
            {
                const auto expected = *mode == climate::CLIMATE_MODE_AUTO ? SettingsData::SCHEDULED : SettingsData::MANUAL;
                return static_cast<SettingsData *>(property->data.get())->wire_mode == expected;
            }
            return false;
        }
    };

    class CommandQueue
    {
    public:
        // At most one pending read and one pending write per property. Repeated
        // polls coalesce, and the newest requested value replaces older intent.
        void push(std::unique_ptr<Command> command)
        {
            for (auto &pending : q_)
                if (pending->same_intent(*command)) { pending = std::move(command); return; }
            if (command->type == CommandType::WRITE)
            {
                auto pos = q_.begin();
                while (pos != q_.end() && ((*pos)->prerequisite || (*pos)->type == CommandType::WRITE)) ++pos;
                q_.insert(pos, std::move(command));
            }
            else
                q_.push_back(std::move(command));
        }
        void push_prerequisite(std::unique_ptr<Command> command) { command->prerequisite = true; q_.push_front(std::move(command)); }
        void push(Command *command) { push(std::unique_ptr<Command>(command)); }
        void retry(std::unique_ptr<Command> command)
        {
            command->restart();
            for (const auto &pending : q_)
                if (pending->same_intent(*command)) return; // a newer request wins
            if (command->prerequisite || command->type == CommandType::WRITE) q_.push_front(std::move(command));
            else q_.push_back(std::move(command));
        }
        std::unique_ptr<Command> pop()
        {
            if (q_.empty()) return nullptr;
            auto command = std::move(q_.front()); q_.pop_front(); return command;
        }
        bool is_empty() const { return q_.empty(); }
        void remove_write(const DeviceProperty *property)
        {
            for (auto it = q_.begin(); it != q_.end();)
                if ((*it)->type == CommandType::WRITE && (*it)->property.get() == property)
                    it = q_.erase(it);
                else
                    ++it;
        }
        void clear() { q_.clear(); }
    private:
        std::deque<std::unique_ptr<Command>> q_;
    };
}
