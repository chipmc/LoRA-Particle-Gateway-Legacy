#pragma once

// Wi-Fi provisioning for Photon 2 / P2.
// Workflow:
// 1. Set UPDATE_WIFI_CREDENTIALS to 1.
// 2. Set WIFI_SSID and WIFI_PASSWORD below.
// 3. Flash once and confirm the device connects.
// 4. Set UPDATE_WIFI_CREDENTIALS back to 0.
// 5. Flash again to remove plaintext credentials from the firmware.

#define UPDATE_WIFI_CREDENTIALS 0

#define FIELD_DEBUG_BUILD 1

#define LORA_RAW_TEST 0

// 1 = hidden SSID, 0 = visible SSID.
// Hidden SSIDs are less reliable for embedded devices.
// Only set WIFI_HIDDEN_SSID to 1 if the network is actually hidden.
#define WIFI_HIDDEN_SSID 0

#define WIFI_SSID "REDACTED_WIFI_SSID"
#define WIFI_PASSWORD "REDACTED_WIFI_PASSWORD"

#if UPDATE_WIFI_CREDENTIALS
	namespace wifi_config_validation {
		constexpr bool stringsEqual(const char *left, const char *right) {
			while (*left != '\0' || *right != '\0') {
				if (*left != *right) {
					return false;
				}
				++left;
				++right;
			}
			return true;
		}
	}

	static_assert(sizeof(WIFI_SSID) > 1, "WIFI_SSID must be set when UPDATE_WIFI_CREDENTIALS is 1");
	static_assert(!wifi_config_validation::stringsEqual(WIFI_SSID, "insert SSID here"), "WIFI_SSID placeholder must be replaced when UPDATE_WIFI_CREDENTIALS is 1");
	static_assert(sizeof(WIFI_PASSWORD) > 1, "WIFI_PASSWORD must be set when UPDATE_WIFI_CREDENTIALS is 1");
	static_assert(!wifi_config_validation::stringsEqual(WIFI_PASSWORD, "insert password here"), "WIFI_PASSWORD placeholder must be replaced when UPDATE_WIFI_CREDENTIALS is 1");
#endif
