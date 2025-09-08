#pragma once

#include <queue>
#include "esphome/components/esp32_ble_tracker/esp32_ble_tracker.h"

#include "properties.h"

namespace esphome
{
    namespace danfoss_eco
    {
        using namespace std;

        enum class CommandType
        {
            READ,
            WRITE
        };

        struct Command
        {
            Command(CommandType t, shared_ptr<DeviceProperty> const &p) : type(t), property(p) {}

            CommandType type; // 0 - read, 1 - write
            shared_ptr<DeviceProperty> property;

            bool execute(esphome::ble_client::BLEClient *client)
            {
                if (this->type == CommandType::WRITE)
                {
                    WritableProperty *wp = static_cast<WritableProperty *>(this->property.get());
                    return wp->write_request(client);
                }
                else
                    return this->property->read_request(client);
            }
        };

        class CommandQueue
        {
        public:
            void push(Command *cmd) { 
                this->q_.push(cmd); 
            }
            
            Command *pop() { 
                if (this->q_.empty()) return nullptr;
                Command *cmd = this->q_.front();
                this->q_.pop();
                return cmd;
            }
            
            bool is_empty() { 
                return this->q_.empty(); 
            }
            
        private:
            std::queue<Command *> q_;
        };

    } // namespace danfoss_eco
} // namespace esphome
