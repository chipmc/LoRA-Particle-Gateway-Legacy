/*
 * Project LoRA-Particle-Gateway
 * Description: This device will listen for data from client devices and forward the data to Particle via webhook
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
// v19.00 - Dual-platform Boron/Photon 2 gateway release with platform abstraction, closed-hours scheduling, P2 pin mapping, and connection diagnostics
// v21.00 - Gateway production hardening release with FRAM repair/verification, normalized logging, battery telemetry, safe local config handling, and boot reset diagnostics
// v23.00 - Release finalization with NodeDB save coalescing, ACK-path persistence reduction, compact persistence instrumentation, and RF9X modem-config compatibility build fix
// v25.00 - Power back-off for low battery conditions, signed LoRa decode hardening, and persistence log severity tuning
// v27.00 - NodeDB persistence hardening with enhanced JSON validation, mutation failure handling, ACK Protocol v1 clarification, and static validator allocation

// Particle Libraries
#include "PublishQueuePosixRK.h"			        // https://github.com/rickkas7/PublishQueuePosixRK
#include "LocalTimeRK.h"					        // https://rickkas7.github.io/LocalTimeRK/
#include "AB1805_RK.h"                          	// Watchdog and Real Time Clock - https://github.com/rickkas7/AB1805_RK
#include "Particle.h"                               // Because it is a CPP file not INO
// Application Files
#include "GatewayPlatform.h"
#include "LoRA_Functions.h"							// Where we store all the information on our LoRA implementation - application specific not a general library
#include "device_pinout.h"							// Define pinouts and initialize them
#include "Particle_Functions.h"							// Particle specific functions
#include "take_measurements.h"						// Manages interactions with the sensors (default is temp for charging)
#include "MyPersistentData.h"						// Where my persistent storage files are kept

// Support for Particle Products (changes coming in 4.x - https://docs.particle.io/cards/firmware/macros/product_id/)
PRODUCT_VERSION(GATEWAY_PRODUCT_VERSION);								// Platform-specific gateway release number
char currentPointRelease[16] = GATEWAY_RELEASE_STRING;

// Prototype functions
void publishStateTransition(void);                  // Keeps track of state machine changes - for debugging
void userSwitchISR();                               // interrupt service routime for the user switch
void publishWebhook(uint8_t nodeNumber);			// Publish data based on node number
void softDelay(uint32_t t);                 		// Soft delay is safer than delay
void serviceBackgroundTasksDuringWait(void);
bool waitForParticleSyncWithHousekeeping(uint32_t timeoutMs);
bool isParkOpenNow(void);
unsigned long nextWakeIntervalSeconds(void);
void refreshGatewayLocalTimeCache(void);
bool shouldSyncGatewayTime(time_t currentTime);
bool syncGatewayTimeIfNeeded(void);
void logGatewayBootTimeStatus(void);
String formatGatewayLocalTime(time_t timestamp);
String formatGatewayUtcTime(time_t timestamp);
String formatGatewaySyncAge(time_t currentTime, time_t lastTimeSync);

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

namespace {

struct GatewayLocalTime {
	bool valid;
	time_t utcTime;
	int hour;
	int minute;
	int second;
	String dateKey;
};

unsigned long gatewayTimeSyncIntervalSeconds() {
#if GATEWAY_DEV_TIME_SYNC_INTERVAL_SECONDS > 0UL
	return GATEWAY_DEV_TIME_SYNC_INTERVAL_SECONDS;
#else
	return GATEWAY_TIME_SYNC_INTERVAL_SECONDS;
#endif
}

uint32_t particleConnectGuardStartedMs = 0;

void startParticleConnectGuard() {
	particleConnectGuardStartedMs = millis();
	Log.info("Particle guard started ms=%lu", (unsigned long)particleConnectGuardStartedMs);
}

void clearParticleConnectGuard() {
	particleConnectGuardStartedMs = 0;
}

bool hasExceededParticleConnectGuardTimeout() {
	if (particleConnectGuardStartedMs == 0) {
		return false;
	}

	const uint32_t elapsedMs = millis() - particleConnectGuardStartedMs;
	if (elapsedMs < PARTICLE_CONNECT_GUARD_TIMEOUT_MS) {
		return false;
	}

	Log.warn("Particle guard triggered elapsed=%lus threshold=%lus",
		(unsigned long)(elapsedMs / 1000UL),
		(unsigned long)(PARTICLE_CONNECT_GUARD_TIMEOUT_MS / 1000UL));
	return true;
}

GatewayLocalTime getGatewayLocalTime(time_t utcTime) {
	GatewayLocalTime localTime = {false, 0, 0, 0, 0, String()};
	if (utcTime <= 0) {
		return localTime;
	}

	LocalTimeConvert localConv(conv);
	localConv.withTime(utcTime).convert();
	localTime.valid = true;
	localTime.utcTime = utcTime;
	localTime.hour = localConv.getLocalTimeHMS().hour;
	localTime.minute = localConv.getLocalTimeHMS().minute;
	localTime.second = localConv.getLocalTimeHMS().second;
	localTime.dateKey = localConv.format("%Y-%m-%d");
	return localTime;
}

GatewayLocalTime getGatewayCurrentLocalTime() {
	if (!Time.isValid()) {
		return GatewayLocalTime{false, 0, 0, 0, 0, String()};
	}
	return getGatewayLocalTime(Time.now());
}

String formatGatewayLocalClockTime(time_t utcTime) {
	const GatewayLocalTime localTime = getGatewayLocalTime(utcTime);
	if (!localTime.valid) {
		return "invalid";
	}
	return String::format("%02d:%02d:%02d", localTime.hour, localTime.minute, localTime.second);
}

bool isGatewayWithinOpenHours(const GatewayLocalTime &localTime) {
	if (!localTime.valid) {
		return true;
	}
	return (localTime.hour >= sysStatus.get_openTime() && localTime.hour <= sysStatus.get_closeTime());
}

bool isNewGatewayLocalDaySince(time_t utcTime) {
	const GatewayLocalTime currentLocalTime = getGatewayCurrentLocalTime();
	const GatewayLocalTime referenceLocalTime = getGatewayLocalTime(utcTime);
	if (!currentLocalTime.valid || !referenceLocalTime.valid) {
		return false;
	}
	return currentLocalTime.dateKey != referenceLocalTime.dateKey;
}

bool hasGatewayLocalHourChangedSince(time_t utcTime) {
	const GatewayLocalTime currentLocalTime = getGatewayCurrentLocalTime();
	if (!currentLocalTime.valid) {
		return false;
	}
	const GatewayLocalTime referenceLocalTime = getGatewayLocalTime(utcTime);
	if (!referenceLocalTime.valid) {
		return true;
	}
	return currentLocalTime.hour != referenceLocalTime.hour;
}

} // anonymous namespace

String formatGatewayLocalTime(time_t timestamp) {
	if (timestamp <= 0) {
		return "invalid";
	}
	LocalTimeConvert localConv(conv);
	localConv.withTime(timestamp).convert();
	return localConv.format("%Y-%m-%d %H:%M:%S");
}

String formatGatewayUtcTime(time_t timestamp) {
	if (timestamp <= 0) {
		return "invalid";
	}

	struct tm utcTm = {};
	gmtime_r(&timestamp, &utcTm);
	char buffer[24];
	strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &utcTm);
	return String(buffer);
}

String formatGatewaySyncAge(time_t currentTime, time_t lastTimeSync) {
	if (currentTime <= 0 || lastTimeSync <= 0 || lastTimeSync > currentTime) {
		return "unknown";
	}

	const unsigned long ageSeconds = (unsigned long)(currentTime - lastTimeSync);
	if (ageSeconds >= 3600UL) {
		return String::format("%luh%lum", ageSeconds / 3600UL, (ageSeconds % 3600UL) / 60UL);
	}
	if (ageSeconds >= 60UL) {
		return String::format("%lum%lus", ageSeconds / 60UL, ageSeconds % 60UL);
	}
	return String::format("%lus", ageSeconds);
}

void refreshGatewayLocalTimeCache(void) {
	if (Time.isValid()) {
		conv.withCurrentTime().convert();
	}
}

bool shouldSyncGatewayTime(time_t currentTime) {
	if (currentTime <= 0) {
		return true;
	}

	const time_t lastTimeSync = sysStatus.get_lastTimeSync();
	if (lastTimeSync <= 0 || lastTimeSync > currentTime) {
		return true;
	}

	return (unsigned long)(currentTime - lastTimeSync) >= gatewayTimeSyncIntervalSeconds();
}

void logGatewayBootTimeStatus(void) {
	const bool rtcSet = ab1805.isRTCSet();
	const time_t currentTime = Time.isValid() ? Time.now() : 0;
	const time_t lastTimeSync = sysStatus.get_lastTimeSync();
	Log.info("Time: rtc=%s utc=%s local=%s tz=%s syncAge=%s", rtcSet ? "ok" : "fail", formatGatewayUtcTime(currentTime).c_str(), formatGatewayLocalTime(currentTime).c_str(), GATEWAY_LOCAL_TIMEZONE_LABEL, formatGatewaySyncAge(currentTime, lastTimeSync).c_str());
}

bool syncGatewayTimeIfNeeded(void) {
	const time_t currentTime = Time.isValid() ? Time.now() : 0;
	if (!shouldSyncGatewayTime(currentTime)) {
		refreshGatewayLocalTimeCache();
		return false;
	}

	time_t rtcTimeBefore = 0;
	const bool rtcTimeAvailable = ab1805.isRTCSet() && ab1805.getRtcAsTime(rtcTimeBefore);
	const time_t lastTimeSync = sysStatus.get_lastTimeSync();
	const char *reason = (!Time.isValid()) ? "invalid" : ((lastTimeSync <= 0 || lastTimeSync > currentTime) ? "unknown" : "stale");
	Log.info("TimeSync: request reason=%s lastSyncAge=%s", reason, formatGatewaySyncAge(currentTime, lastTimeSync).c_str());

	Particle.syncTime();
	if (!waitForParticleSyncWithHousekeeping(PARTICLE_SYNC_TIMEOUT_MS)) {
		Log.error("TimeSync: timeout after %lu ms", PARTICLE_SYNC_TIMEOUT_MS);
		return false;
	}
	if (!Time.isValid()) {
		Log.error("TimeSync: sync completed without valid time");
		return false;
	}

	const time_t syncedTime = Time.now();
	refreshGatewayLocalTimeCache();
	sysStatus.set_lastTimeSync(syncedTime);

	if (rtcTimeAvailable) {
		long driftSeconds = (long)(syncedTime - rtcTimeBefore);
		if (driftSeconds < 0) {
			driftSeconds = -driftSeconds;
		}
		if ((unsigned long)driftSeconds > GATEWAY_RTC_DRIFT_LOG_THRESHOLD_SECONDS) {
			Log.warn("TimeSync: drift=%lds rtc=%lu cloud=%lu action=rtc_update", driftSeconds, (unsigned long)rtcTimeBefore, (unsigned long)syncedTime);
		}
	}

	if (!ab1805.setRtcFromTime(syncedTime)) {
		Log.error("TimeSync: rtc_update_failed cloud=%lu", (unsigned long)syncedTime);
		return false;
	}

	Log.info("RTC sync: utc=%s local=%s tz=%s", formatGatewayUtcTime(syncedTime).c_str(), formatGatewayLocalTime(syncedTime).c_str(), GATEWAY_LOCAL_TIMEZONE_LABEL);
	return true;
}

void setup() 
{
	Serial.begin();
	waitFor(Serial.isConnected, 30000);				// Give the USB serial monitor time to attach before boot logs
	delay(250);
	logGatewayBootHeader();

    initializePinModes();                           // Sets the pinModes
	LoRA_Functions::instance().logGatewayDio0Setup("pre-lora-setup");

	// Load the persistent storage objects
	sysStatus.setup();
	current.setup();
	nodeDatabase.setup();

	takeMeasurements();

    Particle_Functions::instance().setup();         // Sets up all the Particle functions and variables defined in particle_fn.h
                         
    ab1805.withFOUT(D8).setup();                	// Initialize AB1805 RTC   
    ab1805.setWDT(AB1805::WATCHDOG_MAX_SECONDS);	// Enable watchdog

	System.on(out_of_memory, outOfMemoryHandler);   // Enabling an out of memory handler is a good safety tip. If we run out of memory a System.reset() is done.

	PublishQueuePosix::instance().setup();          // Initialize PublishQueuePosixRK
	Log.info("PublishQueue: setup() returned - queued=%u canSleep=%s", (unsigned)PublishQueuePosix::instance().getNumEvents(), PublishQueuePosix::instance().getCanSleep() ? "yes" : "no");

	LoRA_Functions::instance().setup(true);			// Start the LoRA radio (true for Gateway and false for Node)
	LoRA_Functions::instance().attachGatewayDio0DiagnosticInterrupt();
	LoRA_Functions::instance().logGatewayDio0Setup("post-lora-setup");

	// Setup local time and set the publishing schedule
	// LocalTime::instance().withConfig(LocalTimePosixTimezone("EST5EDT,M3.2.0/2:00:00,M11.1.0/2:00:00"));			// East coast of the US
	LocalTime::instance().withConfig(LocalTimePosixTimezone(GATEWAY_LOCAL_TIMEZONE_POSIX));
	refreshGatewayLocalTimeCache();
	const int buttonState = digitalRead(BUTTON_PIN);
	const uint8_t connectivityMode = sysStatus.get_connectivityMode();
	const bool timeValid = Time.isValid();
	const bool syncDueAtBoot = shouldSyncGatewayTime(timeValid ? Time.now() : 0);

	logGatewayBootTimeStatus();

	if (!timeValid || syncDueAtBoot) {
		Log.info("TimeSync: boot action=connect reason=%s", timeValid ? "due" : "invalid");
		state = CONNECTING_STATE;
	}

	if (!buttonState || connectivityMode == 1) {
		Log.info("User button or pre-existing set to connected mode");
		sysStatus.set_connectivityMode(1);					  // connectivityMode Code 1 keeps both LoRA and Cellular connections on
		state = CONNECTING_STATE;
	}
	
	attachInterrupt(BUTTON_PIN,userSwitchISR,CHANGE); // We may need to monitor the user switch to change behaviours / modes

	if (state == INITIALIZATION_STATE) state = SLEEPING_STATE;  // This is not a bad way to start - could also go to the LoRA_STATE
	
}

void loop() {

	switch (state) {
		case IDLE_STATE: {
			if (state != oldState) publishStateTransition();                   // We will apply the back-offs before sending to ERROR state - so if we are here we will take action
			current.set_openHours(isParkOpenNow());
			if (sysStatus.get_alertCodeGateway() != 0) state = ERROR_STATE;
			else if (!current.get_openHours()) state = SLEEPING_STATE;
			else state = LoRA_STATE;											// Go to the LoRA state to start the next cycle									
		} break;

		case SLEEPING_STATE: {
			unsigned long wakeInSeconds;
			time_t nextWakeTime;

			publishStateTransition();                   					// We will apply the back-offs before sending to ERROR state - so if we are here we will take action
			current.set_openHours(isParkOpenNow());
			nodeDatabase.persistIfDirty();
			wakeInSeconds = nextWakeIntervalSeconds();
			nextWakeTime = Time.now() + wakeInSeconds;
			Log.info("Sleep for %lu seconds until next event at %s", wakeInSeconds, formatGatewayLocalClockTime(nextWakeTime).c_str());
			config.mode(SystemSleepMode::ULTRA_LOW_POWER)
				.gpio(BUTTON_PIN,CHANGE)
				.duration(wakeInSeconds * 1000L);
			ab1805.stopWDT();  												   // No watchdogs interrupting our slumber
			SystemSleepResult result = System.sleep(config);                   // Put the device to sleep device continues operations from here
			ab1805.resumeWDT();                                                // Wakey Wakey - WDT can resume
			if (result.wakeupPin() == BUTTON_PIN) {
				waitFor(Serial.isConnected, 10000);							   // Wait for serial connection
				softDelay(1000);
				Log.info("Woke with user button");
			}
			else {															   // Awoke for time
				current.set_openHours(isParkOpenNow());
				if (current.get_openHours()) {
					const GatewayBatteryBackoffState backoffState = gatewayBatteryBackoffState();
					Log.info("Wake: listening window start local=%s duration=%us level=%u", formatGatewayLocalClockTime(Time.now()).c_str(), backoffState.listenWindowSeconds, backoffState.level);
				}
				else {
					Log.info("Wake: scheduled timer local=%s", formatGatewayLocalClockTime(Time.now()).c_str());
				}
			}
			state = IDLE_STATE;

		} break;

		case LoRA_STATE: {														// Enter this state every reporting period and stay here for 5 minutes
			static system_tick_t startLoRAWindow = 0;
			static uint16_t connectionWindowSeconds = 0;
			static system_tick_t lastLoRaDiagnosticLog = 0;

			if (state != oldState) {
				if (oldState != REPORTING_STATE) startLoRAWindow = millis();    // Mark when we enter this state - for timeouts - but multiple messages won't keep us here forever
				lastLoRaDiagnosticLog = 0;
				publishStateTransition();                   					// We will apply the back-offs before sending to ERROR state - so if we are here we will take action
				conv.withCurrentTime().convert();								// Get the time and convert to Local
				current.set_openHours(isParkOpenNow());

				if (sysStatus.get_connectivityMode() == 0) connectionWindowSeconds = gatewayBatteryBackoffState().listenWindowSeconds;
				else connectionWindowSeconds = STAY_CONNECTED * 60U;

				LoRA_Functions::instance().logGatewayStateEntry();

				Log.info("Gateway is listening for %u seconds for LoRA messages and the park is %s (%d / %d / %d)", connectionWindowSeconds, (current.get_openHours()) ? "open":"closed", conv.getLocalTimeHMS().hour, sysStatus.get_openTime(), sysStatus.get_closeTime());
			} 

			if ((millis() - lastLoRaDiagnosticLog) >= 15000UL) {
				lastLoRaDiagnosticLog = millis();
				LoRA_Functions::instance().logGatewayDio0Snapshot();
			}

			if (LoRA_Functions::instance().listenForLoRAMessageGateway()) {
				if (current.get_alertCodeNode() != 1 && current.get_openHours()) {				// We don't report Join alerts or after hours
					state = REPORTING_STATE; 													// Received and acknowledged data from a node - need to report the alert
				}
			}

				if ((millis() - startLoRAWindow) > ((unsigned long)connectionWindowSeconds * 1000UL)) { 					// Keeps us in listening mode for the specified window - then back to idle unless in test mode - keeps listening
				Log.info("Listening window over");
				LoRA_Functions::instance().nodeConnectionsHealthy();							// Will see if any nodes checked in - if not - will reset
				LoRA_Functions::instance().sleepLoRaRadio();									// Done with the LoRA phase - put the radio to sleep
				LoRA_Functions::instance().printNodeData(false);
				nodeDatabase.persistIfDirty();
					if (hasGatewayLocalHourChangedSince(sysStatus.get_lastConnection()) && current.get_openHours()) state = CONNECTING_STATE;  	// Only Connect once an hour after the LoRA window is over and if the park is open			
				else if (sysStatus.get_alertCodeGateway() != 0) state = ERROR_STATE;
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
#if HAL_PLATFORM_WIFI
			static system_tick_t connectStartedMs = 0;
			static system_tick_t wifiConnectedMs = 0;
			static system_tick_t cloudConnectedMs = 0;
#endif

			if (state != oldState) {
				publishStateTransition();  
				if (sysStatus.get_lastConnection() > 0 && isNewGatewayLocalDaySince(sysStatus.get_lastConnection())) {
					current.resetEverything();
					Log.info("New Day - Resetting everything");
				}
				publishWebhook(sysStatus.get_nodeNumber());								// Before we connect - let's send the gateway's webhook
				if (!isCloudConnected()) startNetworkConnect();						// Time to connect to the active network transport
				connectingTimeout = millis();
				startParticleConnectGuard();
#if HAL_PLATFORM_WIFI
				connectStartedMs = connectingTimeout;
				wifiConnectedMs = 0;
				cloudConnectedMs = 0;
#endif
			}

			if (hasExceededParticleConnectGuardTimeout()) {
				clearParticleConnectGuard();
				sysStatus.set_alertCodeGateway(1);
				sysStatus.set_alertTimestampGateway(Time.now());
				state = ERROR_STATE;
				break;
			}

#if HAL_PLATFORM_WIFI
			if (wifiConnectedMs == 0 && isNetworkReady()) {
				wifiConnectedMs = millis();
				SYSTEM_VERBOSE_LOG("Connect metrics detail: wifi_ready after %lu ms", (unsigned long)(wifiConnectedMs - connectStartedMs));
			}
#endif

			if (isCloudConnected()) {											// Either we will connect or we will timeout - will try for 10 minutes 
				Log.info("PublishQueue: cloud connected - queued=%u canSleep=%s", (unsigned)PublishQueuePosix::instance().getNumEvents(), PublishQueuePosix::instance().getCanSleep() ? "yes" : "no");
				#if HAL_PLATFORM_WIFI
				if (cloudConnectedMs == 0) {
					cloudConnectedMs = millis();
				}
				#endif
				syncGatewayTimeIfNeeded();
				if (Time.isValid()) {
					refreshGatewayLocalTimeCache();
					sysStatus.set_lastConnection(Time.now());
				}
				sysStatus.set_lastConnectionDuration((millis() - connectingTimeout) / 1000);	// Record connection time in seconds
				if (isCloudConnected()) {
					#if HAL_PLATFORM_WIFI
					int strength = 0;
					int quality = 0;
					getGatewaySignalMetrics(strength, quality);
					const unsigned long wifiSeconds = (wifiConnectedMs > connectStartedMs) ? ((wifiConnectedMs - connectStartedMs) / 1000UL) : 0UL;
					const unsigned long cloudSeconds = (cloudConnectedMs > wifiConnectedMs && wifiConnectedMs != 0) ? ((cloudConnectedMs - wifiConnectedMs) / 1000UL) : 0UL;
					const unsigned long totalSeconds = (cloudConnectedMs > connectStartedMs) ? ((cloudConnectedMs - connectStartedMs) / 1000UL) : 0UL;
					Log.info("Connect metrics: wifi=%lus cloud=%lus total=%lus rssi=%d quality=%d", wifiSeconds, cloudSeconds, totalSeconds, strength, quality);
					SYSTEM_VERBOSE_LOG("Connect metrics detail: start=%lu wifi=%lu cloud=%lu", (unsigned long)connectStartedMs, (unsigned long)wifiConnectedMs, (unsigned long)cloudConnectedMs);
					#else
					logSignalStrength();
					#endif
				}
				clearParticleConnectGuard();
				if (sysStatus.get_connectivityMode() == 1) state = LoRA_STATE;			// Go back to the LoRA State if we are in connected mode
				else state = DISCONNECTING_STATE;	 									// Typically, we will disconnect and sleep to save power - publishes occur during the 90 seconds before disconnect
			}
			else if (millis() - connectingTimeout > 600000L) {
				#if HAL_PLATFORM_WIFI
				const unsigned long totalSeconds = (millis() - connectStartedMs) / 1000UL;
				if (wifiConnectedMs == 0) {
					Log.info("Connect metrics: wifi_failed after %lus", totalSeconds);
				}
				else {
					const unsigned long wifiSeconds = (wifiConnectedMs - connectStartedMs) / 1000UL;
					Log.info("Connect metrics: cloud_failed wifi=%lus total=%lus", wifiSeconds, totalSeconds);
				}
				#endif
				Log.info("Failed to connect in 10 minutes - giving up");
				sysStatus.set_connectivityMode(0);										// Setting back to zero - must not have coverage here or here at this time
				clearParticleConnectGuard();
				state = DISCONNECTING_STATE;											// Makes sure we turn off the radio
			}

		} break;

		case DISCONNECTING_STATE: {														// Waits 90 seconds then disconnects
			static system_tick_t stayConnectedWindow = 0;
			static system_tick_t disconnectHardTimeout = 0;
			static system_tick_t lastQueueLog = 0;

			if (state != oldState) {
				publishStateTransition(); 
				stayConnectedWindow = millis(); 
				disconnectHardTimeout = millis();
				lastQueueLog = 0;
				Log.info("PublishQueue: DISCONNECTING_STATE entry - queued=%u canSleep=%s", (unsigned)PublishQueuePosix::instance().getNumEvents(), PublishQueuePosix::instance().getCanSleep() ? "yes" : "no");
			}

			// Periodic queue state logging while waiting (every 15 seconds, only if queued)
			if (lastQueueLog == 0 || (millis() - lastQueueLog) > 15000UL) {
				lastQueueLog = millis();
				const size_t queuedEvents = PublishQueuePosix::instance().getNumEvents();
				if (queuedEvents > 0) {
					Log.info("PublishQueue: DISCONNECTING wait - queued=%u canSleep=%s elapsed=%lu ms", (unsigned)queuedEvents, PublishQueuePosix::instance().getCanSleep() ? "yes" : "no", (unsigned long)(millis() - stayConnectedWindow));
				}
			}

			if ((millis() - stayConnectedWindow > 90000UL) && PublishQueuePosix::instance().getCanSleep()) {	// Stay on-line for 90 seconds and until we are done clearing the queue
				if (sysStatus.get_connectivityMode() == 0) Particle_Functions::instance().disconnectFromParticle();
				state = SLEEPING_STATE;
			}
			else if ((millis() - disconnectHardTimeout) > DISCONNECTING_HARD_TIMEOUT_MS) {
				const size_t queuedEvents = PublishQueuePosix::instance().getNumEvents();
				Log.error("Disconnect timeout after %lu ms waiting on publish queue (canSleep=%s queued=%u) - forcing cloud shutdown", DISCONNECTING_HARD_TIMEOUT_MS, PublishQueuePosix::instance().getCanSleep() ? "yes" : "no", (unsigned) queuedEvents);
				if (Particle_Functions::instance().disconnectFromParticle()) {
					state = SLEEPING_STATE;
				}
				else {
					Log.error("Forced disconnect failed after disconnect timeout - entering error recovery");
					sysStatus.set_alertCodeGateway(1);
					sysStatus.set_alertTimestampGateway(Time.now());
					state = ERROR_STATE;
				}
			}
		} break;

		case ERROR_STATE: {
			static system_tick_t resetTimeout = 0;

			if (state != oldState) {
				publishStateTransition();
				resetTimeout = millis();
				if (Particle.connected()) Particle.publish("Alert","Deep power down in 30 seconds", PRIVATE);
				sysStatus.set_alertCodeGateway(0);			// Reset this
			}

			if (millis() - resetTimeout > 30000L) {
				Log.info("Deep power down device");
				nodeDatabase.persistIfDirty();
				softDelay(2000);
				ab1805.deepPowerDown(); 
			}
		} break;
	}

	ab1805.loop();                                  // Keeps the RTC synchronized with the Boron's clock

	PublishQueuePosix::instance().loop();           // Check to see if we need to tend to the message 

	sysStatus.loop();
	current.loop();
	nodeDatabase.loop();

	LoRA_Functions::instance().loop();				// Check to see if Node connections are healthy

	if (outOfMemory >= 0) {                         // In this function we are going to reset the system if there is an out of memory error
		Log.info("Resetting due to low memory");
		nodeDatabase.persistIfDirty();
		softDelay(2000);
		System.reset();
  	}

	if (sysStatus.get_alertCodeGateway() > 0) state = ERROR_STATE;
}

/**
 * @brief Publishes a state transition to the Log Handler and to the Particle monitoring system.
 *
 * @details A good debugging tool.
 */
