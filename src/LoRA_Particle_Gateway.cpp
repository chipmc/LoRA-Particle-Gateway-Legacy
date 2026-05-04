/*
 * Project LoRA-Particle-Gateway
 * Description: This device will listen for data from client devices and forward the data to Particle via webhook
 * Author: Chip McClelland and Jeff Skarda
 * Date: 7-28-22
 */

// v0.01 - Started with the code from utilities - moving to a standard carrier board
// v0.02 - Adding sleep and scheduling using localTimeRK
// v0.03 - Refactored the code to make it more modular
// v0.04 - Works - refactored the LoRA functions
// v0.05 - Moved to the Storage Helper library
// v0.06 - Moved the LoRA Functions to a class implementation
// v0.07 - Moved to a class implementation for Particle Functions
// v0.08 - Simplified wake timing
// v0.09 - Added LoRA functions to clear the buffer and sleep the LoRA radio
// v0.10 - Stable - adding functionality
// v0.11 - Big changes to messaging and storage.  Onboards upto to 3 nodes.  Works!
// v0.12 - Added mandatory sync time on connect and check for empty queue before disconnect and node number mgt.
// v0.13 - Different webhooks for nodes and gateways. added reporting on cellular connection time and signal.  Added sensor type to join reques - need to figure out how to trigger
// v0.14 - Changing over to JsonParserGeneratorRK for node data, storage, webhook creation and Particle function / variable
// v0.15 - Completing move to class for Particle Functions, zero node alert code after send
// v0.16 - Fix for reporting frequency - using calculated variables
// v0.17 - Periodic health checks for connections to nodes
// v0.18 - Particle connection health check and continued move to classes
// v0.19 - Simpler reporting - adding connection success % to nodeData
// v1	- Release candidate - sending to Pilot Mountain
// v1.01- Working to improve consistency of loading persistent date
// v1.02 - udated the way I am implementing the StorageHelperRK function 
// v1.03 - reliability updates
// v1.04 - Added logic to check deviceID to validate node number
// v1.05 - Improved the erase node functions
// v2.00 - First Release for actual deployment
// V6.00 - Reliability Updates - Stays connected longer if a node fails to check in - deployed to Pilot Mountain on 2/16/23
// v7.00 - Added Signal to Noise Ratio to hourly reporting / webhook, battery level monitoring improvements, added power cycle function - Optimized for stick antenna - new center freq
// v9.00 - Breaking Change - v10 Node Required - Node Now Reports RSSI / SNR to Gateway, Simplified Join Request Logic, Storing / reporting hops
// v10.00 - Updated the power management code to encourage charging
// v11.00 - Updated to deviceOS 6.3.4 and changed time zone to PST
// v12.00 - Minimal Change- updated to support the Photon2 - So WiFi and Cellular - automatically selectes
// v13.00 - Fixed carrier-board user button initialization and rolled forward the Photon 2/P2 support changes
// v14.00 - Rescude build - reprovisions WiFi and sets recovery guards.
// v15.00 - Cleanup release - Wi-Fi provisioning credentials removed after rescue deployment.
// v16.00 - Field debug build with forced boot connect, shorter sleep, and extra wake/power diagnostics.


#define DEFAULT_LORA_WINDOW 5
#define STAY_CONNECTED 60
#define PUBLISH_QUEUE_FILE_SIZE 200
#define MAX_SECONDS_WITHOUT_PARTICLE_CONNECTION (6UL * 60UL * 60UL)

// Particle Libraries
#include "PublishQueuePosixRK.h"			        // https://github.com/rickkas7/PublishQueuePosixRK
#include "LocalTimeRK.h"					        // https://rickkas7.github.io/LocalTimeRK/
#include "AB1805_RK.h"                          	// Watchdog and Real Time Clock - https://github.com/rickkas7/AB1805_RK
#include "Particle.h"                               // Because it is a CPP file not INO
// Application Files
#include "config.h"
#include "LoRA_Functions.h"							// Where we store all the information on our LoRA implementation - application specific not a general library
#include "device_pinout.h"							// Define pinouts and initialize them
#include "power_management.h"
#include "Particle_Functions.h"							// Particle specific functions
#include "take_measurements.h"						// Manages interactions with the sensors (default is temp for charging)
#include "MyPersistentData.h"						// Where my persistent storage files are kept

// Support for Particle Products (changes coming in 4.x - https://docs.particle.io/cards/firmware/macros/product_id/)
PRODUCT_VERSION(16);									// For now, we are putting nodes and gateways in the same product group - need to deconflict #
char currentPointRelease[6] ="16.00";

