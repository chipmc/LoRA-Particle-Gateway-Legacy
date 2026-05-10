//Particle Functions
#include "Particle.h"
#include "take_measurements.h"
#include "device_pinout.h"
#include "MyPersistentData.h"
#include "GatewayPlatform.h"

bool takeMeasurements() { 

  platformPrepareBatteryMeasurement();
  #if HAL_PLATFORM_CELLULAR
    softDelay(1000);                                // Give the fuel gauge time to start
  #endif

  // Temperature inside the enclosure
  current.set_internalTempC((int)tmp36TemperatureC(analogRead(TMP36_SENSE_PIN)));

  batteryState();

  if (isItSafeToCharge()) {
    if (platformBatterySupported()) {
      SYSTEM_VERBOSE_LOG("Battery State: %s, SOC: %2.0f%%, VBAT=%.2f source=%s", gatewayBatteryContext(current.get_batteryState()), current.get_stateOfCharge(), GatewayPlatform::lastBatteryTelemetry().voltage, GatewayPlatform::lastBatteryTelemetry().sourceLabel);
    }
  }
  else if (platformBatterySupported()) {
    Log.error("Power configuration error");
  }

  if (sysStatus.get_nodeNumber() == 0 ) logSignalStrength();

  return 1;

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
  return platformReadBatteryState();
}


bool isItSafeToCharge()                             // Returns a true or false if the battery is in a safe charging range.
{
  return platformApplyChargePolicy(current.get_internalTempC());
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