#ifndef GATEWAY_PLATFORM_H
#define GATEWAY_PLATFORM_H

#if defined(__has_include)
#if __has_include("config.h")
#include "config.h"
#else
#error "Missing src/config.h. Restore the committed file or rebuild it from src/config.example.h."
#endif
#else
#include "config.h"
#endif

#include "Particle.h"

#ifndef FIELD_DEBUG_BUILD
#define FIELD_DEBUG_BUILD 0
#endif

#ifndef LORA_DIAGNOSTICS
#define LORA_DIAGNOSTICS FIELD_DEBUG_BUILD
#endif

#ifndef VERBOSE_SYSTEM_LOGS
#define VERBOSE_SYSTEM_LOGS FIELD_DEBUG_BUILD
#endif

#ifndef GATEWAY_TIME_SYNC_INTERVAL_SECONDS
#define GATEWAY_TIME_SYNC_INTERVAL_SECONDS (24UL * 60UL * 60UL)
#endif

#ifndef GATEWAY_DEV_TIME_SYNC_INTERVAL_SECONDS
#define GATEWAY_DEV_TIME_SYNC_INTERVAL_SECONDS 0UL
#endif

#ifndef GATEWAY_RTC_DRIFT_LOG_THRESHOLD_SECONDS
#define GATEWAY_RTC_DRIFT_LOG_THRESHOLD_SECONDS 30UL
#endif

#ifndef GATEWAY_LOCAL_TIMEZONE_POSIX
#define GATEWAY_LOCAL_TIMEZONE_POSIX "UTC0"
#endif

#ifndef GATEWAY_LOCAL_TIMEZONE_LABEL
#define GATEWAY_LOCAL_TIMEZONE_LABEL "UTC"
#endif

#ifndef DEFAULT_LORA_WINDOW
#define DEFAULT_LORA_WINDOW 5
#endif

#ifndef STAY_CONNECTED
#define STAY_CONNECTED 60
#endif

#ifndef PARTICLE_SYNC_TIMEOUT_MS
#define PARTICLE_SYNC_TIMEOUT_MS 45000UL
#endif

#ifndef PARTICLE_CONNECT_GUARD_TIMEOUT_MS
#define PARTICLE_CONNECT_GUARD_TIMEOUT_MS (6UL * 60UL * 60UL * 1000UL)
#endif

#ifndef DISCONNECTING_HARD_TIMEOUT_MS
#define DISCONNECTING_HARD_TIMEOUT_MS 180000UL
#endif

#ifndef UPDATE_WIFI_CREDENTIALS
#define UPDATE_WIFI_CREDENTIALS 0
#endif

#ifndef P2_WIFI_ANTENNA_EXTERNAL
#define P2_WIFI_ANTENNA_EXTERNAL 0
#endif

#ifndef HAL_PLATFORM_CELLULAR
#define HAL_PLATFORM_CELLULAR 0
#endif

#ifndef HAL_PLATFORM_WIFI
#define HAL_PLATFORM_WIFI 0
#endif

#if LORA_DIAGNOSTICS
#define LORA_DIAG_LOG(...) Log.info(__VA_ARGS__)
#else
#define LORA_DIAG_LOG(...) do { } while (0)
#endif

#if VERBOSE_SYSTEM_LOGS
#define SYSTEM_VERBOSE_LOG(...) Log.info(__VA_ARGS__)
#else
#define SYSTEM_VERBOSE_LOG(...) do { } while (0)
#endif

#define GATEWAY_PRODUCT_VERSION 25
#define GATEWAY_RELEASE_STRING "25.00"
#define GATEWAY_FW_VERSION GATEWAY_RELEASE_STRING
#define GATEWAY_FW_BUILD_ID GATEWAY_RELEASE_STRING " " __DATE__ " " __TIME__

static const uint8_t GATEWAY_BATTERY_STATE_NA = 7;
static const uint8_t GATEWAY_BATTERY_STATE_ESTIMATED = 8;

struct GatewayBatteryTelemetry {
	bool available;
	float soc;
	float voltage;
	const char *contextLabel;
	const char *sourceLabel;
};

namespace GatewayPlatform {
	GatewayBatteryTelemetry readBatteryTelemetry();
	GatewayBatteryTelemetry lastBatteryTelemetry();
}

const char *gatewayPlatformName();
const char *gatewayTransportName();
const char *gatewayReleaseString();
void logGatewayBootHeader();

void startNetworkConnect();
bool isNetworkReady();
bool isCloudConnected();
bool disconnectNetworkForSleep();
bool getGatewaySignalMetrics(int &strength, int &quality);

const char *gatewayBatteryContext(uint8_t batteryState);
uint8_t gatewayBatteryContextCode(const GatewayBatteryTelemetry &telemetry);
bool platformBatterySupported();
void platformPrepareBatteryMeasurement();
bool platformReadBatteryState();
bool platformApplyChargePolicy(int internalTempC);
void logSignalStrength();

#endif