// Prototype functions
void publishStateTransition(void);                  // Keeps track of state machine changes - for debugging
void userSwitchISR();                               // interrupt service routime for the user switch
void publishWebhook(uint8_t nodeNumber);			// Publish data based on node number
void softDelay(uint32_t t);                 		// Soft delay is safer than delay
void syncTimeFormattingToLocalTime();              // Keep Particle Time formatting aligned with LocalTimeRK
void initializeParticleConnectionGuardStartTimeIfNeeded(); // Initialize the RTC-backed connection guard start once time becomes valid
void logBootHeader();                               // Log firmware and platform data at boot for field diagnostics
void updateMinHeap();                              // Track the lowest free heap during the active cycle
void logWakeHeap(const char *context);             // Log free heap after startup or wake
void logPreSleepHeap(unsigned long sleepSeconds);  // Log heap immediately before sleep
void powerDownNetworkForSleep(const char *reason); // Non-blocking network shutdown before sleep
void logFieldDebugPowerPath(const char *context);  // Compile-time field diagnostics for battery and power path
void logFieldDebugConnectEntry();                  // Compile-time connect-entry diagnostics
void logFieldDebugWake(const SystemSleepResult &result); // Compile-time wake diagnostics
bool isSafeToRunPowerManagementMaintenance();      // Gateway decides when charger maintenance is safe
bool isSafeToRunPowerManagementRemediation();      // Gateway decides when remediation is safe
bool hasExceededParticleConnectionGuardWindow();   // Force a power-off reboot if cloud connectivity is stale for too long
bool shouldForceParticleConnectionAttempt();       // Force a Particle reconnect before escalating to ERROR_STATE
#if HAL_PLATFORM_WIFI
void configureHiddenWifi();                        // Configure hidden SSID credentials once at startup
void startWifiConnectAttempt(const char *source);  // Start a Wi-Fi connect attempt with lightweight diagnostics
void pollWifiConnectionDiagnostics();              // Emit Wi-Fi readiness and timeout diagnostics without blocking
bool hasConfiguredWifiCredentials();               // True when config.h provides plaintext credentials for updating stored settings
#endif

// System Health Variables
int outOfMemory = -1;                               // From reference code provided in AN0023 (see above)

// State Machine Variables
enum State { INITIALIZATION_STATE, ERROR_STATE, IDLE_STATE, SLEEPING_STATE, LoRA_STATE, CONNECTING_STATE, DISCONNECTING_STATE, REPORTING_STATE};
char stateNames[9][16] = {"Initialize", "Error", "Idle", "Sleeping", "LoRA", "Connecting", "Disconnecting", "Reporting"};
State state = INITIALIZATION_STATE;
State oldState = INITIALIZATION_STATE;

// Initialize Functions
SystemSleepConfiguration config;                    // Initialize new Sleep 2.0 Api
AB1805 ab1805(Wire);                                // Rickkas' RTC / Watchdog library
LocalTimeConvert conv;								// For determining if the park should be opened or closed - need local time
void outOfMemoryHandler(system_event_t event, int param);

// Program Variables
volatile bool userSwitchDectected = false;	
uint32_t heapAtWake = 0;
uint32_t minHeapThisCycle = 0;
uint32_t heapBeforeSleep = 0;
system_tick_t heapCycleStart = 0;
time_t particleConnectionGuardStartTime = 0;
system_tick_t particleConnectionGuardStartMs = 0;
uint8_t particleFailedConnectionCycles = 0;
system_tick_t errorStateEnteredMs = 0;
#if HAL_PLATFORM_WIFI
bool wifiReadyLogged = false;
bool wifiTimeoutLogged = false;
bool wifiConnectAttemptActive = false;
system_tick_t wifiConnectStarted = 0;
#endif

void setup() 
{
	particleConnectionGuardStartMs = millis();
	waitFor(Serial.isConnected, 10000);				// Wait for serial connection

    initializePinModes();                           // Sets the pinModes
	LocalTime::instance().withConfig(LocalTimePosixTimezone("EST5EDT,M3.2.0/2:00:00,M11.1.0/2:00:00"));			// East coast of the US
	ab1805.withFOUT(WAKEUP_PIN).setup();        	// Initialize RTC before loading FRAM-backed data that uses Time.now()
	if (Time.isValid()) {
		syncTimeFormattingToLocalTime();
		initializeParticleConnectionGuardStartTimeIfNeeded();
	}
	else {
		Log.warn("Time not valid at boot - enabling fallback connection guard");
	}
	logBootHeader();

	// Load the persistent storage objects
	sysStatus.setup();
	current.setup();
	nodeDatabase.setup();
	if (!initializePowerManagement()) {
		Log.error("Failed to initialize power management");
	}

	takeMeasurements();
	#if FIELD_DEBUG_BUILD
	logFieldDebugPowerPath("boot");
	#endif

    Particle_Functions::instance().setup();         // Sets up all the Particle functions and variables defined in particle_fn.h
    ab1805.setWDT(AB1805::WATCHDOG_MAX_SECONDS);	// Enable watchdog

	System.on(out_of_memory, outOfMemoryHandler);   // Enabling an out of memory handler is a good safety tip. If we run out of memory a System.reset() is done.

	PublishQueuePosix::instance().withFileQueueSize(PUBLISH_QUEUE_FILE_SIZE).setup();          // Initialize PublishQueuePosixRK
	Log.info("Publish queue configured: ram=%u file=%u", (unsigned)PublishQueuePosix::instance().getRamQueueSize(), (unsigned)PublishQueuePosix::instance().getFileQueueSize());

	LoRA_Functions::instance().setup(true);			// Start the LoRA radio (true for Gateway and false for Node)

	// Setup local time and set the publishing schedule
	conv.withCurrentTime().convert();  				        // Convert to local time for use later

	const bool startupNeedsCloudSync = !Time.isValid() || sysStatus.wasReinitializedThisBoot() || current.wasReinitializedThisBoot() || sysStatus.get_lastConnection() == 0;

	if (!startupNeedsCloudSync && Time.isValid()) {
		Log.info("LocalTime initialized, time is %s and RTC %s set", conv.format("%I:%M:%S%p").c_str(), (ab1805.isRTCSet()) ? "is" : "is not");
	}
	else {
		Log.info("Startup requires Particle connection for time or data validation");
		state = CONNECTING_STATE;
	}

	if (!digitalRead(BUTTON_PIN) || sysStatus.get_connectivityMode()== 1) {
		Log.info("User button or pre-existing set to connected mode");
		sysStatus.set_connectivityMode(1);					  // connectivityMode Code 1 keeps both LoRA and Cellular connections on
		state = CONNECTING_STATE;
	}
	#if FIELD_DEBUG_BUILD
	state = CONNECTING_STATE;
	Log.info("FIELD DEBUG: forcing CONNECTING_STATE on boot");
	#endif

	#if HAL_PLATFORM_WIFI
	configureHiddenWifi();
	if (state == CONNECTING_STATE) {
		#if FIELD_DEBUG_BUILD
		logFieldDebugPowerPath("pre-wifi-connect");
		#endif
		WiFi.on();
		startWifiConnectAttempt("setup");
	}
	else if (WiFi.isOn()) {
		WiFi.off();
	}
	#endif
	
	attachInterrupt(BUTTON_PIN,userSwitchISR,FALLING); // Active-low user button should trigger on press, not release/noise

	if (state == INITIALIZATION_STATE) state = SLEEPING_STATE;  // This is not a bad way to start - could also go to the LoRA_STATE
	logWakeHeap("startup");
	
}

