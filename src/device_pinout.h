/**
 * @file   device_pinout.h
 * @author Chip McClelland
 * @date   7-5-2022
 * @brief  File containing the pinout documentation and initializations
 * */

#ifndef DEVICE_PINOUT_H
#define DEVICE_PINOUT_H

#include "Particle.h"

#if (defined(PLATFORM_P2) && (PLATFORM_ID == PLATFORM_P2)) || (defined(PLATFORM_PHOTON2) && (PLATFORM_ID == PLATFORM_PHOTON2))
#define MUON_IS_P2_FAMILY 1
#else
#define MUON_IS_P2_FAMILY 0
#endif

// Pin definitions (changed from example code)
extern const pin_t RFM95_CS;                     // SPI Chip select pin - Standard SPI pins otherwise
extern const pin_t RFM95_RST;                     // Radio module reset
extern const pin_t RFM95_INT;                     // Interrupt from radio
extern const pin_t BUTTON_PIN;
extern const pin_t BLUE_LED;
extern const pin_t WAKEUP_PIN;   
extern const pin_t TMP36_SENSE_PIN;

// Specific to the sensor
extern const pin_t INT_PIN;
extern const pin_t MODULE_POWER_PIN;

bool initializePinModes();
bool initializePowerCfg(bool enableCharging);

#endif