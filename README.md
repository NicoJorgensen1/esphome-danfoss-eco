Danfoss Eco
=============

## ESPHome 2026.9 baseline

This branch starts from upstream `v1.1.4` (`1bc9622`) and adapts its
climate schema, traits, BLE connection, command queue, UUID formatting,
and runtime temperature limits to ESPHome **2026.9.1**, using the Arduino
framework on `esp32dev`. It also corrects an out-of-bounds access in the
encryption helper's output-length argument and preserves the full 16-byte
settings payload when changing mode. Commands arriving before the first
successful thermostat read retain their requested value until a fresh read
allows the component to prepare the write.
Connection requests go through ESPHome's BLE tracker so it can coordinate
scanning, concurrent clients, and Wi-Fi/Bluetooth radio priority.

It retains upstream manual (`heat`) and scheduled (`auto`, “døgnrytme”)
operation. It does not include the previous fork's presets or minute-slot
polling. Keep Wi-Fi credentials and thermostat keys in private ESPHome
configuration and `secrets.yaml`, outside this repository.

For a thermostat that repeatedly fails to connect, `auto_connect: false` on
its `ble_client` prevents continuous advertisement-driven retries. Configured
polls and climate commands still request a connection through the tracker.

`tests/compile.yaml` exercises two thermostats with dummy credentials;
use it only for configuration validation and compilation:

```sh
esphome config tests/compile.yaml
esphome compile tests/compile.yaml
```

The encryption and schedule-settings regression test compiles the real component helper and
the `xxtea-iot-crypt` 2.0.1 implementation with address and undefined-behavior
sanitizers. Only hardware headers and logging are replaced for the host test:

```sh
sh tests/run-encryption.sh /path/to/xxtea-iot-crypt/src /path/to/python/site-packages
```

Under a ptrace-based sandbox, set `ASAN_OPTIONS=detect_leaks=0`; address
and undefined-behavior checks remain enabled.

## Control reliability

The component performs one GATT request at a time. Temperature and mode commands
read the current payload, write the requested change, and read it back to verify
what the thermostat accepted. Home Assistant receives observed values rather than
optimistic acknowledgments. `auto` verification requires scheduled mode, not vacation mode.

Repeated polls coalesce; the latest pending request for each setting wins. Controls
run before routine polls, after any pairing prerequisite. A failed diagnostic read
is skipped so it cannot discard an unrelated control. Connection, discovery, PIN,
and request deadlines recover stalled sessions. Pending controls retry until their
configurable deadline, with 2/4/8/16/32/60-second backoff capped at 60 seconds.
Routine polling alone still stops after five failed attempts. Polls do not extend
a control deadline. A newer request for the same setting replaces the older one
and starts its own deadline; temperature and mode have independent deadlines.

Set the timeout separately for each thermostat in its `climate` configuration:

```yaml
    command_timeout: 15min
    command_status:
      name: "My Room eTRV Command Delivery"
```

`command_timeout` defaults to `15min`, accepts ESPHome durations from `1ms` to
`24h`, and requires rebuilding/uploading firmware when edited. The optional
diagnostic text sensor reports `unknown` after boot, `pending` while any current
request awaits readback, `confirmed` after all current requests are verified,
or `failed` when a request expires or is rejected. A failure remains visible
after routine successful polls; a new valid request starts a new status cycle.
If one setting expires while another is pending, status stays `pending`, then
becomes `failed` when the remaining setting finishes. This sensor describes
command delivery, not continuous connectivity or the thermostat's Problems flag.

The separate `connection_setup_timeout` defaults to `60s` and accepts `1s` to
`10min`. It includes time waiting behind other BLE clients as well as opening
the link. If `esp32_ble.connection_timeout` is increased (for example, to `40s`),
allow enough setup time for all clients sharing the board (for example,
`connection_setup_timeout: 150s` for three clients). This does not extend
`command_timeout`; expired controls still fail and stop retrying. Once the link
opens, discovery/authentication and individual requests retain their own deadlines.

An expired command is removed and cannot start further writes. A write already
sent cannot be retracted: `failed` means **not confirmed before the deadline**, not
proof that the thermostat did nothing. Pending commands are held in RAM and are
lost on ESP32 restart; `unknown` avoids claiming interrupted commands succeeded.
There is no guarantee of delivery while the thermostat remains unreachable.

Malformed packet lengths are rejected before parsing. A thermostat may return
inconsistent temperature limits (observed raw minimum/maximum 100/68). These do
not invalidate its mode: retain its original settings bytes, warn, and use visual
bounds of 5–35°C. Those fallback bounds are not confirmed device capabilities. PIN and key bytes are not
logged. The Heating/Idle action remains an estimate from temperatures, not measured
valve position. Bluetooth availability and thermostat wake-up time still limit latency.

Host regression tests compile the real control, packet and encryption code with
AddressSanitizer and UndefinedBehaviorSanitizer, using fake ESPHome/BLE boundaries:

