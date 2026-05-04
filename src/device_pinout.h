/**
 * @file   device_pinout.h
 * @author Chip McClelland
 * @date   7-5-2022
 * @brief  File containing the pinout documentation and initializations
 * @note   Photon 2/P2 temperature sensing in this project requires the carrier
 *         to physically bridge A4 to A5. The firmware keeps A4 configured as
 *         an input for safety, reads the TMP36 from A5, and falls back to 25C
 *         if the bridged temperature signal is missing or out of bounds.
 * */

#ifndef DEVICE_PINOUT_H
#define DEVICE_PINOUT_H

#include "Particle.h"

// Pin definitions (changed from example code)
extern const pin_t RFM95_CS;                     // SPI Chip select pin - Standard SPI pins otherwise
extern const pin_t RFM95_RST;                     // Radio module reset
extern const pin_t RFM95_INT;                     // Interrupt from radio
extern const pin_t BUTTON_PIN;
extern const pin_t BLUE_LED;
extern const pin_t WAKEUP_PIN;   
extern const pin_t TMP36_SENSE_PIN;               // Photon 2/P2 require an A4-to-A5 bridge for TMP36 support in this app

// Specific to the sensor
extern const pin_t INT_PIN;
extern const pin_t MODULE_POWER_PIN;

bool initializePinModes();

#endif