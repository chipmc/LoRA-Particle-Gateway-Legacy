//Particle Functions
#include "Particle.h"
#include "device_pinout.h"

/**********************************************************************************************************************
 * ******************************         Boron Pinout Example               ******************************************
 * https://docs.particle.io/reference/datasheets/b-series/boron-datasheet/#pins-and-button-definitions
 *
 Left Side (16 pins)
 * !RESET -
 * 3.3V -
 * !MODE -
 * GND -
 * D19 - A0 -               
 * D18 - A1 -               
 * D17 - A2 -               
 * D16 - A3 -
 * D15 - A4 -               Internal (TMP32) Temp Sensor
 * D14 - A5 / SPI SS -      RFM9x
 * D13 - SCK - SPI Clock -  
 * D12 - MO - SPI MOSI -    RFM9x
 * D11 - MI - SPI MISO -    RFM9x
 * D10 - UART RX -
 * D9 - UART TX -

 Right Size (12 pins)
 * Li+
 * ENABLE
 * VUSB -
 * D8 -                     Wake Connected to Watchdog Timer
 * D7 -                     Blue Led
 * D6 -                     
 * D5 -                     
 * D4 -                     User Switch
 * D3 - 
 * D2 -                     RFM9x
 * D1 - SCL - I2C Clock -   FRAM / RTC and I2C Bus
 * D0 - SDA - I2C Data -    FRAM / RTX and I2C Bus
***********************************************************************************************************************/

//Define pins for the RFM9x on my Particle carrier board
const pin_t RFM95_CS =      D5;                     // SPI Chip select pin - Standard SPI pins otherwise was A5
const pin_t RFM95_RST =     D6;                     // Radio module reset was D3
const pin_t RFM95_INT =     D2;                     // Interrupt from radio
// Carrier Board standard pins
#if defined(MUON_TMP36_SENSE_PIN)
const pin_t TMP36_SENSE_PIN   = MUON_TMP36_SENSE_PIN;
const pin_t WAKEUP_PIN        = WKP;
#elif (defined(PLATFORM_P2) && (PLATFORM_ID == PLATFORM_P2)) || (defined(PLATFORM_PHOTON2) && (PLATFORM_ID == PLATFORM_PHOTON2))
const pin_t TMP36_SENSE_PIN   = A5;                 // Photon 2/P2 use A5 when the carrier bridges A4 to A5 for TMP36 sensing
const pin_t WAKEUP_PIN        = WKP;                // Photon 2 wake-capable pin
#else
const pin_t TMP36_SENSE_PIN   = A4;
const pin_t WAKEUP_PIN        = D8;
#endif
const pin_t BUTTON_PIN        = D4;
const pin_t BLUE_LED          = D7;
// Sensor specific Pins
// Specific to the sensor
extern const pin_t INT_PIN = A1;                   // May need to change this
extern const pin_t MODULE_POWER_PIN = A2;          // Make sure we document this above
const pin_t LED_POWER_PIN = A3;

bool initializePinModes() {
    Log.info("Initalizing the pinModes");
    // Define as inputs or outputs
    pinMode(BUTTON_PIN,INPUT);               // User button on the carrier board - active LOW
    pinMode(WAKEUP_PIN,INPUT);                      // This pin is active HIGH
#if (defined(PLATFORM_PHOTON2) && (PLATFORM_ID == PLATFORM_PHOTON2)) || (defined(PLATFORM_P2) && (PLATFORM_ID == PLATFORM_P2))
    pinMode(A4, INPUT);                             // Keep bridged A4/A5 TMP36 node high-impedance on Photon 2 carriers
#endif
    pinMode(TMP36_SENSE_PIN, INPUT);                // Temperature sense pin is always configured once during startup
    pinMode(BLUE_LED,OUTPUT);                       // On the Boron itself
    pinMode(INT_PIN, INPUT);
    pinMode(MODULE_POWER_PIN, OUTPUT);
    pinMode(LED_POWER_PIN,OUTPUT);
    digitalWrite(LED_POWER_PIN,LOW);                // Turns on the LEd on the PIR sensor
    digitalWrite(MODULE_POWER_PIN,LOW);             // Enable (LOW) or disable (HIGH) the sensor
    pinMode(RFM95_RST,OUTPUT);
    digitalWrite(RFM95_RST,HIGH);
    return true;
}


bool initializePowerCfg(bool enableCharging) {
    #if PLATFORM_ID == PLATFORM_BORON
        Log.info("Initializing Power Config");
        const int maxCurrentFromPanel = 900;            // Not currently used (100,150,500,900,1200,2000 - will pick closest) (550mA for 3.5W Panel, 340 for 2W panel)
        SystemPowerConfiguration conf;
        System.setPowerConfiguration(SystemPowerConfiguration());  // To restore the default configuration

        if (!enableCharging) {
            conf.feature(SystemPowerFeature::DISABLE_CHARGING);
        }
        else {
            conf.powerSourceMaxCurrent(maxCurrentFromPanel) // Set maximum current the power source can provide  3.5W Panel (applies only when powered through VIN)
            .powerSourceMinVoltage(5080) // Set minimum voltage the power source can provide (applies only when powered through VIN)
            .batteryChargeCurrent(maxCurrentFromPanel) // Set battery charge current
            .batteryChargeVoltage(4208) // Set battery termination voltage
            .feature(SystemPowerFeature::USE_VIN_SETTINGS_WITH_USB_HOST); // For the cases where the device is powered through VIN
                                                                         // but the USB cable is connected to a USB host, this feature flag
                                                                         // enforces the voltage/current limits specified in the configuration
                                                                         // (where by default the device would be thinking that it's powered by the USB Host)
        }
        int res = System.setPowerConfiguration(conf); // returns SYSTEM_ERROR_NONE (0) in case of success
        return res;
    #else
        (void)enableCharging;
        return true;
    #endif
}
                