void loop() {
	#if FIELD_DEBUG_BUILD
	static bool loggedFieldDebugLoop = false;
	if (!loggedFieldDebugLoop) {
		loggedFieldDebugLoop = true;
		Log.info("FIELD DEBUG: loop running");
	}
	#endif
	Particle_Functions::instance().loop();

	switch (state) {
		case IDLE_STATE: {
			if (state != oldState) publishStateTransition();                   // We will apply the back-offs before sending to ERROR state - so if we are here we will take action
			if (userSwitchDectected) {
				userSwitchDectected = false;
				Log.info("User button pressed - transitioning to CONNECTING_STATE");
				state = CONNECTING_STATE;
			}
			else if (sysStatus.get_alertCodeGateway() != 0) {
				errorStateEnteredMs = millis();
				state = ERROR_STATE;
			}
			else state = LoRA_STATE;											// Go to the LoRA state to start the next cycle									
		} break;

		case SLEEPING_STATE: {
			unsigned long wakeInSeconds, wakeBoundary;
			time_t time;

			publishStateTransition();                   					// We will apply the back-offs before sending to ERROR state - so if we are here we will take action
			wakeBoundary = (sysStatus.get_frequencyMinutes() * 60UL);
			wakeInSeconds = constrain(wakeBoundary - Time.now() % wakeBoundary, 0UL, wakeBoundary);  // If Time is valid, we can compute time to the start of the next report window	
			#if FIELD_DEBUG_BUILD
			if (wakeInSeconds > 120UL) {
				wakeInSeconds = 120UL;
				Log.info("FIELD DEBUG: sleep capped to 120 seconds");
			}
			#endif
			time = Time.now() + wakeInSeconds;
			conv.withTime(time).convert();
			Log.info("Sleep for %lu seconds until next local event at %s", wakeInSeconds, conv.format("%T").c_str());
			if (isSafeToRunPowerManagementMaintenance() && !applyPowerManagementSafetyPolicy()) {
				Log.warn("Power management safety policy could not be applied");
			}
			if (isSafeToRunPowerManagementRemediation()) {
				PowerManagementAlertCode powerAlert = runPowerManagementRemediation();
				switch (powerAlert) {
				case PowerManagementAlertCode::CHARGE_TOGGLE_DONE:
					Log.info("Power management toggled charging to recover from a charging anomaly");
					break;
				case PowerManagementAlertCode::REQUEST_POWER_CYCLE:
					Log.warn("Power management requested a power cycle after recovery failed");
					break;
				case PowerManagementAlertCode::SERVICE_REQUIRED:
					Log.warn("Power management anomaly persists after the allowed recovery attempts");
					break;
				default:
					break;
				}
			}
			if (sysStatus.get_connectivityMode() == 0) {
				powerDownNetworkForSleep("pre-sleep");
			}
			config.mode(SystemSleepMode::ULTRA_LOW_POWER)
				.gpio(BUTTON_PIN,FALLING)
				.duration(wakeInSeconds * 1000L);
			logPreSleepHeap(wakeInSeconds);
			ab1805.stopWDT();  												   // No watchdogs interrupting our slumber
			SystemSleepResult result = System.sleep(config);                   // Put the device to sleep device continues operations from here
			ab1805.resumeWDT();                                                // Wakey Wakey - WDT can resume
			logWakeHeap((result.wakeupPin() == BUTTON_PIN) ? "wake-button" : "wake-timer");
			#if FIELD_DEBUG_BUILD
			logFieldDebugWake(result);
			#endif
			#if HAL_PLATFORM_WIFI
			Log.info("Wake net: mode=%d particle=%d wifi=%d soc=%2.0f%%",
				sysStatus.get_connectivityMode(),
				Particle.connected() ? 1 : 0,
				WiFi.isOn() ? 1 : 0,
				current.get_stateOfCharge());
			#else
			Log.info("Wake net: mode=%d particle=%d soc=%2.0f%%",
				sysStatus.get_connectivityMode(),
				Particle.connected() ? 1 : 0,
				current.get_stateOfCharge());
			#endif
			resetPowerManagementRecoveryCycle();
			if (result.wakeupPin() == BUTTON_PIN) {
				softDelay(1000);
				Log.info("Woke with user button - transitioning to CONNECTING_STATE");
				userSwitchDectected = false;
				state = CONNECTING_STATE;
			}
			else {															   // Awoke for time
				conv.withCurrentTime().convert();
				Log.info("Awoke at %s %s with %li free memory", conv.format("%T").c_str(), conv.zoneName().c_str(), System.freeMemory());
				state = IDLE_STATE;
			}

		} break;

		case LoRA_STATE: {														// Enter this state every reporting period and stay here for 5 minutes
			static system_tick_t startLoRAWindow = 0;
			static byte connectionWindow = 0;

			if (state != oldState) {
				if (oldState != REPORTING_STATE) startLoRAWindow = millis();    // Mark when we enter this state - for timeouts - but multiple messages won't keep us here forever
				publishStateTransition();                   					// We will apply the back-offs before sending to ERROR state - so if we are here we will take action
				LoRA_Functions::instance().wakeLoRaRadio();
				conv.withCurrentTime().convert();								// Get the time and convert to Local
				if (conv.getLocalTimeHMS().hour >= sysStatus.get_openTime() && conv.getLocalTimeHMS().hour <= sysStatus.get_closeTime()) current.set_openHours(true);
				else current.set_openHours(false);

				if (sysStatus.get_connectivityMode() == 0) connectionWindow = DEFAULT_LORA_WINDOW;
				else connectionWindow = STAY_CONNECTED;

				Log.info("Gateway is listening for %d minutes for LoRA messages and the park is %s (%d / %d / %d)", (sysStatus.get_connectivityMode() == 0) ? DEFAULT_LORA_WINDOW : 60, (current.get_openHours()) ? "open":"closed", conv.getLocalTimeHMS().hour, sysStatus.get_openTime(), sysStatus.get_closeTime());
			} 

			if (LoRA_Functions::instance().listenForLoRAMessageGateway()) {
				#if LORA_RAW_TEST
				Log.info("LoRa raw test packet received; staying in LoRA_STATE");
				#else
				if (current.get_alertCodeNode() != 1 && current.get_openHours()) {				// We don't report Join alerts or after hours
					Log.info("LoRa DATA_RPT complete: alert=%u transitioning to REPORTING_STATE", current.get_alertCodeNode());
					state = REPORTING_STATE; 											// Received and acknowledged data from a node - need to report the alert
				}
				#endif
			}

			if ((millis() - startLoRAWindow) > (connectionWindow *60000UL)) { 					// Keeps us in listening mode for the specified windpw - then back to idle unless in test mode - keeps listening
				Log.info("Listening window over");
				LoRA_Functions::instance().nodeConnectionsHealthy();							// Will see if any nodes checked in - if not - will reset
				LoRA_Functions::instance().sleepLoRaRadio();									// Done with the LoRA phase - put the radio to sleep
				LoRA_Functions::instance().printNodeData(false);
				nodeDatabase.flush(true);
				if (Time.hour() != Time.hour(sysStatus.get_lastConnection()) && current.get_openHours()) state = CONNECTING_STATE;  	// Only Connect once an hour after the LoRA window is over and if the park is open			
				else if (sysStatus.get_alertCodeGateway() != 0) {
					errorStateEnteredMs = millis();
					state = ERROR_STATE;
				}
				else state = SLEEPING_STATE;
			}
		} break;

		case REPORTING_STATE: {
			publishStateTransition();
			publishWebhook(current.get_nodeNumber());							// Gateway or node webhook
			current.set_alertCodeNode(0);										// Zero alert code after send
			sysStatus.set_messageCount(sysStatus.get_messageCount() + 1);		// Increment the message counter 
			state = LoRA_STATE;
		} break;
		
		case CONNECTING_STATE: {
			static system_tick_t connectingTimeout = 0;

			if (state != oldState) {
				publishStateTransition();  
				if (Time.day(sysStatus.get_lastConnection()) != conv.getLocalTimeYMD().getDay()) {
					current.resetEverything();
					Log.info("New Day - Resetting everything");
				}
				#if FIELD_DEBUG_BUILD
				logFieldDebugConnectEntry();
				logFieldDebugPowerPath("connect-entry");
				#endif
				publishWebhook(sysStatus.get_nodeNumber());								// Before we connect - let's send the gateway's webhook
				#if HAL_PLATFORM_WIFI
				if (!WiFi.ready()) {
					#if FIELD_DEBUG_BUILD
					logFieldDebugPowerPath("pre-wifi-connect");
					#endif
					WiFi.on();
					startWifiConnectAttempt("CONNECTING_STATE");
				}
				#endif
				if (!Particle.connected()) Particle.connect();							// Time to connect to Particle
				connectingTimeout = millis();
			}

			if (Particle.connected()) {													// Either we will connect or we will timeout - will try for 10 minutes 
				particleConnectionGuardStartMs = millis();
				particleFailedConnectionCycles = 0;
				sysStatus.set_lastConnectionDuration((millis() - connectingTimeout) / 1000);	// Record connection time in seconds
				if (Particle.connected()) {
					Particle.syncTime();												// To prevent large connections, we will sync every hour when we connect to the Particle cloud.
					bool timeSyncCompleted = waitFor(Particle.syncTimeDone, 30000);
					if (timeSyncCompleted) {
						Log.info("Particle time sync completed");
						if (Time.isValid()) {
							sysStatus.set_lastConnection(Time.now());
							syncTimeFormattingToLocalTime();
							initializeParticleConnectionGuardStartTimeIfNeeded();
						}
					}
					else {
						Log.warn("Particle time sync timed out after 30000 ms");
					}
					if (Time.isValid()) {
						sysStatus.set_lastConnection(Time.now());
						initializeParticleConnectionGuardStartTimeIfNeeded();
					}
					getSignalStrength();
					updateMinHeap();
					Log.info("Heap post-cloud: free=%lu min=%lu", (unsigned long)System.freeMemory(), (unsigned long)minHeapThisCycle);
				}
				#if FIELD_DEBUG_BUILD
				Log.info("FIELD DEBUG: disconnecting network before LoRA listen");
				powerDownNetworkForSleep("before-lora-listen");
				Log.info("FIELD DEBUG: going directly to LoRA_STATE after cloud connection");
				state = LoRA_STATE;
				#else
				if (sysStatus.get_connectivityMode() == 1) state = LoRA_STATE;			// Go back to the LoRA State if we are in connected mode
				else state = DISCONNECTING_STATE;	 									// Typically, we will disconnect and sleep to save power - publishes occur during the 90 seconds before disconnect
				#endif
			}
			else if (millis() - connectingTimeout > 600000L) {
				Log.info("Failed to connect in 10 minutes - giving up");
				if (particleFailedConnectionCycles < UINT8_MAX) particleFailedConnectionCycles++;
				sysStatus.set_connectivityMode(0);										// Setting back to zero - must not have coverage here or here at this time
				if (hasExceededParticleConnectionGuardWindow()) {
					Log.warn("Particle guard still expired after failed connection attempt - entering ERROR_STATE");
					errorStateEnteredMs = millis();
					state = ERROR_STATE;
				}
				else {
					state = DISCONNECTING_STATE;									// Makes sure we turn off the radio
				}
			}

		} break;

		case DISCONNECTING_STATE: {														// Waits 90 seconds then disconnects
			static system_tick_t stayConnectedWindow = 0;

			if (state != oldState) {
				publishStateTransition(); 
				stayConnectedWindow = millis(); 
			}

			if ((millis() - stayConnectedWindow > 90000UL) && PublishQueuePosix::instance().getCanSleep()) {	// Stay on-line for 90 seconds and until we are done clearing the queue
				if (sysStatus.get_connectivityMode() == 0) powerDownNetworkForSleep("disconnecting complete");
				state = SLEEPING_STATE;
			}
		} break;

		case ERROR_STATE: {
			if (state != oldState) {
				errorStateEnteredMs = millis();
				publishStateTransition();
				if (Particle.connected()) Particle.publish("Alert","Deep power down in 30 seconds", PRIVATE);
				sysStatus.set_alertCodeGateway(0);			// Reset this
			}

			if (errorStateEnteredMs != 0 && (millis() - errorStateEnteredMs > 30000UL)) {
				Log.info("Deep power down device");
				softDelay(2000);
				ab1805.deepPowerDown(); 
			}
		} break;
	}

	ab1805.loop();                                  // Keeps the RTC synchronized with the Boron's clock

	PublishQueuePosix::instance().loop();           // Check to see if we need to tend to the message 
	#if HAL_PLATFORM_WIFI
	pollWifiConnectionDiagnostics();
	#endif

	sysStatus.loop();
	current.loop();
	nodeDatabase.loop();

	LoRA_Functions::instance().loop();				// Check to see if Node connections are healthy
	updateMinHeap();

	if (outOfMemory >= 0) {                         // In this function we are going to reset the system if there is an out of memory error
		Log.info("Resetting due to low memory");
		softDelay(2000);
		System.reset();
  	}

	if (shouldForceParticleConnectionAttempt()) {
		Log.warn("Particle guard expired - forcing CONNECTING_STATE before reset");
		state = CONNECTING_STATE;
	}

	if (sysStatus.get_alertCodeGateway() > 0) {
		errorStateEnteredMs = millis();
		state = ERROR_STATE;
	}
}

