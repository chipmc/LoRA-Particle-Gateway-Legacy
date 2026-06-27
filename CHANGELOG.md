# Changelog

## v27.00 - 2026-06-27

### Production Hardening Summary

Version 27 production hardening release validated and ready for deployment.

**Validated Behaviors:**
- ✅ PublishQueue drain behavior under WiFi outage scenarios
- ✅ Command window behavior and timing
- ✅ RecoveryListen hardening and retry logic
- ✅ Timestamp-safe node health tracking across time sync events
- ✅ WiFi outage queue accumulation and drain cycles
- ✅ Clean compilation for Boron and Photon 2/P2 platforms

**Key Improvements:**
- Enhanced NodeDB persistence hardening with comprehensive JSON validation
- ACK Protocol v1 contamination fix prevents transient schedule intervals from corrupting NodeDB frequency fields
- Improved mutation failure handling with FRAM save verification
- Separated transient ACK scheduling from persistent node configuration

### ACK Protocol v1 NodeDB Contamination Fix (2026-06-22)
**Problem:** Gateway v27 persisted transient ACK schedule intervals into NodeDB frequency fields during outage recovery, causing false FrequencyChange logs (60 → 17 → 12 → 60).

**Root Cause:** `acknowledgeDataReportGateway()` and `acknowledgeJoinRequestGateway()` used `scheduleHint.frequencyMinutes` for both ACK bytes 6-7 (correct) and NodeDB persistence (incorrect).

**Solution:** Separated two concepts:
- `ackScheduleIntervalMinutes` = transient ACK interval for wire protocol (ACK bytes 6-7)
- `configuredReportFrequencyMinutes` = persistent NodeDB cadence from `gatewayDesiredReportFrequencyMinutes()` (includes pending manual/cloud updates and battery-backoff cadence)

**Impact:**
- ACK v1 wire protocol preserved (no Node firmware changes required)
- NodeDB frequency fields now stable across transient ACK interval changes
- FrequencyChange logs only emit when actual configured cadence changes (including pending updates)
- Unnecessary NodeDB saves avoided when only ACK interval varies
- Pending manual/cloud frequency updates properly reflected in NodeDB persistence

**Changed:** [src/LoRA_Functions.cpp](src/LoRA_Functions.cpp)
- DATA_ACK and JOIN_ACK handlers now persist `configuredReportFrequencyMinutes` from `gatewayDesiredReportFrequencyMinutes()` instead of transient `ackScheduleIntervalMinutes`
- Updated GatewayScheduleHint struct comment to clarify dual semantics and persistence guidance
- Added inline comments distinguishing transient ACK interval from persistent cadence
- Fixed trailing whitespace

### Protocol Clarification (ACK Protocol v1)
- **ACK bytes 6-7 semantics:** Gateway sends scheduleIntervalMinutes with dual semantics controlled by openHours flag (byte 10).
- **During open hours:** Gateway sends `minutesUntilNextGatewayWindow()` — the actual minutes until the next aligned boundary window, NOT the reporting cadence.
- **During closed hours:** Gateway sends `minutesUntilNextOpening()` — minutes until next opening for relative node sleep.
- **Legacy Node v26 interpretation:**
  - During open hours: Node uses bytes 6-7 as a boundary interval (wall-clock alignment).
  - During closed hours: Node uses bytes 6-7 as relative sleep duration.
  - Known limitation: After outage recovery during open hours, Node may misinterpret minutesUntilNextGatewayWindow as a new cadence if the value differs from its stored frequency.
- Updated protocol documentation to use scheduleIntervalMinutes terminology instead of ambiguous frequencyMinutes.
- Added clarifying comments to GatewayScheduleHint and NodeFrequencyState structs noting legacy field naming.

**Known ACK Protocol v1 Limitations:**
- ACK bytes 6-7 conflate two concepts: periodic reporting cadence vs one-time next-wake target.
- Legacy Nodes may misinterpret minutesUntilNextGatewayWindow during open hours as a cadence change.
- No version-gated protocol negotiation exists for safe protocol evolution.

**Future Work (ACK Protocol v2):**
- Separate report cadence (periodic) from next wake target (one-time).
- Add protocol version negotiation for backward-compatible rollout.
- Require coordinated Gateway/Node firmware updates with version gating.

### NodeDB Persistence Hardening
- Enhanced JSON validator to verify NodeDB shape (root object with "nodes" array) in addition to syntax.
- Validator uses file-scope static allocation (JsonParserStatic<1024,256>) to avoid stack allocation spikes.
- Updated NodeDB parser sizing for 10-node support: 1024 bytes, 256 tokens (was 50 tokens).
- Defined shared constants: NODEDB_JSON_BYTES=1024, NODEDB_JSON_TOKENS=256, NODEDB_MAX_NODES=10.
- Matched validator token capacity to operational parser limits to avoid rejecting valid payloads.
- Fixed mutation functions (findNodeNumber, nodeUpdate, changeType, changeAlert) to return failure when FRAM save fails.
- Added writeNodeFrequencyState return value check in JOIN_ACK path with node-contextual logging.
- Implemented jp state restoration from FRAM after save failure to maintain in-memory/persisted consistency.
- Improved error visibility with concise diagnostics for invalid JSON loads (length + 60-char sanitized preview).
- Enhanced saveNodeIDJson return path to detect flush failures via dirty-flag check.

**Residual Risks:**
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
