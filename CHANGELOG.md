# Changelog

## v13.00

- Changed the carrier-board `BUTTON_PIN` setup to `INPUT` so the user button behaves correctly on the current hardware.
- Bumped the gateway firmware and product version to `13.00` for this release.

## v12.00

- Unified Photon 2 and P2 temperature handling to read the TMP36 on `A5` while keeping `A4` configured as an input for safety.
- Documented that Photon 2/P2 temperature sensing only works on this carrier when `A4` and `A5` are physically bridged.
- Kept the guarded 25C fallback for invalid or out-of-range temperature readings so battery-related logic never trusts a bad TMP36 value.