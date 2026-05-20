#pragma once

// Shared gateway operational configuration.
// This file is committed and should not contain local secrets.

// -----------------------------------------------------------------------------
// Build / Diagnostics
// -----------------------------------------------------------------------------

#define UPDATE_WIFI_CREDENTIALS 0

#define FIELD_DEBUG_BUILD 0

#define LORA_RAW_TEST 0

#define LORA_DIAGNOSTICS 0

#define VERBOSE_SYSTEM_LOGS 0

// -----------------------------------------------------------------------------
// Time / Timezone
// -----------------------------------------------------------------------------

#define GATEWAY_TIME_SYNC_INTERVAL_SECONDS (24UL * 60UL * 60UL)
#define GATEWAY_DEV_TIME_SYNC_INTERVAL_SECONDS 0UL
#define GATEWAY_RTC_DRIFT_LOG_THRESHOLD_SECONDS 30UL
#define GATEWAY_LOCAL_TIMEZONE_POSIX "SGT-8"
#define GATEWAY_LOCAL_TIMEZONE_LABEL "SGT"

// -----------------------------------------------------------------------------
// WiFi
// -----------------------------------------------------------------------------

// Photon 2 / P2 WiFi antenna selection.
// 0 = internal chip antenna, 1 = external u.FL antenna.
#define P2_WIFI_ANTENNA_EXTERNAL 0

// 1 = hidden SSID, 0 = visible SSID.
#define WIFI_HIDDEN_SSID 0

#if defined(__has_include)
#if __has_include("local_secrets.h")
#include "local_secrets.h"
#endif
#endif

#ifndef WIFI_SSID
#define WIFI_SSID "insert SSID here"
#endif

#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD "insert password here"
#endif

// -----------------------------------------------------------------------------
// LoRa Timing
// -----------------------------------------------------------------------------

#define DEFAULT_LORA_WINDOW 5

// -----------------------------------------------------------------------------
// Cloud Connectivity
// -----------------------------------------------------------------------------

#define STAY_CONNECTED 60
#define PARTICLE_SYNC_TIMEOUT_MS 45000UL

// -----------------------------------------------------------------------------
// Sleep / Power
// -----------------------------------------------------------------------------

#define DISCONNECTING_HARD_TIMEOUT_MS 180000UL

// -----------------------------------------------------------------------------
// Watchdog / Safety
// -----------------------------------------------------------------------------

#define PARTICLE_CONNECT_GUARD_TIMEOUT_MS (6UL * 60UL * 60UL * 1000UL)

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