// Battery conect information - https://docs.particle.io/reference/device-os/firmware/boron/#batterystate-
const char* batteryContext[7] = {"Unknown","Not Charging","Charging","Charged","Discharging","Fault","Diconnected"};

//Particle Functions
#include "Particle.h"
#include "take_measurements.h"
#include "device_pinout.h"
#include "MyPersistentData.h"
#include "power_management.h"

#if HAL_PLATFORM_CELLULAR
FuelGauge fuelGauge;                                // Needed to address issue with updates in low battery state
#endif

namespace {

uint8_t fallbackInternalTempC() {
  uint8_t previous = current.get_internalTempC();
  return (previous > 0 && previous < 80) ? previous : 25;
}

int readTmp36Raw(pin_t pin) {
  analogRead(pin);
  delayMicroseconds(50);

  const int sampleCount = 8;
  int rawSum = 0;
  for (int index = 0; index < sampleCount; index++) {
    rawSum += analogRead(pin);
    delayMicroseconds(50);
  }

  return rawSum / sampleCount;
}

uint8_t readInternalTempC() {
  int raw = readTmp36Raw(TMP36_SENSE_PIN);

  bool sensorOk = (raw > 50 && raw < 4000);
  float tempC = tmp36TemperatureC(raw);

  if (!sensorOk || tempC < 0.0f || tempC > 80.0f) {
    uint8_t fallback = fallbackInternalTempC();
    Log.warn("TMP36 reading invalid or out of range (tmp36=%.2f C, raw=%d, sensorOk=%s) - falling back to %uC",
        (double)tempC, raw, sensorOk ? "true" : "false", fallback);
    return fallback;
  }

  Log.info("TMP36 valid temp %.2fC (raw=%d) - using for battery charging limits",
      (double)tempC, raw);

  return (uint8_t)(tempC + 0.5f);
}

float readBatteryVoltage() {
#if HAL_PLATFORM_CELLULAR
  return fuelGauge.getVCell();
#elif PLATFORM_ID == 32 || PLATFORM_ID == 34
  int raw = analogRead(A6);
  return raw / 819.2f;
#else
  return 0.0f;
#endif
}

bool hasExternalPowerSource() {
#if HAL_PLATFORM_POWER_MANAGEMENT && HAL_PLATFORM_PMIC_BQ24195
  PMIC pmic;
  return pmic.isPowerGood();
#else
  return false;
#endif
}

void updatePowerManagementFromObservation() {
  PowerObservation obs = {};
  int batteryStateCode = current.get_batteryState();

  obs.batterySoc = current.get_stateOfCharge();
  obs.batteryVoltage = readBatteryVoltage();
  obs.temperatureF = (current.get_internalTempC() * 9.0f / 5.0f) + 32.0f;
  obs.inputPowerPresent = hasExternalPowerSource();
  obs.batteryIsCharging = batteryStateCode == 2;
  obs.batteryNotCharging = batteryStateCode == 1;
  obs.batteryFault = batteryStateCode == 5;

  updatePowerManagementObservation(obs);
}

}

bool takeMeasurements() { 

  #if HAL_PLATFORM_CELLULAR
    fuelGauge.quickStart();                         // Start the fuel gauge
    softDelay(1000);                                // Give the fuel gauge time to start
  #endif

  // Temperature inside the enclosure
  current.set_internalTempC(readInternalTempC());

  batteryState();
  updatePowerManagementFromObservation();

  Log.info("Battery State: %s, SOC: %2.0f%%",batteryContext[current.get_batteryState()],current.get_stateOfCharge());

  if (sysStatus.get_nodeNumber() == 0 ) getSignalStrength();

  return 1;

}

float getBatteryVoltageForDiagnostics() {
  return readBatteryVoltage();
}

bool getExternalPowerPresentForDiagnostics() {
  return hasExternalPowerSource();
}


