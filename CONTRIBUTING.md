# Contributing to ESP LoRa Link

Bug reports, documentation improvements, and firmware changes are welcome.

## Report a problem or suggest an improvement

Check [existing issues](https://github.com/pravennn08/esp-lora-link/issues) first. For a bug, choose **New issue → Bug report** and include the board and radio models, dependency versions, wiring details, reproduction steps, and relevant serial or compiler logs.

For a feature idea, open an issue describing the behavior you want and a practical use case. Discuss larger protocol or hardware changes before implementing them.

## Set up locally

Follow the [README setup instructions](README.md#getting-started). The build dependencies used by CI are:

| Dependency | Version / target |
| --- | --- |
| Arduino CLI | 1.x |
| ESP32 board package by Espressif Systems | 3.3.0 |
| [LoRa by Sandeep Mistry](https://github.com/sandeepmistry/arduino-LoRa) | 0.8.0 |
| Board | ESP32 Dev Module (`esp32:esp32:esp32`) |

With Arduino CLI installed, run these commands from the repository root. Add the index URL only once if it is already configured locally.

```sh
arduino-cli config add board_manager.additional_urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
arduino-cli core update-index
arduino-cli core install esp32:esp32@3.3.0
arduino-cli lib install "LoRa@0.8.0"
arduino-cli compile --fqbn esp32:esp32:esp32 --warnings all LoRaButtonNodeA
arduino-cli compile --fqbn esp32:esp32:esp32 --warnings all LoRaButtonNodeB
```

Arduino IDE users can open each `.ino` directly from its matching folder and use **Verify**.

## Make a change

1. Fork the repository and create a branch for your change.
2. Keep changes focused and follow the surrounding C++ formatting.
3. Keep both `LoRaButtonLink.h` copies identical when changing the protocol.
4. Preserve the distinct node IDs. Update the wiring diagram and README if you change GPIO mappings or radio settings.
5. Add a concise entry under **Unreleased** in [CHANGELOG.md](CHANGELOG.md) for user-visible changes.

## Validate your work

For firmware changes, compile **both** sketches. CI performs compilation; it cannot verify wiring or radio behavior.

When hardware is available, check both directions:

- Holding Node A's button lights Node B's LED; releasing it turns that LED off.
- Holding Node B's button lights Node A's LED; releasing it turns that LED off.
- With a peer-controlled LED on, removing the sending node's power turns that LED off about 2.5 seconds after its last fresh state.
- Serial Monitor at 115200 baud shows the expected state updates and acknowledgements.

For documentation-only changes, check file links, GPIO labels, and image readability. Report which checks you performed. If you could not compile or use hardware, say so in the pull request.

## Submit a pull request

Describe the problem, resulting behavior, related issue, and validation results. Include the board package and LoRa versions when reporting firmware builds, and hardware details when reporting radio tests.

Keep generated firmware binaries, credentials, and local IDE files out of the pull request. Use the existing [MIT License](LICENSE.md) for contributions to this project.
