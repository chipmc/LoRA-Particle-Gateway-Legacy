#include "GatewayPlatform.h"

#include <math.h>
#include <string.h>

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
	"N/A",
	"Estimated"
};

const float MIN_VALID_GATEWAY_BATTERY_VOLTAGE = 3.0f;
const float MAX_VALID_GATEWAY_BATTERY_VOLTAGE = 4.5f;

struct VoltageSocPoint {
	float voltage;
	float soc;
};

const VoltageSocPoint conservativeSocPoints[] = {
	{3.50f, 10.0f},
	{3.70f, 40.0f},
	{3.85f, 60.0f},
	{4.00f, 80.0f},
	{4.10f, 90.0f},
	{4.18f, 98.0f},
};

GatewayBatteryTelemetry lastBatteryTelemetryCache = {false, 0.0f, 0.0f, "N/A", "unsupported"};

#if HAL_PLATFORM_CELLULAR
FuelGauge fuelGauge;
#endif

bool isValidGatewayBatteryVoltage(float voltage) {
	return !isnan(voltage) && voltage >= MIN_VALID_GATEWAY_BATTERY_VOLTAGE && voltage <= MAX_VALID_GATEWAY_BATTERY_VOLTAGE;
}

float clampBatteryPercent(float value) {
	if (isnan(value)) return value;
	if (value < 0.0f) return 0.0f;
	if (value > 100.0f) return 100.0f;
	return value;
}

float estimateConservativeGatewaySoc(float voltage) {
	if (!isValidGatewayBatteryVoltage(voltage)) return NAN;
	if (voltage <= conservativeSocPoints[0].voltage) return conservativeSocPoints[0].soc;
	if (voltage >= conservativeSocPoints[(sizeof(conservativeSocPoints) / sizeof(conservativeSocPoints[0])) - 1].voltage) return 98.0f;

	for (size_t index = 1; index < (sizeof(conservativeSocPoints) / sizeof(conservativeSocPoints[0])); index++) {
		if (voltage <= conservativeSocPoints[index].voltage) {
			const float lowerVoltage = conservativeSocPoints[index - 1].voltage;
			const float upperVoltage = conservativeSocPoints[index].voltage;
			const float lowerSoc = conservativeSocPoints[index - 1].soc;
			const float upperSoc = conservativeSocPoints[index].soc;
			const float position = (voltage - lowerVoltage) / (upperVoltage - lowerVoltage);
			return clampBatteryPercent(lowerSoc + position * (upperSoc - lowerSoc));
		}
	}

	return 98.0f;
}

} // anonymous namespace

namespace GatewayPlatform {

GatewayBatteryTelemetry readBatteryTelemetry() {
	GatewayBatteryTelemetry telemetry = {false, 0.0f, 0.0f, "N/A", "unsupported"};

#if HAL_PLATFORM_CELLULAR
	telemetry.available = true;
	telemetry.soc = clampBatteryPercent(System.batteryCharge());
	telemetry.voltage = fuelGauge.getVCell();
	telemetry.contextLabel = gatewayBatteryContext((uint8_t)System.batteryState());
	telemetry.sourceLabel = "fuel_gauge";
#elif HAL_PLATFORM_WIFI
	const int rawAdc = analogRead(A6);
	SYSTEM_VERBOSE_LOG("Gateway battery raw ADC: %d", rawAdc);
	const float voltage = ((float)rawAdc) / 819.2f;
	if (isValidGatewayBatteryVoltage(voltage)) {
		telemetry.available = true;
		telemetry.soc = estimateConservativeGatewaySoc(voltage);
		telemetry.voltage = voltage;
		telemetry.contextLabel = "Estimated";
		telemetry.sourceLabel = "vbat_adc";
	}
	else {
		telemetry.voltage = 0.0f;
		telemetry.sourceLabel = "vbat_adc";
	}
#endif

	lastBatteryTelemetryCache = telemetry;
	return telemetry;
}

GatewayBatteryTelemetry lastBatteryTelemetry() {
	return lastBatteryTelemetryCache;
}

} // namespace GatewayPlatform

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
	Log.info("Boot: FW=%s platform=%s transport=%s deviceOS=%s", GATEWAY_FW_VERSION, gatewayPlatformName(), gatewayTransportName(), System.version().c_str());
	Log.info("Boot: BuildID=%s", GATEWAY_FW_BUILD_ID);
	Log.info("Boot: flags(field=%d loraDiag=%d verbose=%d) resetReason=%d resetData=%lu", FIELD_DEBUG_BUILD, LORA_DIAGNOSTICS, VERBOSE_SYSTEM_LOGS, (int)System.resetReason(), (unsigned long)System.resetReasonData());
}

