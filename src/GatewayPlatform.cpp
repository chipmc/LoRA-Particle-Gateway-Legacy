#include "GatewayPlatform.h"

#include "MyPersistentData.h"
#include "device_pinout.h"

namespace {

const char *const batteryContextValues[] = {
	"Unknown",
	"Not Charging",
	"Charging",
	"Charged",
	"Discharging",
	"Fault",
	"Disconnected",
	"N/A"
};

#if HAL_PLATFORM_CELLULAR
FuelGauge fuelGauge;
#endif

} // anonymous namespace

const char *gatewayPlatformName() {
#if HAL_PLATFORM_CELLULAR
	return "Boron";
#elif HAL_PLATFORM_WIFI
	return "Photon 2/P2";
#else
	return "Unknown";
#endif
}

const char *gatewayTransportName() {
#if HAL_PLATFORM_CELLULAR
	return "Cellular";
#elif HAL_PLATFORM_WIFI
	return "WiFi";
#else
	return "Unknown";
#endif
}

const char *gatewayReleaseString() {
	return GATEWAY_RELEASE_STRING;
}

void logGatewayBootHeader() {
	Log.info("Boot: platform=%s transport=%s deviceOS=%s release=%s flags(field=%d loraDiag=%d verbose=%d)", gatewayPlatformName(), gatewayTransportName(), System.version().c_str(), gatewayReleaseString(), FIELD_DEBUG_BUILD, LORA_DIAGNOSTICS, VERBOSE_SYSTEM_LOGS);
}

void startNetworkConnect() {
#if HAL_PLATFORM_WIFI
	if (!WiFi.ready()) {
		WiFi.on();
		WiFi.connect();
	}
#endif
	if (!Particle.connected()) {
		Particle.connect();
	}
}

bool isNetworkReady() {
#if HAL_PLATFORM_CELLULAR
	return Cellular.ready();
#elif HAL_PLATFORM_WIFI
	return WiFi.ready();
#else
	return false;
#endif
}

bool isCloudConnected() {
	return Particle.connected();
}

bool getGatewaySignalMetrics(int &strength, int &quality) {
#if HAL_PLATFORM_CELLULAR
	auto sig = Cellular.RSSI();
	strength = (int)sig.getStrength();
	quality = (int)sig.getQuality();
	return true;
#elif HAL_PLATFORM_WIFI
	auto sig = WiFi.RSSI();
	strength = (int)sig.getStrength();
	quality = (int)sig.getQuality();
	return true;
#else
	strength = 0;
	quality = 0;
	return false;
#endif
}

bool disconnectNetworkForSleep() {
	time_t startTime = Time.now();
	SYSTEM_VERBOSE_LOG("Disconnecting Particle cloud transport");
	Particle.disconnect();
	waitForNot(Particle.connected, 15000);
	Particle.process();
	if (Particle.connected()) {
		Log.info("Failed to disconnect from Particle cloud");
		return false;
	}
	Log.info("Disconnected from Particle cloud in %i seconds", (int)(Time.now() - startTime));

	startTime = Time.now();
#if HAL_PLATFORM_CELLULAR
	Cellular.disconnect();
	Cellular.off();
	waitFor(Cellular.isOff, 30000);
	Particle.process();
	if (Cellular.isOn()) {
		Log.info("Failed to turn off the cellular modem");
		return false;
	}
	Log.info("Turned off the cellular modem in %i seconds", (int)(Time.now() - startTime));
	return true;
#elif HAL_PLATFORM_WIFI
	WiFi.disconnect();
	WiFi.off();
	waitForNot(WiFi.ready, 15000);
	Particle.process();
	if (WiFi.ready()) {
		Log.info("Failed to turn off WiFi");
		return false;
	}
	Log.info("Turned off WiFi in %i seconds", (int)(Time.now() - startTime));
	return true;
#else
	return true;
#endif
}

const char *gatewayBatteryContext(uint8_t batteryState) {
	if (batteryState >= (sizeof(batteryContextValues) / sizeof(batteryContextValues[0]))) {
		return batteryContextValues[GATEWAY_BATTERY_STATE_NA];
	}
	return batteryContextValues[batteryState];
}

bool platformBatterySupported() {
#if HAL_PLATFORM_CELLULAR
	return true;
#else
	return false;
#endif
}

void platformPrepareBatteryMeasurement() {
#if HAL_PLATFORM_CELLULAR
	fuelGauge.quickStart();
#endif
}

bool platformReadBatteryState() {
#if HAL_PLATFORM_CELLULAR
	current.set_stateOfCharge(System.batteryCharge());
	return current.get_stateOfCharge() > 60;
#else
	current.set_stateOfCharge(0.0);
	current.set_batteryState(GATEWAY_BATTERY_STATE_NA);
	return true;
#endif
}

bool platformApplyChargePolicy(int internalTempC) {
#if HAL_PLATFORM_CELLULAR
	if (internalTempC < 0 || internalTempC > 37) {
		if (!initializePowerCfg(false)) {
			current.set_batteryState(1);
			SYSTEM_VERBOSE_LOG("Charging disabled - temp is %iC", internalTempC);
			return true;
		}
		Log.error("Unable to disable charging");
		current.set_batteryState(0);
		return false;
	}

	if (!initializePowerCfg(true)) {
		current.set_batteryState(System.batteryState());
		SYSTEM_VERBOSE_LOG("Charging enabled - inside temp range");
		return true;
	}
	Log.error("Unable to enable charging");
	current.set_batteryState(0);
	return false;
#else
	(void) internalTempC;
	current.set_batteryState(GATEWAY_BATTERY_STATE_NA);
	return true;
#endif
}

void logSignalStrength() {
#if HAL_PLATFORM_CELLULAR
	const char *radioTech[] = {"Unknown", "None", "WiFi", "GSM", "UMTS", "CDMA", "LTE", "IEEE802154", "LTE_CAT_M1", "LTE_CAT_NB1"};
	auto sig = Cellular.RSSI();
	auto rat = sig.getAccessTechnology();
	Log.info("%s S:%2.0f%%, Q:%2.0f%%", radioTech[rat], sig.getStrength(), sig.getQuality());
#elif HAL_PLATFORM_WIFI
	auto sig = WiFi.RSSI();
	Log.info("WiFi S:%2.0f%%, Q:%2.0f%%", sig.getStrength(), sig.getQuality());
#endif
}