void syncTimeFormattingToLocalTime() {
	if (!Time.isValid()) {
		return;
	}

	const LocalTimePosixTimezone &timeZoneConfig = LocalTime::instance().getConfig();
	if (!timeZoneConfig.isValid()) {
		return;
	}

	LocalTimeConvert localConv;
	localConv.withConfig(timeZoneConfig).withCurrentTime().convert();

	Time.zone(-((float)timeZoneConfig.standardHMS.toSeconds()) / 3600.0f);
	if (timeZoneConfig.hasDST()) {
		Time.setDSTOffset(((float)(timeZoneConfig.standardHMS.toSeconds() - timeZoneConfig.dstHMS.toSeconds())) / 3600.0f);
		if (localConv.isDST()) {
			Time.beginDST();
		}
		else {
			Time.endDST();
		}
	}
	else {
		Time.setDSTOffset(0);
		Time.endDST();
	}
}

void initializeParticleConnectionGuardStartTimeIfNeeded() {
	if (Time.isValid() && particleConnectionGuardStartTime == 0) {
		particleConnectionGuardStartTime = Time.now();
		Log.info("Particle connection guard start initialized at %lu", (unsigned long)particleConnectionGuardStartTime);
	}
}

void logBootHeader() {
	const char *platformName;
	const char *transportName;

	#if PLATFORM_ID == PLATFORM_BORON
	platformName = "boron";
	#elif defined(PLATFORM_PHOTON2) && (PLATFORM_ID == PLATFORM_PHOTON2)
	platformName = "photon2";
	#elif defined(PLATFORM_P2) && (PLATFORM_ID == PLATFORM_P2)
	platformName = "p2";
	#else
	platformName = "unknown";
	#endif

	#if HAL_PLATFORM_CELLULAR
	transportName = "cellular";
	#elif HAL_PLATFORM_WIFI
	transportName = "wifi";
	#else
	transportName = "unknown";
	#endif

	Log.info("========== Boot Header ==========");
	Log.info("Firmware %s platform=%s transport=%s deviceOS=%s", currentPointRelease, platformName, transportName, System.version().c_str());
	Log.info("DeviceID=%s resetReason=%d timeValid=%s build=%s %s", System.deviceID().c_str(), (int)System.resetReason(), Time.isValid() ? "yes" : "no", __DATE__, __TIME__);
	Log.info("Build flags: FIELD_DEBUG_BUILD=%d LORA_RAW_TEST=%d UPDATE_WIFI_CREDENTIALS=%d", FIELD_DEBUG_BUILD, LORA_RAW_TEST, UPDATE_WIFI_CREDENTIALS);
}

