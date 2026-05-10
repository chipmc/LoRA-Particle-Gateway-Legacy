#pragma once

// Safe committed template for local developer configuration.
// Copy this file to src/config.h before building:
// cp src/config.example.h src/config.h

#define UPDATE_WIFI_CREDENTIALS 0

#define FIELD_DEBUG_BUILD 0

#define LORA_RAW_TEST 0

#define VERBOSE_SYSTEM_LOGS 0

// Photon 2 / P2 WiFi antenna selection.
// 0 = internal chip antenna, 1 = external u.FL antenna.
#define P2_WIFI_ANTENNA_EXTERNAL 0

// 1 = hidden SSID, 0 = visible SSID.
// Hidden SSIDs are less reliable for embedded devices.
// Only set WIFI_HIDDEN_SSID to 1 if the network is actually hidden.
#define WIFI_HIDDEN_SSID 0

#define WIFI_SSID "insert SSID here"
#define WIFI_PASSWORD "insert password here"

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