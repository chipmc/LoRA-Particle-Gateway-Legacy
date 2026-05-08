# Changelog

## v19.00 - 2026-05-08

- Added platform abstraction so the gateway supports Boron and Photon 2 / P2 from one codebase.
- Preserved LoRa RF settings and RadioHead mesh behavior while isolating transport-specific APIs to the platform shim.
- Added closed-hours gateway sleep suppression and gateway-provided overnight scheduling hints for nodes.
- Added Photon 2 / P2-specific carrier pin mapping and boot-time pin-mode logging.
- Added low-noise Photon 2 Wi-Fi/cloud connection metrics for soak-test diagnostics.
- Reduced diagnostic log noise by compile-gating verbose LoRa and system debug paths.
- Kept RadioHead library files unchanged.