void updateMinHeap() {
	uint32_t freeHeap = System.freeMemory();
	if (minHeapThisCycle == 0 || freeHeap < minHeapThisCycle) {
		minHeapThisCycle = freeHeap;
	}
}

void logWakeHeap(const char *context) {
	heapAtWake = System.freeMemory();
	minHeapThisCycle = heapAtWake;
	heapCycleStart = millis();
	Log.info("Heap %s: free=%lu", context, (unsigned long)heapAtWake);
}

void logPreSleepHeap(unsigned long sleepSeconds) {
	updateMinHeap();
	heapBeforeSleep = System.freeMemory();
	Log.info("Heap pre-sleep: free=%lu min=%lu awakeMs=%lu nextSleepS=%lu",
		(unsigned long)heapBeforeSleep,
		(unsigned long)minHeapThisCycle,
		(unsigned long)(millis() - heapCycleStart),
		sleepSeconds);
}

void powerDownNetworkForSleep(const char *reason) {
	Log.info("Powering down network for sleep: %s", reason);

	if (Particle.connected()) {
		Particle.disconnect();
		Particle.process();
	}

	#if HAL_PLATFORM_WIFI
	if (WiFi.isOn()) {
		WiFi.off();
	}
	#endif
}

void logFieldDebugPowerPath(const char *context) {
	#if FIELD_DEBUG_BUILD
	const double stateOfCharge = current.get_stateOfCharge();
	const unsigned batteryState = current.get_batteryState();
	const int internalTempC = current.get_internalTempC();
	#if HAL_PLATFORM_CELLULAR || PLATFORM_ID == 32 || PLATFORM_ID == 34
	const float batteryVoltage = getBatteryVoltageForDiagnostics();
	#endif
	#if HAL_PLATFORM_POWER_MANAGEMENT && HAL_PLATFORM_PMIC_BQ24195
	const bool inputPowerPresent = getExternalPowerPresentForDiagnostics();
	#endif

	#if HAL_PLATFORM_POWER_MANAGEMENT && HAL_PLATFORM_PMIC_BQ24195
	Log.info("FIELD DEBUG %s: soc=%2.0f%% vbat=%.2f batt=%u temp=%dC extpwr=%d",
		context,
		stateOfCharge,
		(double)batteryVoltage,
		batteryState,
		internalTempC,
		inputPowerPresent ? 1 : 0);
	#elif HAL_PLATFORM_CELLULAR || PLATFORM_ID == 32 || PLATFORM_ID == 34
	Log.info("FIELD DEBUG %s: soc=%2.0f%% vbat=%.2f batt=%u temp=%dC extpwr=n/a",
		context,
		stateOfCharge,
		(double)batteryVoltage,
		batteryState,
		internalTempC);
	#else
	Log.info("FIELD DEBUG %s: soc=%2.0f%% vbat=n/a batt=%u temp=%dC extpwr=n/a",
		context,
		stateOfCharge,
		batteryState,
		internalTempC);
	#endif
	#else
	(void)context;
	#endif
}