float tmp36TemperatureC (int adcValue) { 
    // Analog inputs have values from 0-4095, or
    // 12-bit precision. 0 = 0V, 4095 = 3.3V, 0.0008 volts (0.8 mV) per unit
    // The temperature sensor docs use millivolts (mV), so use 3300 as the factor instead of 3.3.
    float mV = ((float)adcValue) * 3300 / 4095;

    // According to the TMP36 docs:
    // Offset voltage 500 mV, scaling 10 mV/deg C, output voltage at 25C = 750 mV (77F)
    // The offset voltage is subtracted from the actual voltage, allowing negative temperatures
    // with positive voltages.

    // Example value=969 mV=780.7 tempC=28.06884765625 tempF=82.52392578125

    // With the TMP36, with the flat side facing you, the pins are:
    // Vcc | Analog Out | Ground
    // You must put a 0.1 uF capacitor between the analog output and ground or you'll get crazy
    // inaccurate values!
    return (mV - 500) / 10;
}


bool batteryState() {
  #if PLATFORM_ID == PLATFORM_BORON
    current.set_stateOfCharge(System.batteryCharge());                 // Assign to system value
    current.set_batteryState(System.batteryState());
    if (current.get_stateOfCharge() > 60) return true;
    else return false;
  #elif PLATFORM_ID == 32 || PLATFORM_ID == 34
    // Photon 2 / P2 do not expose Boron fuel-gauge APIs. Estimate SoC from VBAT.
    float voltage = readBatteryVoltage();
    double stateOfCharge = (voltage - 3.0f) * (100.0f / (4.2f - 3.0f));

    if (stateOfCharge < 0.0f) {
      stateOfCharge = 0.0f;
    }
    else if (stateOfCharge > 100.0f) {
      stateOfCharge = 100.0f;
    }

    current.set_stateOfCharge(stateOfCharge);
    current.set_batteryState(0);                                       // Unknown without PMIC/fuel gauge

    if (current.get_stateOfCharge() > 60) return true;
    else return false;
  #else
    current.set_stateOfCharge(0);
    current.set_batteryState(0);
    return true;
  #endif
}


void getSignalStrength() {
  char signalStr[32];
  #if HAL_PLATFORM_CELLULAR
    const char* radioTech[10] = {"Unknown","None","WiFi","GSM","UMTS","CDMA","LTE","IEEE802154","LTE_CAT_M1","LTE_CAT_NB1"};
    CellularSignal sig = Cellular.RSSI();
    auto rat = sig.getAccessTechnology();
    float strengthPercentage = sig.getStrength();
    float qualityPercentage = sig.getQuality();

    if (strengthPercentage < 0 || qualityPercentage < 0) return;

    snprintf(signalStr, sizeof(signalStr), "%s S:%2.0f%%, Q:%2.0f%% ", radioTech[rat], strengthPercentage, qualityPercentage);
    Log.info(signalStr);
  #elif HAL_PLATFORM_WIFI
    WiFiSignal sig = WiFi.RSSI();
    float strengthPercentage = sig.getStrength();
    float qualityPercentage = sig.getQuality();

    if (strengthPercentage < 0 || qualityPercentage < 0) return;

    snprintf(signalStr, sizeof(signalStr), "WiFi S:%2.0f%%, Q:%2.0f%% ", strengthPercentage, qualityPercentage);
    Log.info(signalStr);
  #endif
}


bool recordCount() // This is where we check to see if an interrupt is set when not asleep or act on a tap that woke the device
{
  pinSetFast(BLUE_LED);                                                                               // Turn on the blue LED

  current.set_lastCountTime(Time.now());
  current.set_hourlyCount(current.get_hourlyCount() +1);                                              // Increment the PersonCount
  current.set_dailyCount(current.get_dailyCount() +1);                                               // Increment the PersonCount
  Log.info("Count, hourly: %i. daily: %i",current.get_hourlyCount(),current.get_dailyCount());
  delay(200);
  pinResetFast(BLUE_LED);

  return true;
}

/**
 * @brief soft delay let's us process Particle functions and service the sensor interrupts while pausing
 * 
 * @details takes a single unsigned long input in millis
 * 
 */
inline void softDelay(uint32_t t) {
  for (uint32_t ms = millis(); millis() - ms < t; Particle.process());  //  safer than a delay()
}