```sh
sh tests/run-control.sh /path/to/xxtea-iot-crypt/src /path/to/python/site-packages
PYTHONPATH=. python tests/config.py
PYTHONPATH=. python tests/setup-timeout.py
```

They cover readback verification, malformed packets, serialized requests, retained
and superseded commands, polling coalescing, bounded retries during polling,
disconnect recovery, missing discovery, late PIN replies, failed diagnostic reads,
commands arriving during close, pairing prerequisites, and the observed inconsistent
temperature-limit fields. Hardware validation is
separate; these tests cannot establish radio reliability or actual valve movement.

The original upstream documentation follows. Its legacy configuration
examples predate this branch's ESPHome version.

The ``danfoss_eco`` climate platform creates a climate device which can be used to control a Danfoss Eco eTRV.

This component supports the following functionality:

- Switch between Manual and Schedule modes
- Set the target room temperature
- Show the current temperature
- Show current action (Heating/Idle)
- Show remaining battery level
- Managing multiple eTRVs from a single ESP32
- Reporting eTRV error status (with error codes)

This platform uses the ESP32 BLE peripheral on ESP32, which means, ``ble_client`` configuration should be provided.

Onboarding your device
------------------------
You should know the MAC address of your Danfoss Eco in order to complete the configuration. This information can be obtained in two ways:
1. Look it up in Danfoss Eco mobile app: `Settings->System Information->MAC Address`
2. Use `danfoss_eco_scanner` sensor and check the EspHome logs. Sample configuration for the scanner:
```yaml
esphome:
  name: etrv2wifi-scanner
  platform: ESP32
  board: esp32dev
  libraries:
    - xxtea-iot-crypt@2.0.1

logger:
  level: INFO

external_components:
  - source: github://dmitry-cherkas/esphome-danfoss-eco@v1.1.3

sensor:
  - platform: danfoss_eco_scanner
    id: scanner
```
When the scanner is running, press the hardware button on your Danfoss Eco in order to speed up the discovery. Sample scanner output will look like this:
```
[01:40:19][I][danfoss_eco_scanner:027]: Found Danfoss eTRV, MAC: 00:04:2F:xx:yy:zz, Name: 0;0:04:2F:xx:yy:zz;eTRV
```

Once the MAC Adress is known, esphome component can be configured as follows:
```yaml
external_components:
  - source: github://dmitry-cherkas/esphome-danfoss-eco@v1.1.3
ble_client:
  - mac_address: 00:04:2f:xx:yy:zz
    id: room_eco
climate:
  - platform: danfoss_eco
    name: "My Room eTRV"
    ble_client_id: room_eco
    battery_level:
      name: "My Room eTRV Battery Level"
    temperature:
      name: "My Room eTRV Temperature"
    update_interval: 30min
```

### Obtaining the `secret_key`
Danfoss Eco is using encrypted communication, which relies on the `secret_key`. This key can be obtained only if the hardware button was pressed before Bluetooth connection is established. Keep track of the EspHome logs to know, when the button should be pressed:
```
[01:25:01][I][logger:214]: Log initialized
[01:25:01][I][app:029]: Running through setup()...
[01:25:02][I][danfoss_eco:215]: [My Room eTRV] Short press Danfoss Eco hardware button NOW in order to allow reading the secret key
```
If the button was not pressed in time, failure to retrieve the `secret_key` will be logged. Press the button during a later connection attempt.
```
[01:25:13][W][danfoss_eco:148]: [My Room eTRV] Danfoss Eco hardware button was not pressed, unable to read the secret key
```

When the component reads the `secret_key`, it stores it in ESP32 flash. This
branch logs only whether the key was saved, never the key bytes. Keep any explicit
key configuration in private ESPHome secrets.

Configuration options
------------------------

- **id** (*Optional*): Manually specify the ID used for code generation.
- **name** (**Required**, string): The name of the climate device.
- **ble_client_id** (**Required**): The ID of the BLE Client.
- **pin_code** (**Optional**, string): Device PIN code (if configured). Should be 4 characters numeric string.
- **secret_key** (**Required**, string): Device encryption key, 16 characters.
- **battery_level** (**Optional**, string): Remaining battery level sensor name. Sensor will not be created, if the name is not provided.
- **temperature** (**Optional**, string): Current temperature (Celsius) sensor name. Sensor will not be created, if the name is not provided.

> **NOTE:** Find more configuration examples in the repository root folder.


See Also
--------

This component is based on the work of other authors:
* [AdamStrojek libetrv](https://github.com/AdamStrojek/libetrv) (with additional features from [spin83](https://github.com/spin83/libetrv) fork)
* MQTT bridge for Danfoss eTRV by [keton](https://github.com/keton/etrv2mqtt) and Home Assistant add-on by [HBDK](https://github.com/HBDK/Eco2-Tools)

Other implementations:
* [Source code](https://github.com/dsltip/Danfoss-BLE) for controlling Danfoss ECO via cc2541 as a gateway from the desktop PC.