void logFieldDebugConnectEntry() {
	#if FIELD_DEBUG_BUILD
	#if HAL_PLATFORM_WIFI
	Log.info("FIELD DEBUG connect: soc=%2.0f%% batt=%u heap=%lu wifi=%d particle=%d",
		current.get_stateOfCharge(),
		(unsigned)current.get_batteryState(),
		(unsigned long)System.freeMemory(),
		WiFi.isOn() ? 1 : 0,
		Particle.connected() ? 1 : 0);
	#else
	Log.info("FIELD DEBUG connect: soc=%2.0f%% batt=%u heap=%lu particle=%d",
		current.get_stateOfCharge(),
		(unsigned)current.get_batteryState(),
		(unsigned long)System.freeMemory(),
		Particle.connected() ? 1 : 0);
	#endif
	#endif
}

void logFieldDebugWake(const SystemSleepResult &result) {
	#if FIELD_DEBUG_BUILD
	#if HAL_PLATFORM_WIFI
	Log.info("FIELD DEBUG wake: reset=%d pin=%d heap=%lu soc=%2.0f%% particle=%d wifi=%d",
		(int)System.resetReason(),
		(int)result.wakeupPin(),
		(unsigned long)System.freeMemory(),
		current.get_stateOfCharge(),
		Particle.connected() ? 1 : 0,
		WiFi.isOn() ? 1 : 0);
	#else
	Log.info("FIELD DEBUG wake: reset=%d pin=%d heap=%lu soc=%2.0f%% particle=%d",
		(int)System.resetReason(),
		(int)result.wakeupPin(),
		(unsigned long)System.freeMemory(),
		current.get_stateOfCharge(),
		Particle.connected() ? 1 : 0);
	#endif
	#else
	(void)result;
	#endif
}