void startNetworkConnect() {
#if HAL_PLATFORM_WIFI
	static bool antennaSelected = false;
	if (!WiFi.ready()) {
		WiFi.on();
		#if MUON_IS_P2_FAMILY
		if (!antennaSelected) {
			const int antennaResult = WiFi.selectAntenna(P2_WIFI_ANTENNA_EXTERNAL ? ANT_EXTERNAL : ANT_INTERNAL);
			if (antennaResult == 0) {
				SYSTEM_VERBOSE_LOG("Configured P2 WiFi antenna: %s", P2_WIFI_ANTENNA_EXTERNAL ? "external" : "internal");
			}
			else {
				Log.warn("Failed to configure P2 WiFi antenna: mode=%s result=%d", P2_WIFI_ANTENNA_EXTERNAL ? "external" : "internal", antennaResult);
			}
			antennaSelected = true;
		}
		#endif
		#if UPDATE_WIFI_CREDENTIALS
		WiFiCredentials credentials;
		credentials.setSsid(WIFI_SSID)
			.setPassword(WIFI_PASSWORD)
			.setSecurity(WPA2)
			.setHidden(WIFI_HIDDEN_SSID != 0);
		if (WiFi.setCredentials(credentials)) {
			Log.info("Updated stored WiFi credentials for SSID '%s' (hidden=%d)", WIFI_SSID, WIFI_HIDDEN_SSID);
		}
		else {
			Log.error("Failed to update stored WiFi credentials for SSID '%s'", WIFI_SSID);
		}
		#else
		Log.info("Using stored WiFi credentials");
		#endif
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

uint8_t gatewayBatteryContextCode(const GatewayBatteryTelemetry &telemetry) {
	if (!telemetry.available || !telemetry.contextLabel) {
		return GATEWAY_BATTERY_STATE_NA;
	}

	for (uint8_t index = 0; index < (sizeof(batteryContextValues) / sizeof(batteryContextValues[0])); index++) {
		if (strcmp(telemetry.contextLabel, batteryContextValues[index]) == 0) {
			return index;
		}
	}

	return GATEWAY_BATTERY_STATE_NA;
}

bool platformBatterySupported() {
#if HAL_PLATFORM_CELLULAR || HAL_PLATFORM_WIFI
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
	const GatewayBatteryTelemetry telemetry = GatewayPlatform::readBatteryTelemetry();
	current.set_stateOfCharge(telemetry.available ? telemetry.soc : 0.0);
	current.set_batteryState(gatewayBatteryContextCode(telemetry));
	return telemetry.available && telemetry.soc > 60.0f;
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
#elif HAL_PLATFORM_WIFI
	(void) internalTempC;
	return true;
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
	if (!Cellular.ready() || sig.getStrength() < 0.0f || sig.getQuality() < 0.0f) {
		return;
	}
	auto rat = sig.getAccessTechnology();
	Log.info("%s S:%2.0f%%, Q:%2.0f%%", radioTech[rat], sig.getStrength(), sig.getQuality());
#elif HAL_PLATFORM_WIFI
	if (!WiFi.ready()) {
		return;
	}
	auto sig = WiFi.RSSI();
	if (sig.getStrength() < 0.0f || sig.getQuality() < 0.0f) {
		return;
	}
	Log.info("WiFi S:%2.0f%%, Q:%2.0f%%", sig.getStrength(), sig.getQuality());
#endif
}