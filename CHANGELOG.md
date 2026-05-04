# Changelog

## v16.00

- Added the temporary `FIELD_DEBUG_BUILD` profile to force Particle connection attempts on boot and cap sleep to 120 seconds.
- Added guarded boot, connect, and wake diagnostics for battery, power-path, heap, and network state to help catch the next reboot remotely.
- Preserved the existing network power-down-before-sleep safety behavior while improving field observability.

## v15.00

- Disabled rescue Wi-Fi reprovisioning by setting `UPDATE_WIFI_CREDENTIALS` back to `0`.
- Removed the plaintext Wi-Fi SSID and password from the source configuration for the post-recovery cleanup release.
- Bumped the gateway firmware and product version to `15.00` for this cleanup release.

## v13.00

- Changed the carrier-board `BUTTON_PIN` setup to `INPUT` so the user button behaves correctly on the current hardware.
- Bumped the gateway firmware and product version to `13.00` for this release.

## v12.00

- Unified Photon 2 and P2 temperature handling to read the TMP36 on `A5` while keeping `A4` configured as an input for safety.
- Documented that Photon 2/P2 temperature sensing only works on this carrier when `A4` and `A5` are physically bridged.
- Kept the guarded 25C fallback for invalid or out-of-range temperature readings so battery-related logic never trusts a bad TMP36 value.