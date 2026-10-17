#include "esphome/core/helpers.h"
#include "helpers.h"
#include "esphome/components/climate/climate_mode.h"
#include "device_data.h"
#include <array>
#include <cassert>
#include <vector>

// Exercise the real encryption helper and XXTEA implementation under ASan.
// A uint16_t length passed as size_t* must fail, even if encryption round-trips.
int main() {
    uint8_t key[16] = {};
    auto cipher = std::make_shared<Xxtea>();
    assert(cipher->set_key(key, sizeof(key)) == XXTEA_STATUS_SUCCESS);
    for (size_t length : {size_t{8}, size_t{16}, size_t{32}, size_t{80}}) {
        std::array<uint8_t, 80> payload{};
        for (size_t i = 0; i < length; ++i)
            payload[i] = static_cast<uint8_t>(i + 1);
        const auto original = payload;
        esphome::danfoss_eco::encrypt(cipher, payload.data(), length);
        assert(payload != original);
        esphome::danfoss_eco::decrypt(cipher, payload.data(), length);
        assert(payload == original);
    }

    // A settings command must send all 16 bytes, retaining unrelated settings
    // and mapping manual/scheduled modes to their wire values 0/1.
    const std::array<uint8_t, 16> original_settings = {
        0x44, 20, 60, 10, 1, 16, 1, 2, 3, 4, 5, 6, 7, 8, 0xA5, 0x5A
    };
    auto encrypted_settings = original_settings;
    esphome::danfoss_eco::encrypt(cipher, encrypted_settings.data(), encrypted_settings.size());
    esphome::danfoss_eco::SettingsData settings(cipher, encrypted_settings.data(), encrypted_settings.size());
    assert(settings.length == 16);
    for (auto mode : {esphome::climate::CLIMATE_MODE_HEAT, esphome::climate::CLIMATE_MODE_AUTO}) {
        settings.device_mode = mode;
        std::vector<uint8_t> packed(settings.length);
        settings.pack(packed.data());
        esphome::danfoss_eco::decrypt(cipher, packed.data(), packed.size());
        auto expected = original_settings;
        expected[4] = mode == esphome::climate::CLIMATE_MODE_HEAT ? 0 : 1;
        assert(std::equal(packed.begin(), packed.end(), expected.begin(), expected.end()));
    }
}
