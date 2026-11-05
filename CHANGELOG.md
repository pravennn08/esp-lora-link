# Changelog

Notable changes to ESP LoRa Link are recorded here. Changes awaiting a tagged release are listed under **Unreleased**.

## Unreleased

### Fixed

- Retry dependency downloads in CI up to three times to tolerate temporary server failures during ESP32 core or LoRa installation.

### Added

- Setup and upload tutorial, wiring tables, radio settings, and troubleshooting in the README.
- Wiring diagram and illustrative animation showing button-to-peer-LED behavior.
- Explicit dependency information for LoRa by Sandeep Mistry, version 0.8.0.
- GitHub Actions compilation checks for both ESP32 sketches.
- Contribution guidelines, a hardware-focused bug-report form, and a pull request template.

### Changed

- Renamed the sketch folders to `LoRaButtonNodeA` and `LoRaButtonNodeB` so they match their main `.ino` files and open directly in Arduino IDE.
- Updated CI and documentation to use the renamed folders.