/**
 * @brief Publishes a state transition to the Log Handler and to the Particle monitoring system.
 *
 * @details A good debugging tool.
 */
void publishStateTransition(void)
{
	char stateTransitionString[256];
	if (state == IDLE_STATE && !Time.isValid()) snprintf(stateTransitionString, sizeof(stateTransitionString), "From %s to %s with invalid time", stateNames[oldState],stateNames[state]);
	else snprintf(stateTransitionString, sizeof(stateTransitionString), "From %s to %s", stateNames[oldState],stateNames[state]);

	oldState = state;

	Log.info(stateTransitionString);
}

// Here are the various hardware and timer interrupt service routines
void outOfMemoryHandler(system_event_t event, int param) {
    outOfMemory = param;
}

void userSwitchISR() {
	userSwitchDectected = true;
}

bool isSafeToRunPowerManagementRemediation() {
	return !userSwitchDectected;
}

bool isSafeToRunPowerManagementMaintenance() {
	return !userSwitchDectected;
}

bool hasExceededParticleConnectionGuardWindow() {
	time_t lastConnection = sysStatus.get_lastConnection();

	// --------------------------------------------------
	// PRIMARY PATH: RTC-backed timing (preferred)
	// --------------------------------------------------
	if (Time.isValid()) {
		initializeParticleConnectionGuardStartTimeIfNeeded();

		time_t now = Time.now();
		time_t referenceTime = 0;

		if (lastConnection > 0 && now > lastConnection) {
			referenceTime = lastConnection;
		}
		else if (particleConnectionGuardStartTime > 0 && now > particleConnectionGuardStartTime) {
			referenceTime = particleConnectionGuardStartTime;
		}
		else {
			return false;
		}

		unsigned long elapsedSeconds = static_cast<unsigned long>(now - referenceTime);

		if (elapsedSeconds >= MAX_SECONDS_WITHOUT_PARTICLE_CONNECTION) {
			Log.warn("Particle guard (RTC) triggered elapsed=%lu threshold=%lu",
				elapsedSeconds,
				(unsigned long)MAX_SECONDS_WITHOUT_PARTICLE_CONNECTION);
			return true;
		}

		return false;
	}

	// --------------------------------------------------
	// FALLBACK PATH: Time NOT valid -> use cycle-based guard
	// --------------------------------------------------
	if (particleFailedConnectionCycles >= 6) {
		Log.warn("Particle guard (cycle fallback) triggered after %u failed connection cycles", particleFailedConnectionCycles);
		return true;
	}

	return false;
}

bool shouldForceParticleConnectionAttempt() {
	return state != CONNECTING_STATE &&
		state != ERROR_STATE &&
		hasExceededParticleConnectionGuardWindow();
}

#if HAL_PLATFORM_WIFI
// Hidden SSID connections are slower and less reliable, require Device OS 5.5.0+,
// and Photon 2 supports 2.4 GHz WPA2/WPA3 with WPA2 AES preferred.
void configureHiddenWifi() {
	#if UPDATE_WIFI_CREDENTIALS
	if (!WiFi.isOn()) {
		WiFi.on();
	}

	WiFi.clearCredentials();

	WiFiCredentials credentials;
	credentials.setSsid(WIFI_SSID)
		.setPassword(WIFI_PASSWORD)
		.setSecurity(WPA2)
		.setCipher(WLAN_CIPHER_AES);

	if (WIFI_HIDDEN_SSID) {
		credentials.setHidden(true);
	}

	WiFi.setCredentials(credentials);
	Log.info("WiFi credentials updated");
	#else
	Log.info("Using stored WiFi credentials");
	#endif
}

void startWifiConnectAttempt(const char *source) {
	wifiReadyLogged = false;
	wifiTimeoutLogged = false;
	wifiConnectAttemptActive = true;
	wifiConnectStarted = millis();
	WiFi.connect();
	#if UPDATE_WIFI_CREDENTIALS
		Log.info("[%10lu] WiFi.connect() called (%s) SSID='%s'", (unsigned long)wifiConnectStarted, source, WIFI_SSID);
	#else
		Log.info("[%10lu] WiFi.connect() called (%s) using stored credentials", (unsigned long)wifiConnectStarted, source);
	#endif
}

