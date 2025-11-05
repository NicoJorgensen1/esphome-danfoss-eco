# Bugfix: Preset Reverting to Default

## Problem Description
When changing the preset mode in Home Assistant (e.g., from HOME to SLEEP), the preset would revert back after a short time. This made it impossible to maintain custom preset selections.

## Root Cause
The issue was in `components/danfoss_eco/properties.cpp` in the `SettingsProperty::update_state()` method.

**The Problem Flow:**
1. User changes preset in Home Assistant (e.g., selects SLEEP)
2. ESPHome writes the settings to the Danfoss device
3. After write completes, ESPHome calls `update()` to refresh state
4. `update()` reads all settings back from the device
5. **BUG:** When reading settings, the code unconditionally overwrote the preset based on device mode:
   - SCHEDULED mode → forced preset to HOME
   - VACATION mode → forced preset to AWAY

Since the Danfoss ECO device doesn't actually store "presets" (it only knows MANUAL, SCHEDULED, and VACATION modes), when you selected SLEEP preset (which writes SCHEDULED mode to the device), the next read would see SCHEDULED mode and forcibly change the preset back to HOME.

## The Fix
Modified `properties.cpp` to preserve user-selected presets instead of overwriting them on every read:

1. **Preserve user selection**: Don't overwrite preset unless there's a specific reason
2. **Sync VACATION mode**: Only force preset to AWAY when device is actually in VACATION mode
3. **Handle initialization**: Set a sensible default (HOME) when preset is uninitialized
4. **Smart transitions**: Only change preset when device mode and preset become inconsistent

## Changed File
- `components/danfoss_eco/properties.cpp` - lines 126-150

## Testing
After applying this fix:
1. Flash your ESP32 with the updated code
2. Try changing presets in Home Assistant
3. Presets should now stay as you set them (HOME, SLEEP, AWAY)
4. Only AWAY preset will trigger actual VACATION mode on the device

## Technical Details
The Danfoss ECO device has 3 real operating modes:
- **MANUAL** (manual temperature control)
- **SCHEDULED** (follows programmed schedule)
- **VACATION** (special vacation mode with custom temperature)

ESPHome maps these to Climate modes and adds presets for better UX:
- MANUAL → HEAT mode
- SCHEDULED → AUTO mode + HOME/SLEEP presets (both use SCHEDULED mode)
- VACATION → AUTO mode + AWAY preset

Since multiple presets map to the same device mode (SCHEDULED), the preset must be maintained by ESPHome, not derived from the device state.