void publishStateTransition(void)
{
	const State previousState = oldState;

	oldState = state;

	if (previousState == CONNECTING_STATE && state != CONNECTING_STATE) {
		clearParticleConnectGuard();
	}

	if (state == IDLE_STATE && !Time.isValid()) {
		Log.info("From %s to %s with invalid time", stateNames[previousState], stateNames[state]);
	}
	else {
		Log.info("From %s to %s", stateNames[previousState], stateNames[state]);
	}
}

// Here are the various hardware and timer interrupt service routines
void outOfMemoryHandler(system_event_t event, int param) {
    outOfMemory = param;
}

void userSwitchISR() {
	userSwitchDectected = true;
}

/**
 * @brief Publish Webhook will put the webhook data into the publish queue
 * 
 * @details Nodes and Gateways will use the same format for this webook - data sources will change
 * 
 * 
 */

void publishWebhook(uint8_t nodeNumber) {
	char data[256];                             						// Store the date in this character array - not global

	if (!Time.isValid()) return;										// A webhook without a valid timestamp is worthless
	unsigned long endTimePeriod = Time.now() - (Time.second() + 1);		// Moves the timestamp withing the reporting boundary - so 18:00:14 becomes 17:59:59 - helps in Ubidots reporting

	if (nodeNumber > 0) {												// Webhook for a node
		String deviceID = LoRA_Functions::instance().findDeviceID(nodeNumber, current.get_nodeID());
		if (deviceID == "null") return;									// A webhook without a deviceID is worthless

		float percentSuccess = ((current.get_successCount() * 1.0)/ current.get_messageCount())*100.0;
		const int nodeRssi = (int)current.get_RSSI();
		const int nodeSnr = (int)current.get_SNR();

		snprintf(data, sizeof(data),
			"{\"deviceid\":\"%s\", \"hourly\":%u, \"daily\":%u, \"sensortype\":%d, "
			"\"battery\":%4.2f,\"key1\":\"%s\",\"temp\":%d, \"resets\":%d,"
			"\"alerts\": %d, \"node\": %d, \"rssi\":%d,  \"snr\":%d, \"hops\":%d, "
			"\"msg\":%d, \"success\":%4.2f, \"timestamp\":%lu000}",
			deviceID.c_str(), current.get_hourlyCount(), current.get_dailyCount(), current.get_sensorType(),
			current.get_stateOfCharge(), gatewayBatteryContext(current.get_batteryState()),
			current.get_internalTempC(), current.get_resetCount(), current.get_alertCodeNode(), current.get_nodeNumber(),
			nodeRssi, nodeSnr, current.get_hops(), current.get_messageCount(), percentSuccess,
			endTimePeriod);
		bool publishResult = PublishQueuePosix::instance().publish("Ubidots-LoRA-Node-v1", data, PRIVATE | WITH_ACK);
		Log.info("PublishQueue: node=%u publish=%s queued=%u canSleep=%s", nodeNumber, publishResult ? "ok" : "FAIL", (unsigned)PublishQueuePosix::instance().getNumEvents(), PublishQueuePosix::instance().getCanSleep() ? "yes" : "no");
	}
	else {																// Webhook for the gateway
		takeMeasurements();												// Loads the current values for the Gateway
		const GatewayBatteryTelemetry telemetry = GatewayPlatform::lastBatteryTelemetry();
		Log.info("Gateway battery: %.0f%% %s VBAT=%.2f source=%s", telemetry.available ? telemetry.soc : 0.0f, telemetry.contextLabel, telemetry.available ? telemetry.voltage : 0.0f, telemetry.sourceLabel);

		snprintf(data, sizeof(data),
			"{\"deviceid\":\"%s\", \"battery\":%4.2f,\"key1\":\"%s\",\"temp\":%d, "
			"\"resets\":%d, \"alerts\": %d, \"msg\":%d, \"connecttime\":%u, \"timestamp\":%lu000}",
			Particle.deviceID().c_str(), current.get_stateOfCharge(), gatewayBatteryContext(current.get_batteryState()),
			current.get_internalTempC(), sysStatus.get_resetCount(), sysStatus.get_alertCodeGateway(),
			sysStatus.get_messageCount(), sysStatus.get_lastConnectionDuration(), endTimePeriod);
		bool publishResult = PublishQueuePosix::instance().publish("Ubidots-LoRA-Gateway-v2", data, PRIVATE | WITH_ACK);
		Log.info("PublishQueue: gateway publish=%s queued=%u canSleep=%s", publishResult ? "ok" : "FAIL", (unsigned)PublishQueuePosix::instance().getNumEvents(), PublishQueuePosix::instance().getCanSleep() ? "yes" : "no");
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

bool isParkOpenNow(void) {
	const GatewayLocalTime localTime = getGatewayCurrentLocalTime();
	if (!localTime.valid) {
		return true;
	}
	return isGatewayWithinOpenHours(localTime);
}

unsigned long nextWakeIntervalSeconds(void) {
	if (!Time.isValid()) {
		return max(60UL, (unsigned long)sysStatus.get_frequencyMinutes() * 60UL);
	}
	const GatewayLocalTime localTime = getGatewayCurrentLocalTime();
	if (!localTime.valid) {
		return max(60UL, (unsigned long)sysStatus.get_frequencyMinutes() * 60UL);
	}
	const unsigned long wakeBoundary = (sysStatus.get_frequencyMinutes() * 60UL);
	const unsigned long secondsSinceLocalMidnight = ((unsigned long)localTime.hour * 3600UL) + ((unsigned long)localTime.minute * 60UL) + (unsigned long)localTime.second;
	const unsigned long nextBoundary = wakeBoundary - (secondsSinceLocalMidnight % wakeBoundary);
	const unsigned long nextBoundarySecondsSinceMidnight = (secondsSinceLocalMidnight + nextBoundary) % (24UL * 60UL * 60UL);
	const int nextWakeHour = (int)(nextBoundarySecondsSinceMidnight / 3600UL);
	const bool nextBoundaryIsOpen = (nextWakeHour >= sysStatus.get_openTime() && nextWakeHour <= sysStatus.get_closeTime());

	if (current.get_openHours() && nextBoundaryIsOpen) {
		return nextBoundary;
	}

	int hoursUntilOpen = (int)sysStatus.get_openTime() - localTime.hour;
	if (hoursUntilOpen <= 0) {
		hoursUntilOpen += 24;
	}

	const unsigned long secondsUntilOpen = (unsigned long)hoursUntilOpen * 3600UL
		- ((unsigned long)localTime.minute * 60UL)
		- (unsigned long)localTime.second;

	return max(1UL, secondsUntilOpen);
}

void serviceBackgroundTasksDuringWait(void) {
	Particle.process();
	ab1805.loop();
	PublishQueuePosix::instance().loop();
	sysStatus.loop();
	current.loop();
	nodeDatabase.loop();
	LoRA_Functions::instance().loop();
}

bool waitForParticleSyncWithHousekeeping(uint32_t timeoutMs) {
	const system_tick_t start = millis();
	while (!Particle.syncTimeDone()) {
		serviceBackgroundTasksDuringWait();
		if ((millis() - start) >= timeoutMs) {
			return false;
		}
	}
	return true;
}