void pollWifiConnectionDiagnostics() {
	if (WiFi.ready()) {
		if (!wifiReadyLogged) {
			WiFiSignal sig = WiFi.RSSI();
			IPAddress localIp = WiFi.localIP();
			#if UPDATE_WIFI_CREDENTIALS
				Log.info("[%10lu] WiFi.ready()=true SSID='%s' RSSI=%0.1f IP=%u.%u.%u.%u",
					(unsigned long)millis(),
					WIFI_SSID,
					sig.getStrengthValue(),
					localIp[0], localIp[1], localIp[2], localIp[3]);
			#else
				Log.info("[%10lu] WiFi.ready()=true RSSI=%0.1f IP=%u.%u.%u.%u",
					(unsigned long)millis(),
					sig.getStrengthValue(),
					localIp[0], localIp[1], localIp[2], localIp[3]);
			#endif
			wifiReadyLogged = true;
			wifiConnectAttemptActive = false;
		}
	}
	else if (wifiConnectAttemptActive && !wifiTimeoutLogged && (millis() - wifiConnectStarted > 60000UL)) {
		#if UPDATE_WIFI_CREDENTIALS
			Log.info("[%10lu] WiFi connect timeout after %lu ms SSID='%s'",
				(unsigned long)millis(),
				(unsigned long)(millis() - wifiConnectStarted),
				WIFI_SSID);
		#else
			Log.info("[%10lu] WiFi connect timeout after %lu ms using stored credentials",
				(unsigned long)millis(),
				(unsigned long)(millis() - wifiConnectStarted));
		#endif
		wifiTimeoutLogged = true;
	}
}
#endif

/**
 * @brief Publish Webhook will put the webhook data into the publish queue
 * 
 * @details Nodes and Gateways will use the same format for this webook - data sources will change
 * 
 * 
 */
void publishWebhook(uint8_t nodeNumber) {
	char data[384];                             						// Store the date in this character array - not global
	// Battery conect information - https://docs.particle.io/reference/device-os/firmware/boron/#batterystate-
	const char* batteryContext[8] = {"Unknown","Not Charging","Charging","Charged","Discharging","Fault","Diconnected"};

	if (!Time.isValid()) return;										// A webhook without a valid timestamp is worthless
	unsigned long endTimePeriod = Time.now() - (Time.second() + 1);		// Moves the timestamp withing the reporting boundary - so 18:00:14 becomes 17:59:59 - helps in Ubidots reporting

	if (nodeNumber > 0) {												// Webhook for a node
		String deviceID = LoRA_Functions::instance().findDeviceID(nodeNumber, current.get_nodeID());
		if (deviceID == "null") return;									// A webhook without a deviceID is worthless

		float percentSuccess = ((current.get_successCount() * 1.0)/ current.get_messageCount())*100.0;

		snprintf(data, sizeof(data), "{\"deviceid\":\"%s\", \"hourly\":%u, \"daily\":%u, \"sensortype\":%d, \"battery\":%4.2f,\"key1\":\"%s\",\"temp\":%d, \"resets\":%d,\"alerts\": %d, \"node\": %d, \"rssi\":%d,  \"snr\":%d, \"hops\":%d, \"msg\":%d, \"success\":%4.2f, \"timestamp\":%lu000}",\
		deviceID.c_str(), current.get_hourlyCount(), current.get_dailyCount(), current.get_sensorType(), current.get_stateOfCharge(), batteryContext[current.get_batteryState()],\
		current.get_internalTempC(), current.get_resetCount(), current.get_alertCodeNode(), current.get_nodeNumber(), current.get_RSSI(), current.get_SNR(), current.get_hops(), current.get_messageCount(), percentSuccess, endTimePeriod);
		PublishQueuePosix::instance().publish("Ubidots-LoRA-Node-v1", data, PRIVATE | WITH_ACK);
	}
	else {																// Webhook for the gateway
		takeMeasurements();												// Loads the current values for the Gateway
		unsigned gatewayAlert = static_cast<unsigned>(sysStatus.get_alertCodeGateway());
		unsigned connectionTimeSeconds = static_cast<unsigned>(sysStatus.get_lastConnectionDuration());
		unsigned powerAlert = static_cast<unsigned>(getLastPowerManagementAlert());
		unsigned powerObservationCount = static_cast<unsigned>(getConsecutiveChargingExpectedNotChargingSamples());
		unsigned powerRecoveryAttempts = static_cast<unsigned>(getTotalPowerRecoveryAttempts());

		snprintf(data, sizeof(data), "{\"deviceid\":\"%s\",\"battery\":%4.2f,\"key1\":\"%s\",\"temp\":%d,\"resets\":%d,\"alerts\":%u,\"msg\":%d,\"connectiontime\":%u,\"pmalert\":%u,\"pmobs\":%u,\"pmrec\":%u,\"timestamp\":%lu000}",\
		Particle.deviceID().c_str(), current.get_stateOfCharge(), batteryContext[current.get_batteryState()],\
		current.get_internalTempC(), sysStatus.get_resetCount(), gatewayAlert, sysStatus.get_messageCount(), connectionTimeSeconds, powerAlert, powerObservationCount, powerRecoveryAttempts, endTimePeriod);
		if (PublishQueuePosix::instance().publish("Ubidots-LoRA-Gateway-v2", data, PRIVATE | WITH_ACK) && gatewayAlert != 0) {
			sysStatus.set_alertCodeGateway(0);
		}
		if (powerAlert != 0) {
			clearPowerManagementAlert();
		}
	}
	return;
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
