# Changelog

## v27.00 - 2026-06-16

### Protocol Clarification
- Clarified Gateway/Node v26/v27 protocol semantics: ACK bytes 6-7 carry scheduleIntervalMinutes with dual semantics controlled by openHours flag (byte 10).
- During open hours: scheduleIntervalMinutes = reporting cadence for boundary-aligned node scheduling.
- During closed hours: scheduleIntervalMinutes = minutes until next opening for relative node sleep.
- Updated protocol documentation to use scheduleIntervalMinutes terminology instead of ambiguous frequencyMinutes.
- Preserved functional behavior: Gateway sends time offsets during closed hours, cadence during open hours.
- Added clarifying comments to GatewayScheduleHint and NodeFrequencyState structs noting legacy field naming.

### NodeDB Persistence Hardening
- Enhanced JSON validator to verify NodeDB shape (root object with "nodes" array) in addition to syntax.
- Updated NodeDB parser sizing for 10-node support: 1024 bytes, 256 tokens (was 50 tokens).
- Defined shared constants: NODEDB_JSON_BYTES=1024, NODEDB_JSON_TOKENS=256, NODEDB_MAX_NODES=10.
- Matched validator token capacity to operational parser limits to avoid rejecting valid payloads.
- Fixed mutation functions (findNodeNumber, nodeUpdate, changeType, changeAlert) to return failure when FRAM save fails.
- Added writeNodeFrequencyState return value checks in DATA_ACK and JOIN_ACK paths with node-contextual logging.
- Implemented jp state restoration from FRAM after save failure to maintain in-memory/persisted consistency.
- Improved error visibility with concise diagnostics for invalid JSON loads (length + 60-char sanitized preview).
- Enhanced saveNodeIDJson return path to detect flush failures via dirty-flag check.

**Residual Risks:**
- Validator allocates ~6KB stack (JsonParserStatic<1024,256>) during validation. P2 has sufficient stack but worth monitoring.
- jp reload from FRAM after mutation is not atomic with the mutation operation.
- ACK operations may partially succeed (e.g., nodeUpdate succeeds but frequency update fails; ACK sent anyway).
- Non-persisted mutations may occur if force=false (deferred flush); global jp reflects changes not yet in FRAM.

## v25.00 - 2026-06-08

- Power back-off for low battery conditions.
- Hardened gateway data-report decode with explicit signed/unsigned 16-bit helpers while preserving packet format, field order, checksums, and ACK structure.
- Added LoRa diagnostics-gated decode trace logging of raw RSSI/SNR bytes and decoded values to verify signed handling without increasing normal log noise.
- Reclassified expected NodeDB persistence latency (including ACK-window persistence) to non-critical logs, while retaining `PersistCrit` for clearly abnormal durations above 1000 ms.

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
