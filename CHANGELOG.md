# Changelog

## v23.00 - 2026-05-17

- Reduced Gateway NodeDB persistence overhead by coalescing duplicate saves, keeping data-report bookkeeping off the ACK hot path, and preserving mirrored FRAM durability and repair behavior.
- Added compact persistence timing instrumentation and 24-hour write statistics so field logs can show save latency, mirrored-write cost, and burst frequency without verbose spam.
- Kept LoRa runtime behavior unchanged while replacing the brittle RF9X preset enum dependency with the exact equivalent modem-register shim for reproducible Boron and P2 builds.
- Preserved existing sleep/connect, watchdog, PMIC, and timing behavior while keeping release logging concise and reducing format-string truncation risk.
- Centralized gateway operational settings into committed `src/config.h`, kept `src/local_secrets.h` local and ignored, and preserved the existing compile-time Wi-Fi credential validation flow.
- Kept the current v23 runtime behavior while clarifying boot/time/wake observability, suppressing invalid pre-connect Wi-Fi signal logs, and reducing low-level RTC helper noise.
- Validated full Boron and P2 production builds for release packaging.

## v21.00 - 2026-05-09

- Added gateway production-hardening updates for Photon 2 / P2, including VBAT-based battery telemetry with conservative SoC estimation.
- Hardened FRAM-backed persistence by immediately saving first-boot defaults, repairing blank NodeDB state, verifying both NodeDB copies, and preserving corrupt NodeDB payloads for later inspection.
- Normalized app-side runtime logging through the central `Log` handler, added boot reset reason/data reporting, and preserved concise Wi-Fi provisioning and battery diagnostic logs.
- Added safe local configuration handling with a committed `src/config.example.h`, ignored `src/config.h`, and a configurable P2 antenna selection define.

## v19.00 - 2026-05-08

- Added platform abstraction so the gateway supports Boron and Photon 2 / P2 from one codebase.
- Preserved LoRa RF settings and RadioHead mesh behavior while isolating transport-specific APIs to the platform shim.
- Added closed-hours gateway sleep suppression and gateway-provided overnight scheduling hints for nodes.
- Added Photon 2 / P2-specific carrier pin mapping and boot-time pin-mode logging.
- Added low-noise Photon 2 Wi-Fi/cloud connection metrics for soak-test diagnostics.
- Reduced diagnostic log noise by compile-gating verbose LoRa and system debug paths.
- Kept RadioHead library files unchanged.

## v13.00

- Changed the carrier-board `BUTTON_PIN` setup to `INPUT` so the user button behaves correctly on the current hardware.
- Bumped the gateway firmware and product version to `13.00` for this release.

## v12.00

- Unified Photon 2 and P2 temperature handling to read the TMP36 on `A5` while keeping `A4` configured as an input for safety.
- Documented that Photon 2/P2 temperature sensing only works on this carrier when `A4` and `A5` are physically bridged.
- Kept the guarded 25C fallback for invalid or out-of-range temperature readings so battery-related logic never trusts a bad TMP36 value.
