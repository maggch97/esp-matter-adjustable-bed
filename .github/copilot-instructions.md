# Copilot Instructions

## Build

This is an ESP-IDF project using the ESP-IDF Component Manager. ESP-IDF v5.4.1 is required.

```bash
# All commands go through the helper script which sets up IDF_PATH and environment:
./idf.sh build              # full build
./idf.sh flash monitor      # flash to device and open serial monitor
./idf.sh menuconfig         # interactive sdkconfig editor
./idf.sh set-target esp32s3 # switch target chip (requires clean rebuild)
```

If `idf.sh` doesn't work (e.g. different IDF install path), source ESP-IDF manually:
```bash
export IDF_PATH=~/workspace/esp-idf
source "$IDF_PATH/export.sh"
idf.py build
```

There are no unit tests or linters configured.

## Architecture

This is a Matter smart adjustable bed controller. It creates a Matter device with:

- **Root node** (endpoint 0) — mandatory Matter root
- **Aggregator endpoint** — groups the two motor endpoints
- **Motor 1 endpoint** (Window Covering) — e.g. head section lift
- **Motor 2 endpoint** (Window Covering) — e.g. foot section lift

Each motor is controlled by two GPIO relay pins (forward/reverse). A FreeRTOS timer (`motor_timer_callback`, 100ms period) drives motors toward their target positions by toggling relays, then updates the Matter `CurrentPositionLiftPercent100ths` attribute when the target is reached.

### Key files

- `main/app_main.cpp` — Matter node setup, motor control logic (timer callback, GPIO relay init, attribute callbacks)
- `main/app_driver.cpp` — Physical button driver using `espressif/button` v4 GPIO API, Generic Switch event handling
- `main/app_priv.h` — Shared types, button/endpoint structs, `ABORT_APP_ON_FAILURE` macro

### Dependencies

Declared in `main/idf_component.yml`, resolved automatically by the Component Manager:
- `espressif/esp_matter ^1.4.0` — Matter protocol stack
- `espressif/button ^4` — GPIO button driver

## Conventions

- All source files live in `main/`. This is a single-component ESP-IDF project.
- `sdkconfig.defaults` contains required build settings (HKDF, BLE, partition table, etc). Don't delete entries without understanding Matter requirements.
- The `partitions.csv` uses OTA-capable layout. Changes affect flash addressing.
- Motor position is tracked in milliseconds of movement time (0 to `FULL_MOVEMENT_TIME_MS`), not percentage. Conversion happens at the Matter attribute boundary.
- The project uses C++17 (`-std=gnu++17` set in project-level CMakeLists.txt).
- Button API uses `iot_button_new_gpio_device()` + `iot_button_register_cb()` (v4 API, not the old `iot_button_create`).
