#include "LoRA_Functions.h"
#include <RHMesh.h>
#include <RH_RF95.h>						        // https://docs.particle.io/reference/device-os/libraries/r/RH_RF95/
#include "device_pinout.h"
#include "MyPersistentData.h"
#include "JsonParserGeneratorRK.h"
#include "LocalTimeRK.h"
#include "Particle_Functions.h"
#include "GatewayPlatform.h"
#include <fcntl.h>
#include <unistd.h>

extern LocalTimeConvert conv;

// Singleton instantiation - from template
LoRA_Functions *LoRA_Functions::_instance;

// [static]
LoRA_Functions &LoRA_Functions::instance() {
    if (!_instance) {
        _instance = new LoRA_Functions();
    }
    return *_instance;
}

LoRA_Functions::LoRA_Functions() {
}

LoRA_Functions::~LoRA_Functions() {
}

// ************************************************************************
// ******** JSON Object - Scoped to LoRA_Functions Class        ***********
// ************************************************************************
// JSON for node data
JsonParserStatic<1024, 50> jp;						// Make this global - reduce possibility of fragmentation


// ************************************************************************
// *****                      LoRA Setup                              *****
// ************************************************************************
// In this implementation - we have one gateway with a fixed node  number of 0 and up to 10 nodes with node numbers 1-10 ...
// In an unconfigured node, there will be a node number of greater than 10 triggering a join request
// Configured nodes will be stored in a JSON object by the gateway with three fields: node number, Particle deviceID and Time stamp of last contact
//
const uint8_t GATEWAY_ADDRESS = 0;
// const double RF95_FREQ = 915.0;				 	// Frequency - ISM
const double RF95_FREQ = 926.84;				// Center frequency for the omni-directional antenna I am using

// Define the message flags
typedef enum { NULL_STATE, JOIN_REQ, JOIN_ACK, DATA_RPT, DATA_ACK, ALERT_RPT, ALERT_ACK} LoRA_State;
char loraStateNames[7][16] = {"Null", "Join Req", "Join Ack", "Data Report", "Data Ack", "Alert Rpt", "Alert Ack"};
static LoRA_State lora_state = NULL_STATE;

volatile uint32_t loraDio0InterruptCount = 0;

const uint8_t GATEWAY_TX_POWER_DBM = 23;
const uint16_t GATEWAY_MANAGER_TIMEOUT_MS = 2000;
// Exact Bw125Cr45Sf2048 register triplet from the local RH_RF95 modem table.
const RH_RF95::ModemConfig GATEWAY_MODEM_CONFIG = {0x72, 0xb4, 0x04};
const char *const GATEWAY_MODEM_CONFIG_NAME = "Bw125Cr45Sf2048";
const char *const RADIOHEAD_GIT_DIFF_STATUS = "build-source git diff clean for RadioHead/RH_RF95/RHMesh/RHDatagram";

void loraDio0DiagnosticISR();

namespace {

const uint32_t PRE_ACK_PERSIST_INFO_MS = 250;
const uint32_t PRE_ACK_PERSIST_WARN_MS = 450;
const uint32_t PRE_ACK_PERSIST_CRITICAL_MS = 1000;

const char *const NODE_DB_BAD_PATH = "/usr/nodedb.bad";
const char *const NODE_DB_BAD_TMP_PATH = "/usr/nodedb.bad.tmp";

struct PersistSnapshot {
	uint32_t saveCount;
	uint32_t totalMs;
	uint16_t lastMs;
	uint16_t lastMirrorMs;
};

struct GatewayScheduleHint {
	uint16_t frequencyMinutes;
	bool openHours;
};

struct NodeFrequencyState {
	uint16_t desiredReportFrequencyMinutes;
	uint16_t nodeAcknowledgedFrequencyMinutes;
};

constexpr uint16_t decodeUnsigned16FromBytes(uint8_t msb, uint8_t lsb) {
	return (uint16_t)(((uint16_t)msb << 8) | (uint16_t)lsb);
}

constexpr int16_t decodeSigned16FromBytes(uint8_t msb, uint8_t lsb) {
	return (int16_t)decodeUnsigned16FromBytes(msb, lsb);
}

uint16_t decodeUnsigned16At(const uint8_t *payload, uint8_t msbIndex, uint8_t lsbIndex) {
	return decodeUnsigned16FromBytes(payload[msbIndex], payload[lsbIndex]);
}

int16_t decodeSigned16At(const uint8_t *payload, uint8_t msbIndex, uint8_t lsbIndex) {
	return decodeSigned16FromBytes(payload[msbIndex], payload[lsbIndex]);
}

static_assert(decodeSigned16FromBytes(0xFF, 0xC7) == -57, "decodeSigned16FromBytes failed for -57");
static_assert(decodeSigned16FromBytes(0xFF, 0xFF) == -1, "decodeSigned16FromBytes failed for -1");
static_assert(decodeSigned16FromBytes(0x00, 0x00) == 0, "decodeSigned16FromBytes failed for 0");
static_assert(decodeSigned16FromBytes(0x00, 0x0B) == 11, "decodeSigned16FromBytes failed for +11");

const uint16_t GATEWAY_NORMAL_REPORT_FREQUENCY_MINUTES = 60;
const uint16_t GATEWAY_LEVEL_1_REPORT_FREQUENCY_MINUTES = 120;
const uint16_t GATEWAY_LEVEL_2_REPORT_FREQUENCY_MINUTES = 240;
const uint16_t GATEWAY_LEVEL_3_REPORT_FREQUENCY_MINUTES = 480;
const uint16_t GATEWAY_NORMAL_LISTEN_WINDOW_SECONDS = 5U * 60U;
const uint16_t GATEWAY_LEVEL_1_LISTEN_WINDOW_SECONDS = 2U * 60U;
const uint16_t GATEWAY_LEVEL_2_LISTEN_WINDOW_SECONDS = 60U;
const uint16_t GATEWAY_LEVEL_3_LISTEN_WINDOW_SECONDS = 30U;

static uint8_t batteryBackoffLevel = 0;
static float batteryBackoffLastSoc = 100.0f;

uint16_t batteryBackoffFrequencyForLevel(uint8_t level) {
	switch (level) {
		case 1:
			return GATEWAY_LEVEL_1_REPORT_FREQUENCY_MINUTES;
		case 2:
			return GATEWAY_LEVEL_2_REPORT_FREQUENCY_MINUTES;
		case 3:
			return GATEWAY_LEVEL_3_REPORT_FREQUENCY_MINUTES;
		default:
			return GATEWAY_NORMAL_REPORT_FREQUENCY_MINUTES;
	}
}

uint16_t batteryBackoffListenWindowForLevel(uint8_t level) {
	switch (level) {
		case 1:
			return GATEWAY_LEVEL_1_LISTEN_WINDOW_SECONDS;
		case 2:
			return GATEWAY_LEVEL_2_LISTEN_WINDOW_SECONDS;
		case 3:
			return GATEWAY_LEVEL_3_LISTEN_WINDOW_SECONDS;
		default:
			return GATEWAY_NORMAL_LISTEN_WINDOW_SECONDS;
	}
}

uint8_t batteryBackoffLevelForSoc(float soc, uint8_t currentLevel) {
	switch (currentLevel) {
		case 0:
			if (soc < 30.0f) return 1;
			break;
		case 1:
			if (soc < 20.0f) return 2;
			if (soc > 35.0f) return 0;
			break;
		case 2:
			if (soc < 10.0f) return 3;
			if (soc > 25.0f) return 1;
			break;
		case 3:
			if (soc > 15.0f) return 2;
			break;
		default:
			return 0;
	}
	return currentLevel;
}

NodeFrequencyState readNodeFrequencyState(const JsonParserGeneratorRK::jsmntok_t *nodeObjectContainer) {
	NodeFrequencyState state = {GATEWAY_NORMAL_REPORT_FREQUENCY_MINUTES, GATEWAY_NORMAL_REPORT_FREQUENCY_MINUTES};
	if (!nodeObjectContainer) {
		return state;
	}
	int desiredReportFrequencyMinutes = (int)state.desiredReportFrequencyMinutes;
	int nodeAcknowledgedFrequencyMinutes = (int)state.nodeAcknowledgedFrequencyMinutes;
	jp.getValueByKey(nodeObjectContainer, "desiredReportFrequency", desiredReportFrequencyMinutes);
	jp.getValueByKey(nodeObjectContainer, "nodeAcknowledgedFrequency", nodeAcknowledgedFrequencyMinutes);
	state.desiredReportFrequencyMinutes = (uint16_t)max(0, desiredReportFrequencyMinutes);
	state.nodeAcknowledgedFrequencyMinutes = (uint16_t)max(0, nodeAcknowledgedFrequencyMinutes);
	if (state.desiredReportFrequencyMinutes == 0) {
		state.desiredReportFrequencyMinutes = GATEWAY_NORMAL_REPORT_FREQUENCY_MINUTES;
	}
	if (state.nodeAcknowledgedFrequencyMinutes == 0) {
		state.nodeAcknowledgedFrequencyMinutes = GATEWAY_NORMAL_REPORT_FREQUENCY_MINUTES;
	}
	return state;
}

bool writeNodeFrequencyState(const JsonParserGeneratorRK::jsmntok_t *nodeObjectContainer, uint16_t desiredReportFrequencyMinutes, uint16_t nodeAcknowledgedFrequencyMinutes, bool persistNow) {
	if (!nodeObjectContainer) {
		return false;
	}
	JsonModifier mod(jp);
	mod.insertOrUpdateKeyValue(nodeObjectContainer, "desiredReportFrequency", desiredReportFrequencyMinutes);
	mod.insertOrUpdateKeyValue(nodeObjectContainer, "nodeAcknowledgedFrequency", nodeAcknowledgedFrequencyMinutes);
	if (persistNow) {
		nodeDatabase.saveNodeIDJson(jp.getBuffer());
	}
	return true;
}

bool allKnownNodesAcknowledgedFrequency(uint16_t reportFrequencyMinutes) {
	if (reportFrequencyMinutes <= sysStatus.get_frequencyMinutes()) {
		return true;
	}

	const JsonParserGeneratorRK::jsmntok_t *nodesArrayContainer;
	if (!jp.getValueTokenByKey(jp.getOuterObject(), "nodes", nodesArrayContainer) || !nodesArrayContainer) {
		return true;
	}

	for (int index = 0; index < nodesArrayContainer->size; index++) {
		const JsonParserGeneratorRK::jsmntok_t *nodeObjectContainer = jp.getTokenByIndex(nodesArrayContainer, index);
		if (!nodeObjectContainer) {
			break;
		}
		const NodeFrequencyState nodeFrequencyState = readNodeFrequencyState(nodeObjectContainer);
		if (nodeFrequencyState.nodeAcknowledgedFrequencyMinutes != reportFrequencyMinutes) {
			return false;
		}
	}

	return true;
}

void syncGatewayFrequencyWithBatteryBackoff(const GatewayBatteryBackoffState &backoffState) {
	const uint16_t currentFrequencyMinutes = sysStatus.get_frequencyMinutes();
	if (backoffState.reportFrequencyMinutes < currentFrequencyMinutes) {
		if (currentFrequencyMinutes != backoffState.reportFrequencyMinutes) {
			Log.info("FrequencyChange: gateway old=%u new=%u reason=BATTERY_BACKOFF", currentFrequencyMinutes, backoffState.reportFrequencyMinutes);
			sysStatus.set_frequencyMinutes(backoffState.reportFrequencyMinutes);
		}
		if (sysStatus.get_updatedFrequencyMinutes() == backoffState.reportFrequencyMinutes) {
			sysStatus.set_updatedFrequencyMinutes(0);
		}
		return;
	}

	if (backoffState.reportFrequencyMinutes > currentFrequencyMinutes && allKnownNodesAcknowledgedFrequency(backoffState.reportFrequencyMinutes)) {
		Log.info("FrequencyChange: gateway old=%u new=%u reason=BATTERY_BACKOFF", currentFrequencyMinutes, backoffState.reportFrequencyMinutes);
		sysStatus.set_frequencyMinutes(backoffState.reportFrequencyMinutes);
		if (sysStatus.get_updatedFrequencyMinutes() == backoffState.reportFrequencyMinutes) {
			sysStatus.set_updatedFrequencyMinutes(0);
		}
	}
	else if (backoffState.reportFrequencyMinutes == currentFrequencyMinutes && sysStatus.get_updatedFrequencyMinutes() == backoffState.reportFrequencyMinutes) {
		sysStatus.set_updatedFrequencyMinutes(0);
	}
}

GatewayBatteryBackoffState computeGatewayBatteryBackoffState() {
	const GatewayBatteryTelemetry telemetry = GatewayPlatform::lastBatteryTelemetry();
	if (telemetry.available) {
		batteryBackoffLastSoc = telemetry.soc;
		batteryBackoffLevel = batteryBackoffLevelForSoc(telemetry.soc, batteryBackoffLevel);
	}

	GatewayBatteryBackoffState state = {
		batteryBackoffLevel,
		batteryBackoffLastSoc,
		batteryBackoffFrequencyForLevel(batteryBackoffLevel),
		batteryBackoffListenWindowForLevel(batteryBackoffLevel)
	};
	syncGatewayFrequencyWithBatteryBackoff(state);
	return state;
}

uint16_t gatewayDesiredReportFrequencyMinutes() {
	const uint16_t pendingFrequencyMinutes = sysStatus.get_updatedFrequencyMinutes();
	if (pendingFrequencyMinutes > 0) {
		return pendingFrequencyMinutes;
	}
	return computeGatewayBatteryBackoffState().reportFrequencyMinutes;
}

bool preserveCorruptNodeDb(const char *json, size_t len) {
	if (!json) {
		return false;
	}

	int fd = open(NODE_DB_BAD_TMP_PATH, O_RDWR | O_CREAT | O_TRUNC, 0644);
	if (fd < 0) {
		return false;
	}

	bool ok = true;
	if (len > 0) {
		ok = (write(fd, json, len) == (ssize_t)len);
	}
	close(fd);

	if (!ok) {
		unlink(NODE_DB_BAD_TMP_PATH);
		return false;
	}

	unlink(NODE_DB_BAD_PATH);
	if (rename(NODE_DB_BAD_TMP_PATH, NODE_DB_BAD_PATH) != 0) {
		unlink(NODE_DB_BAD_TMP_PATH);
		return false;
	}

	return true;
}

const char *nodeDbParseReason(size_t len, bool added) {
	if (len == 0) {
		return "empty";
	}
	if (len >= sizeof(nodeIDData::NodeData::nodeIDJson)) {
		return "missing-terminator-or-truncated";
	}
	if (!added) {
		return "parser-buffer-full";
	}
	return "invalid-json";
}

bool parseNodeDatabase(const String &json, size_t len, bool logFailure) {
	jp.clear();
	const bool added = jp.addString(json.c_str());
	const bool parsed = added && jp.parse();
	if (!parsed && logFailure) {
		Log.error("NodeDB parse failed: len=%u reason=%s", (unsigned)len, nodeDbParseReason(len, added));
	}
	return parsed;
}

unsigned int nodeDatabaseCount() {
	const JsonParserGeneratorRK::jsmntok_t *nodesArrayContainer = nullptr;
	if (!jp.getValueTokenByKey(jp.getOuterObject(), "nodes", nodesArrayContainer) || !nodesArrayContainer) {
		return 0;
	}
	return (unsigned int)nodesArrayContainer->size;
}

bool shouldSendClosedHoursHint() {
	conv.withCurrentTime().convert();
	const int currentHour = conv.getLocalTimeHMS().hour;
	return (currentHour >= sysStatus.get_closeTime() || currentHour < sysStatus.get_openTime());
}

uint16_t minutesUntilNextOpening() {
	conv.withCurrentTime().convert();
	const auto localTime = conv.getLocalTimeHMS();
	int hoursUntilOpen = (int)sysStatus.get_openTime() - localTime.hour;
	if (hoursUntilOpen < 0) {
		hoursUntilOpen += 24;
	}
	const unsigned long secondsUntilOpen = (unsigned long)hoursUntilOpen * 3600UL
		- ((unsigned long)localTime.minute * 60UL)
		- (unsigned long)localTime.second;
	return (uint16_t)max(1UL, (secondsUntilOpen + 59UL) / 60UL);
}

uint16_t minutesUntilNextGatewayWindow() {
	// Calculate minutes until the next aligned gateway listening window.
	// This ensures nodes wake at the gateway's listening schedule boundary
	// rather than using a relative offset from their current time.
	conv.withCurrentTime().convert();
	const auto localTime = conv.getLocalTimeHMS();
	
	const uint16_t frequencyMinutes = gatewayDesiredReportFrequencyMinutes();
	const unsigned long frequencySeconds = (unsigned long)frequencyMinutes * 60UL;
	const unsigned long secondsSinceLocalMidnight = 
		((unsigned long)localTime.hour * 3600UL) + 
		((unsigned long)localTime.minute * 60UL) + 
		(unsigned long)localTime.second;
	
	// Calculate seconds until next aligned boundary
	const unsigned long nextBoundarySeconds = 
		frequencySeconds - (secondsSinceLocalMidnight % frequencySeconds);
	
	// Convert to minutes, rounding up, never return 0
	return (uint16_t)max(1UL, (nextBoundarySeconds + 59UL) / 60UL);
}

GatewayScheduleHint gatewayScheduleHint() {
	if (!Time.isValid()) {
		return {gatewayDesiredReportFrequencyMinutes(), true};
	}
	if (shouldSendClosedHoursHint()) {
		return {minutesUntilNextOpening(), false};
	}
	// Return actual minutes until next gateway window, not frequency
	return {minutesUntilNextGatewayWindow(), true};
}

time_t gatewayAckTimestamp() {
	return Time.isValid() ? Time.now() : 0;
}

void encodeGatewayAckTimestamp(uint8_t *payload, time_t gatewayTime) {
	payload[2] = ((uint8_t) (gatewayTime >> 24));
	payload[3] = ((uint8_t) (gatewayTime >> 16));
	payload[4] = ((uint8_t) (gatewayTime >> 8));
	payload[5] = ((uint8_t) gatewayTime);
}

PersistSnapshot capturePersistSnapshot() {
	const NodeDbPersistStats stats = nodeDatabase.getPersistStats();
	return {stats.saveCount, stats.totalMs, stats.lastMs, stats.lastMirrorMs};
}

void logPersistWindow(const char *context, uint32_t elapsedMs, const PersistSnapshot &before) {
	const PersistSnapshot after = capturePersistSnapshot();
	const uint32_t saveDelta = after.saveCount - before.saveCount;
	if (saveDelta == 0) {
		return;
	}

	if (elapsedMs > PRE_ACK_PERSIST_CRITICAL_MS || after.lastMs > PRE_ACK_PERSIST_CRITICAL_MS || after.lastMirrorMs > PRE_ACK_PERSIST_CRITICAL_MS) {
		Log.error("PersistCrit: ctx=%s preAck=%lums saves=%lu save=%ums mirror=%ums", context, (unsigned long)elapsedMs, (unsigned long)saveDelta, after.lastMs, after.lastMirrorMs);
	}
	else if (elapsedMs > PRE_ACK_PERSIST_WARN_MS || after.lastMs > PRE_ACK_PERSIST_WARN_MS || after.lastMirrorMs > PRE_ACK_PERSIST_WARN_MS || saveDelta > 1) {
		Log.warn("PersistWarn: ctx=%s preAck=%lums saves=%lu save=%ums mirror=%ums", context, (unsigned long)elapsedMs, (unsigned long)saveDelta, after.lastMs, after.lastMirrorMs);
	}
	else if (elapsedMs > PRE_ACK_PERSIST_INFO_MS || after.lastMs > PRE_ACK_PERSIST_INFO_MS || after.lastMirrorMs > PRE_ACK_PERSIST_INFO_MS) {
		Log.info("Persist: ctx=%s preAck=%lums saves=%lu save=%ums mirror=%ums", context, (unsigned long)elapsedMs, (unsigned long)saveDelta, after.lastMs, after.lastMirrorMs);
	}
}

} // anonymous namespace

GatewayBatteryBackoffState gatewayBatteryBackoffState() {
	return computeGatewayBatteryBackoffState();
}

static const char *rhModeName(RHGenericDriver::RHMode mode) __attribute__((unused));

static const char *rhModeName(RHGenericDriver::RHMode mode) {
	switch (mode) {
		case RHGenericDriver::RHModeInitialising:
			return "initialising";
		case RHGenericDriver::RHModeSleep:
			return "sleep";
		case RHGenericDriver::RHModeIdle:
			return "idle";
		case RHGenericDriver::RHModeTx:
			return "tx";
		case RHGenericDriver::RHModeRx:
			return "rx";
		case RHGenericDriver::RHModeCad:
			return "cad";
		default:
			return "unknown";
	}
}

// Singleton instance of the radio driver
class DiagnosticRH_RF95 : public RH_RF95 {
public:
	static DiagnosticRH_RF95 *interruptOwner;

	DiagnosticRH_RF95(uint8_t slaveSelectPin, uint8_t interruptPin) : RH_RF95(slaveSelectPin, interruptPin) {
	}

	void attachDiagnosticInterrupt() {
		interruptOwner = this;
		attachInterrupt(RFM95_INT, loraDio0DiagnosticISR, RISING);
	}

	void handleDiagnosticInterrupt() {
		RH_RF95::handleInterrupt();
	}

	uint8_t getIrqFlagsRegister() {
		return spiRead(RH_RF95_REG_12_IRQ_FLAGS);
	}
};

DiagnosticRH_RF95 *DiagnosticRH_RF95::interruptOwner = nullptr;

void loraDio0DiagnosticISR() {
	loraDio0InterruptCount++;
	if (DiagnosticRH_RF95::interruptOwner) {
		DiagnosticRH_RF95::interruptOwner->handleDiagnosticInterrupt();
	}
}

DiagnosticRH_RF95 driver(RFM95_CS, RFM95_INT);

class DiagnosticRHMesh : public RHMesh {
public:
	DiagnosticRHMesh(RHGenericDriver& driver, uint8_t thisAddress) : RHMesh(driver, thisAddress) {
	}

	bool recvfromAck(uint8_t* payload, uint8_t* payloadLen, uint8_t* source = NULL, uint8_t* dest = NULL, uint8_t* id = NULL, uint8_t* flags = NULL, uint8_t* hops = NULL) {
		uint8_t tmpMessageLen = sizeof(diagnosticMessage);
		uint8_t routeSource;
		uint8_t routeDest;
		uint8_t routeId;
		uint8_t routeFlags;
		uint8_t routeHops;

		if (RHRouter::recvfromAck(diagnosticMessage, &tmpMessageLen, &routeSource, &routeDest, &routeId, &routeFlags, &routeHops)) {
			MeshMessageHeader* meshHeader = (MeshMessageHeader*)&diagnosticMessage;

			if (tmpMessageLen >= 1 && meshHeader->msgType == RH_MESH_MESSAGE_TYPE_APPLICATION) {
				MeshApplicationMessage* applicationMessage = (MeshApplicationMessage*)meshHeader;
				if (source) *source = routeSource;
				if (dest) *dest = routeDest;
				if (id) *id = routeId;
				if (flags) *flags = routeFlags;
				if (hops) *hops = routeHops;
				uint8_t messageLen = tmpMessageLen - sizeof(MeshMessageHeader);
				if (*payloadLen > messageLen) {
					*payloadLen = messageLen;
				}
				memcpy(payload, applicationMessage->data, *payloadLen);
				return true;
			}
			else if (routeDest == RH_BROADCAST_ADDRESS && tmpMessageLen > 1 && meshHeader->msgType == RH_MESH_MESSAGE_TYPE_ROUTE_DISCOVERY_REQUEST) {
				MeshRouteDiscoveryMessage* discoveryMessage = (MeshRouteDiscoveryMessage*)meshHeader;
				uint8_t routeCount = tmpMessageLen - sizeof(MeshMessageHeader) - 2;

				LORA_DIAG_LOG("LoRa diag route req src=%u via=%u dest=%u hops=%u rssi=%d snr=%d", routeSource, headerFrom(), discoveryMessage->dest, routeHops, driver.lastRssi(), driver.lastSNR());

				if (routeSource == _thisAddress) {
					return false;
				}

				for (uint8_t routeIndex = 0; routeIndex < routeCount; routeIndex++) {
					if (discoveryMessage->route[routeIndex] == _thisAddress) {
						return false;
					}
				}

				addRouteTo(routeSource, headerFrom());

				if (_isa_router) {
					for (uint8_t routeIndex = 0; routeIndex < routeCount; routeIndex++) {
						addRouteTo(discoveryMessage->route[routeIndex], headerFrom());
					}
				}

				if (isPhysicalAddress(&discoveryMessage->dest, discoveryMessage->destlen)) {
					discoveryMessage->header.msgType = RH_MESH_MESSAGE_TYPE_ROUTE_DISCOVERY_RESPONSE;
					uint8_t routeResult __attribute__((unused)) = RHRouter::sendtoWait((uint8_t*)discoveryMessage, tmpMessageLen, routeSource);
					LORA_DIAG_LOG("LoRa diag route rsp %s to=%u for=%u code=%u", (routeResult == RH_ROUTER_ERROR_NONE) ? "sent" : "failed", routeSource, discoveryMessage->dest, routeResult);
				}
				else if ((routeCount < _max_hops) && _isa_router) {
					discoveryMessage->route[routeCount] = _thisAddress;
					tmpMessageLen++;
					RHRouter::sendtoFromSourceWait(diagnosticMessage, tmpMessageLen, RH_BROADCAST_ADDRESS, routeSource);
				}
			}
		}
		return false;
	}

private:
	uint8_t diagnosticMessage[RH_ROUTER_MAX_MESSAGE_LEN];
};

// Class to manage message delivery and receipt, using the driver declared above
DiagnosticRHMesh manager(driver, GATEWAY_ADDRESS);

// Mesh has much greater memory requirements, and you may need to limit the
// max message length to prevent wierd crashes
#ifndef RH_MAX_MESSAGE_LEN
#define RH_MAX_MESSAGE_LEN 255
#endif

// Mesh has much greater memory requirements, and you may need to limit the
// max message length to prevent wierd crashes
// #define RH_MESH_MAX_MESSAGE_LEN 50
uint8_t buf[RH_MESH_MAX_MESSAGE_LEN];               // Related to max message size - RadioHead example note: dont put this on the stack:

bool LoRA_Functions::setup(bool gatewayID) {
    // Set up the Radio Module
	LoRA_Functions::initializeRadio();

	if (gatewayID == true) {
		sysStatus.set_nodeNumber(GATEWAY_ADDRESS);							// Gateway - Manager is initialized by default with GATEWAY_ADDRESS - make sure it is stored in FRAM
		Log.info("LoRA Radio initialized as a gateway (address %d) with a deviceID of %s", GATEWAY_ADDRESS, System.deviceID().c_str());
		LORA_DIAG_LOG("LoRa diag setup: addr=%u freq=%.2f modem=%s regs=%02x/%02x/%02x timeout=%u tx=%u pins cs=%d irq=%d rst=%d", manager.thisAddress(), RF95_FREQ, GATEWAY_MODEM_CONFIG_NAME, GATEWAY_MODEM_CONFIG.reg_1d, GATEWAY_MODEM_CONFIG.reg_1e, GATEWAY_MODEM_CONFIG.reg_26, GATEWAY_MANAGER_TIMEOUT_MS, GATEWAY_TX_POWER_DBM, (int)RFM95_CS, (int)RFM95_INT, (int)RFM95_RST);
		LORA_DIAG_LOG("LoRa diag setup: rh_rf95_version=0x%02x source_check=%s", driver.getDeviceVersion(), RADIOHEAD_GIT_DIFF_STATUS);
	}
	else if (sysStatus.get_nodeNumber() > 0 && sysStatus.get_nodeNumber() <= 10) {
		manager.setThisAddress(sysStatus.get_nodeNumber());// Node - use the Node address in valid range from memory
		Log.info("LoRA Radio initialized as node %i and a deviceID of %s", manager.thisAddress(), System.deviceID().c_str());
	}
	else {																						// Else, we will set as an unconfigured node
		sysStatus.set_nodeNumber(11);
		manager.setThisAddress(11);
		Log.info("LoRA Radio initialized as an unconfigured node %i and a deviceID of %s", manager.thisAddress(), System.deviceID().c_str());
	}

	// Here is where we load the JSON object from memory and parse
	const String nodeDbJson = nodeDatabase.get_nodeIDJson();
	const size_t nodeDbLen = nodeDatabase.get_nodeIDJsonLength();
	SYSTEM_VERBOSE_LOG("The node string is: %s", nodeDbJson.c_str());

	if (parseNodeDatabase(nodeDbJson, nodeDbLen, true)) {
		Log.info("NodeDB loaded: nodes=%u", nodeDatabaseCount());
	}
	else {
		preserveCorruptNodeDb(nodeDbJson.c_str(), nodeDbLen);
		if (nodeDatabase.resetNodeIDs()) {
			const String repairedNodeDbJson = nodeDatabase.get_nodeIDJson();
			const size_t repairedNodeDbLen = nodeDatabase.get_nodeIDJsonLength();
			if (parseNodeDatabase(repairedNodeDbJson, repairedNodeDbLen, true)) {
				Log.info("NodeDB loaded: nodes=%u", nodeDatabaseCount());
			}
		}
	}
	return true;
}

void LoRA_Functions::loop() {
												
}


// ************************************************************************
// *****					Common LoRA Functions					*******
// ************************************************************************


void LoRA_Functions::clearBuffer() {
	uint8_t bufT[RH_RF95_MAX_MESSAGE_LEN];
	while (true) {
		uint8_t lenT = sizeof(bufT);
		if (!driver.recv(bufT, &lenT)) {
			break;
		}
	}
}

void LoRA_Functions::sleepLoRaRadio() {
	driver.sleep();                             	// Here is where we will power down the LoRA radio module
}

void LoRA_Functions::logGatewayStateEntry() {
	const GatewayBatteryBackoffState state = gatewayBatteryBackoffState();
	Log.info("BatteryBackoff: level=%u soc=%.0f reportFrequency=%u listenWindowSeconds=%u", state.level, state.soc, state.reportFrequencyMinutes, state.listenWindowSeconds);
	#if !LORA_DIAGNOSTICS
	return;
	#else
	manager.available();
	const RHGenericDriver::RHMode mode __attribute__((unused)) = driver.mode();
	LORA_DIAG_LOG("LoRa diag state: rx_armed=%s mode=%s addr=%u freq=%.2f modem=%s timeout=%u", (mode == RHGenericDriver::RHModeRx) ? "yes" : "no", rhModeName(mode), manager.thisAddress(), RF95_FREQ, GATEWAY_MODEM_CONFIG_NAME, GATEWAY_MANAGER_TIMEOUT_MS);
	#endif
}

void LoRA_Functions::attachGatewayDio0DiagnosticInterrupt() {
	if (!LORA_DIAGNOSTICS) {
		return;
	}
	driver.attachDiagnosticInterrupt();
}

void LoRA_Functions::logGatewayDio0Setup(const char *context) {
	if (!LORA_DIAGNOSTICS) {
		return;
	}
	LORA_DIAG_LOG("LoRa diag dio0 %s: irq_pin=%d level=%d count=%lu", context, (int)RFM95_INT, (int)digitalRead(RFM95_INT), (unsigned long)loraDio0InterruptCount);
}

void LoRA_Functions::logGatewayDio0Snapshot() {
	#if !LORA_DIAGNOSTICS
	return;
	#else
	static uint32_t lastLoggedDio0Count = 0;
	static system_tick_t lastSnapshotMs = 0;
	const uint32_t dio0Count = loraDio0InterruptCount;
	const uint8_t irqFlags = driver.getIrqFlagsRegister();
	if (dio0Count == lastLoggedDio0Count && irqFlags == 0 && (millis() - lastSnapshotMs) < 30000UL) {
		return;
	}
	const bool managerAvailable __attribute__((unused)) = manager.available();
	const bool driverAvailable __attribute__((unused)) = driver.available();
	lastLoggedDio0Count = dio0Count;
	lastSnapshotMs = millis();
	LORA_DIAG_LOG("LoRa diag dio0: level=%d count=%lu mode=%s irq_flags=0x%02x rx_done=%s crc_error=%s manager_available=%s driver_available=%s", (int)digitalRead(RFM95_INT), (unsigned long)loraDio0InterruptCount, rhModeName(driver.mode()), irqFlags, (irqFlags & RH_RF95_RX_DONE) ? "yes" : "no", (irqFlags & RH_RF95_PAYLOAD_CRC_ERROR) ? "yes" : "no", managerAvailable ? "yes" : "no", driverAvailable ? "yes" : "no");
	#endif
}

bool  LoRA_Functions::initializeRadio() {  			// Set up the Radio Module
	digitalWrite(RFM95_RST,LOW);					// Reset the radio module before setup
	delay(10);
	digitalWrite(RFM95_RST,HIGH);
	delay(10);

	if (!manager.init()) {
		Log.info("init failed");					// Defaults after init are 434.0MHz, 0.05MHz AFC pull-in, modulation FSK_Rb2_4Fd36
		return false;
	}
	driver.setFrequency(RF95_FREQ);					// Frequency is typically 868.0 or 915.0 in the Americas, or 433.0 in the EU - Are there more settings possible here?
	driver.setTxPower(GATEWAY_TX_POWER_DBM, false); // If you are using RFM95/96/97/98 modules which uses the PA_BOOST transmitter pin, then you can set transmitter powers from 5 to 23 dBm (13dBm default).  PA_BOOST?
	driver.setModemRegisters(&GATEWAY_MODEM_CONFIG);
	driver.setLowDatarate();						// https://www.airspayce.com/mikem/arduino/RadioHead/classRH__RF95.html#a8e2df6a6d2cb192b13bd572a7005da67
	manager.setTimeout(GATEWAY_MANAGER_TIMEOUT_MS);			// 200mSec is the default - may need to extend once we play with other settings on the modem - https://www.airspayce.com/mikem/arduino/RadioHead/classRHReliableDatagram.html
return true;
}


// ************************************************************************
// *****                      Gateway Functions                       *****
// ************************************************************************

// Common across message types - these messages are general for send and receive

bool LoRA_Functions::listenForLoRAMessageGateway() {
	uint8_t len = sizeof(buf);
	uint8_t from;
	uint8_t dest;
	uint8_t id;
	uint8_t messageFlag;
	uint8_t hops;
	if (manager.recvfromAck(buf, &len, &from, &dest, &id, &messageFlag, &hops)) {	// We have received a message - need to validate it
		LORA_DIAG_LOG("LoRa diag recvfromAck: ok from=%u to=%u id=%u flags=0x%02x hops=%u rssi=%d snr=%d", from, dest, id, messageFlag, hops, driver.lastRssi(), driver.lastSNR());
		const uint16_t packetMagic = decodeUnsigned16At(buf, 0, 1);
		const uint16_t packetNodeId = decodeUnsigned16At(buf, 2, 3);

		// First we will validate that this node belongs in this network by checking the magic number
		if (!(packetMagic == sysStatus.get_magicNumber())) {
			Log.info("Node %d message magic number of %u did not match the Magic Number in memory %u - Ignoring", current.get_nodeNumber(), packetMagic, sysStatus.get_magicNumber());
			return false;
		}
		current.set_nodeNumber(from);												// Captures the nodeNumber
		current.set_tempNodeNumber(0);											// Clear for new response
		current.set_hops(hops);													// How many hops to get here
		current.set_nodeID(packetNodeId);									// Captures the nodeID for Data or Alert reports
		lora_state = (LoRA_State)(0x0F & messageFlag);								// Strip out the overhead byte to get the message flag
		if (lora_state == DATA_RPT) {
			LORA_DIAG_LOG("LoRa diag data_rpt: node=%u id=%u hops=%u rssi=%d snr=%d", from, current.get_nodeID(), hops, driver.lastRssi(), driver.lastSNR());
		}
		Log.info("Node %d with ID %d a %s message with RSSI/SNR of %d / %d in %d hops", current.get_nodeNumber(), current.get_nodeID(), loraStateNames[lora_state], driver.lastRssi(), driver.lastSNR(), current.get_hops());

		// Next we need to test the nodeNumber / deviceID to make sure this node is properly configured
		if (current.get_nodeNumber() < 11 && !LoRA_Functions::instance().nodeConfigured(current.get_nodeNumber(), current.get_nodeID())) {
			Log.info("Node not properly configured, resetting node number");
			current.set_tempNodeNumber(current.get_nodeNumber());					// Store node number in temp for the repsonse
			current.set_nodeNumber(11);											// Set node number to 11
		}
		else if (current.get_nodeNumber() >= 11) {
			current.set_tempNodeNumber(from);										// We need this address for the reply
			current.set_nodeNumber(11);											// This way an unconfigured nor invalid node ends up wtih a node number of 11
		}

		if (lora_state == DATA_RPT) {if(!LoRA_Functions::instance().decipherDataReportGateway()) return false;}
		else if (lora_state == JOIN_REQ) {if(!LoRA_Functions::instance().decipherJoinRequestGateway()) return false;}
		else {Log.info("Invalid message flag, returning"); return false;}

		// At this point the message is valid and has been deciphered - the response will carry the pending desired frequency.
		// The response will be specific to the message type
		if (lora_state == DATA_ACK) { if(LoRA_Functions::instance().acknowledgeDataReportGateway()) return true;}
		else if (lora_state == JOIN_ACK) { if(LoRA_Functions::instance().acknowledgeJoinRequestGateway()) return true;}
		else {Log.info("Invalid message flag"); return false;}
	}
	else LoRA_Functions::clearBuffer();
	return false;

}

// These are the receive and respond messages for data reports

bool LoRA_Functions::decipherDataReportGateway() {			// Receives the data report and loads results into current object for reporting
	const uint16_t hourlyCount = decodeUnsigned16At(buf, 4, 5);
	const uint16_t dailyCount = decodeUnsigned16At(buf, 6, 7);
	const int16_t nodeRssi = decodeSigned16At(buf, 15, 16);
	const int16_t nodeSnr = decodeSigned16At(buf, 17, 18);

	current.set_hourlyCount(hourlyCount);
	current.set_dailyCount(dailyCount);
	current.set_sensorType(buf[8]);
	current.set_internalTempC(buf[9]);
	current.set_stateOfCharge(buf[10]);
	current.set_batteryState(buf[11]);
	current.set_resetCount(buf[12]);
	current.set_messageCount(buf[13]);
	current.set_successCount(buf[14]);
	current.set_RSSI(nodeRssi);										// These values are from the node based on the last successful data report
	current.set_SNR(nodeSnr);

	LORA_DIAG_LOG("LoRa diag node decode: node=%u msg=%u rawRSSI=%02x%02x rawSNR=%02x%02x decRSSI=%d decSNR=%d temp=%d soc=%.0f battState=%u hourly=%u daily=%u hops=%u", current.get_nodeNumber(), current.get_messageCount(), buf[15], buf[16], buf[17], buf[18], (int)nodeRssi, (int)nodeSnr, (int)current.get_internalTempC(), current.get_stateOfCharge(), (unsigned)current.get_batteryState(), hourlyCount, dailyCount, current.get_hops());

	lora_state = DATA_ACK;		// Prepare to respond
	return true;
}

bool LoRA_Functions::acknowledgeDataReportGateway() { 		// This is a response to a data message 
	char messageString[128];
	const GatewayBatteryBackoffState backoffState = gatewayBatteryBackoffState();
	const GatewayScheduleHint scheduleHint = gatewayScheduleHint();
	const time_t ackTime = gatewayAckTimestamp();
	byte pendingAlert = 0;
	bool clearPendingAlert = false;
	float successPercent = 0.0f;

	// buf[0] - buf[1] is magic number - processed above
	encodeGatewayAckTimestamp(buf, ackTime);
	buf[6] = highByte(scheduleHint.frequencyMinutes);	// Frequency of reports set by the gateway
	buf[7] = lowByte(scheduleHint.frequencyMinutes);	
	// The next few bytes of the response will depend on whether the node is configured or not
	if (current.get_nodeNumber() == 11) {			// This is a data report from an unconfigured node - need to tell it to rejoin
		Log.info("Node %d is invalid, setting alert code to 1", current.get_nodeNumber());
		current.set_alertCodeNode(1);				// This will ensure the node rejoins the network
		current.set_alertTimestampNode(Time.now());
		buf[8] = current.get_alertCodeNode();												
		buf[9] = current.get_sensorType();			// Since the node is unconfigured, we need to beleive it when it tells us the type
	}
	else {											// This is a data report from a configured node - will use the node database
		pendingAlert = LoRA_Functions::getAlert(current.get_nodeNumber());
		current.set_alertCodeNode(pendingAlert);
		if (current.get_alertCodeNode() > 0) Log.info("Node %d has a pending alert %d", current.get_nodeNumber(), current.get_alertCodeNode());
	
		if (current.get_alertCodeNode() == 7) {		// if it is a change in type alert - we can do that here
			int newSensorType = LoRA_Functions::getType(current.get_nodeNumber());
			Log.info("In data acknowledge, changing type to from %d to %d", current.get_sensorType(), newSensorType );
			current.set_sensorType(newSensorType);	// Update current value for data report
			buf[9] = newSensorType;
		}
		else buf[9] = current.get_sensorType();

		clearPendingAlert = (current.get_alertCodeNode() != 0);
		buf[8] = current.get_alertCodeNode();

		if (current.get_messageCount()==0) successPercent = 0.0;
		else successPercent = ((current.get_successCount()+1.0)/(float)current.get_messageCount()) * 100.0;  // Add one to success because we are receving the message
	}
	buf[10] = scheduleHint.openHours;
	buf[11] = current.get_messageCount();			// Repeat back message number

	// nodeDatabase.flush(true);					// Save updates to the nodID database
	// current.flush(true);							// Save values reported by the nodes
	digitalWrite(BLUE_LED,HIGH);			       	// Sending data

	byte nodeAddress = (current.get_tempNodeNumber() == 0) ? current.get_nodeNumber() : current.get_tempNodeNumber();  // get the return address right

	if (manager.sendtoWait(buf, 12, nodeAddress, DATA_ACK) == RH_ROUTER_ERROR_NONE) {
		digitalWrite(BLUE_LED,LOW);

		if (current.get_nodeNumber() != 11) {
			const system_tick_t persistStart = millis();
			const PersistSnapshot beforePersist = capturePersistSnapshot();
			const JsonParserGeneratorRK::jsmntok_t *nodesArrayContainer;
			jp.getValueTokenByKey(jp.getOuterObject(), "nodes", nodesArrayContainer);
			const JsonParserGeneratorRK::jsmntok_t *nodeObjectContainer = jp.getTokenByIndex(nodesArrayContainer, current.get_nodeNumber() - 1);
			const NodeFrequencyState nodeFrequencyState = readNodeFrequencyState(nodeObjectContainer);
			if (nodeFrequencyState.nodeAcknowledgedFrequencyMinutes != scheduleHint.frequencyMinutes) {
				Log.info("FrequencyChange: node=%d old=%u new=%u reason=BATTERY_BACKOFF", current.get_nodeNumber(), nodeFrequencyState.nodeAcknowledgedFrequencyMinutes, scheduleHint.frequencyMinutes);
			}
			if (clearPendingAlert) {
				LoRA_Functions::changeAlert(current.get_nodeNumber(), 0, false);
			}
			LoRA_Functions::instance().nodeUpdate(current.get_nodeNumber(), successPercent, false);
			writeNodeFrequencyState(nodeObjectContainer, scheduleHint.frequencyMinutes, scheduleHint.frequencyMinutes, true);
			syncGatewayFrequencyWithBatteryBackoff(backoffState);
			logPersistWindow("dataAckPost", millis() - persistStart, beforePersist);
		}

		const int nodeRssi = (int)current.get_RSSI();
		const int nodeSnr = (int)current.get_SNR();
		snprintf(messageString,sizeof(messageString),"Node %d data report %d acknowledged with alert %d, next window %s in %u minutes, and RSSI / SNR of %d / %d", current.get_nodeNumber(), buf[11], buf[8], buf[10] ? "open" : "closed", (unsigned)decodeUnsigned16At(buf, 6, 7), nodeRssi, nodeSnr);
		Log.info("%s", messageString);
		if (Particle.connected()) Particle.publish("status", messageString,PRIVATE);
		return true;
	}
	else {
		Log.info("Node %d data report response not acknowledged", nodeAddress);
		digitalWrite(BLUE_LED,LOW);
		return false;
	}
}


// These are the receive and respond messages for join requests
bool LoRA_Functions::decipherJoinRequestGateway() {			// Ths only question here is whether the node with the join request needs a new nodeNumber or is just looking for a clock set
	char nodeDeviceID[25];
	const system_tick_t persistStart = millis();
	const PersistSnapshot beforePersist = capturePersistSnapshot();
	// buf[0] - buf[1] Magic number processed above
	// but[2] - buf[3] nodeID processed above
	// buf[4] - buf[28] needs to be loaded here
	for (uint8_t i=0; i<sizeof(nodeDeviceID); i++) {
		nodeDeviceID[i] = buf[i+4];
	}
	current.set_sensorType(buf[29]);								// Store device type in the current data buffer 
	current.set_nodeNumber(findNodeNumber(nodeDeviceID, current.get_nodeID(), false));		// Look up the new node number
	
	Log.info("Node %d join request from %s will change node number to %d", current.get_tempNodeNumber(), nodeDeviceID ,current.get_nodeNumber());

	current.set_alertCodeNode(1);									// This is a join request so alert code is 1
	current.set_alertTimestampNode(Time.now());

	LoRA_Functions::changeType(current.get_nodeNumber(), current.get_sensorType(), false);  // Record the sensor type in the nodeID structure
	logPersistWindow("joinPreAck", millis() - persistStart, beforePersist);

	lora_state = JOIN_ACK;			// Prepare to respond
	return true;
}

bool LoRA_Functions::acknowledgeJoinRequestGateway() {
	char messageString[128];
	const GatewayBatteryBackoffState backoffState = gatewayBatteryBackoffState();
	const GatewayScheduleHint scheduleHint = gatewayScheduleHint();
	const time_t ackTime = gatewayAckTimestamp();
	Log.info("Acknowledge Join Request");
	// This is a response to a data message and a specific payload and message flag
	// Send a reply back to the originator client
     
	buf[0] = highByte(sysStatus.get_magicNumber());					// Magic number - so you can trust me
	buf[1] = lowByte(sysStatus.get_magicNumber());					// Magic number - so you can trust me
	encodeGatewayAckTimestamp(buf, ackTime);
	buf[6] = highByte(scheduleHint.frequencyMinutes);			// Frequency of reports - for Gateways
	buf[7] = lowByte(scheduleHint.frequencyMinutes);	
	buf[8] = (current.get_nodeNumber() != 11) ?  0 : 1;				// Clear the alert code for the node unless the nodeNumber process failed
	buf[9] = current.get_nodeNumber();								
	buf[10] = current.get_sensorType();								// In a join request the node type overwrites the node database value


	digitalWrite(BLUE_LED,HIGH);			        				// Sending data

	byte nodeAddress = (current.get_tempNodeNumber() == 0) ? current.get_nodeNumber() : current.get_tempNodeNumber();  // get the return address right

	Log.info("Sending response to %d with free memory = %li", nodeAddress, System.freeMemory());

	if (manager.sendtoWait(buf, 11, nodeAddress, JOIN_ACK) == RH_ROUTER_ERROR_NONE) {
		current.set_tempNodeNumber(0);								// Temp no longer needed
		digitalWrite(BLUE_LED,LOW);
		const JsonParserGeneratorRK::jsmntok_t *nodesArrayContainer;
		jp.getValueTokenByKey(jp.getOuterObject(), "nodes", nodesArrayContainer);
		const JsonParserGeneratorRK::jsmntok_t *nodeObjectContainer = jp.getTokenByIndex(nodesArrayContainer, current.get_nodeNumber() - 1);
		const NodeFrequencyState nodeFrequencyState = readNodeFrequencyState(nodeObjectContainer);
		if (nodeFrequencyState.nodeAcknowledgedFrequencyMinutes != scheduleHint.frequencyMinutes) {
			Log.info("FrequencyChange: node=%d old=%u new=%u reason=BATTERY_BACKOFF", current.get_nodeNumber(), nodeFrequencyState.nodeAcknowledgedFrequencyMinutes, scheduleHint.frequencyMinutes);
		}
		writeNodeFrequencyState(nodeObjectContainer, scheduleHint.frequencyMinutes, scheduleHint.frequencyMinutes, true);
		syncGatewayFrequencyWithBatteryBackoff(backoffState);
		const int nodeRssi = (int)current.get_RSSI();
		const int nodeSnr = (int)current.get_SNR();
		snprintf(messageString,sizeof(messageString),"Node %d joined with sensorType %s, alert %d and RSSI / SNR of %d / %d", nodeAddress, (buf[10] ==0)? "car":"person",current.get_alertCodeNode(), nodeRssi, nodeSnr);
		Log.info("%s", messageString);
		if (Particle.connected()) Particle.publish("status", messageString,PRIVATE);
		return true;
	}
	else {
		Log.info("Node %d join response not acknowledged", current.get_tempNodeNumber()); // Acknowledgement not received - this needs more attention as node is in undefined state
		digitalWrite(BLUE_LED,LOW);
		return false;
	}
}

// ************************************************************************
// *****             Node Management  Functions                       *****
// ************************************************************************

/* NodeID JSON structure
{nodes:[
	{
		{node:(int)nodeNumber},
		{dID: (String)deviceID},
		{rID: (int)radioID},
		{last: (time_t)lastConnectTime},
		{type: (int)sensorType},
		{succ: (float)successfulSent%},
		{pend: (int)pendingAlerts}
	},
	....]
}
*/


// These functions access data in the nodeID JSON
uint8_t LoRA_Functions::findNodeNumber(const char* deviceID, int radioID, bool persistNow) {
	int index=1;															// Variables to hold values for the function
	String nodeDeviceID;
	int nodeNumber;

	if (radioID != LoRA_Functions::stringCheckSum(deviceID)) {
		Log.info("DeviceID and checksum mismatch - setting node to 11");
		return 11;															// Return value for unconfigured node
	}
	else Log.info("Checksum validated");

	const JsonParserGeneratorRK::jsmntok_t *nodesArrayContainer;			// Token for the outer array
	jp.getValueTokenByKey(jp.getOuterObject(), "nodes", nodesArrayContainer);
	const JsonParserGeneratorRK::jsmntok_t *nodeObjectContainer;			// Token for the objects in the array (I beleive)

	for (int i=0; i<10; i++) {												// Iterate through the array looking for a match
		nodeObjectContainer = jp.getTokenByIndex(nodesArrayContainer, i);
		if(nodeObjectContainer == NULL) {
			Log.info("findNodeNumber ran out of entries at i = %d",i);
			break;															// Ran out of entries - no match found
		} 
		jp.getValueByKey(nodeObjectContainer, "dID", nodeDeviceID);			// Get the deviceID and compare
		if (nodeDeviceID == deviceID) {
			jp.getValueByKey(nodeObjectContainer, "node", nodeNumber);		// A match!
			return nodeNumber;												// All is good - return node number for the deviceID passed to the function
		}
		index++;															// This will be the node number for the next node if no match is found
	}
	// If we got to here, the deviceID was not a match for any entry and a new nodeNumer will be assigned
	nodeNumber = index;
	JsonModifier mod(jp);

	Log.info("New node will be assigned number %d, deviceID of %s",nodeNumber, deviceID);

	mod.startAppend(jp.getOuterArray());
		mod.startObject();
		mod.insertKeyValue("node", nodeNumber);
		mod.insertKeyValue("dID", deviceID);
		mod.insertKeyValue("rID",radioID);
		mod.insertKeyValue("last", Time.now());
		mod.insertKeyValue("type", (int)3);									// This is a temp value that will be updated
		mod.insertKeyValue("succ",(float)0.0);								// This is a temp value that will be updated
		mod.insertKeyValue("pend",(int)0);
			mod.insertKeyValue("desiredReportFrequency", (int)60);
			mod.insertKeyValue("nodeAcknowledgedFrequency", (int)60);
		mod.finishObjectOrArray();
	mod.finish();

	if (persistNow) {
		nodeDatabase.saveNodeIDJson(jp.getBuffer());
	}

	return index;
}

String LoRA_Functions::findDeviceID(int nodeNumber, int radioID)  {
	String nodeDeviceID;
	int nodeRadioID;

	const JsonParserGeneratorRK::jsmntok_t *nodesArrayContainer;			// Token for the outer array
	jp.getValueTokenByKey(jp.getOuterObject(), "nodes", nodesArrayContainer);
	const JsonParserGeneratorRK::jsmntok_t *nodeObjectContainer;			// Token for the objects in the array (I beleive)

	nodeObjectContainer = jp.getTokenByIndex(nodesArrayContainer, nodeNumber-1);
	if(nodeObjectContainer == NULL) return "null";							// Ran out of entries - no match found

	jp.getValueByKey(nodeObjectContainer,"rID", nodeRadioID);				// Get the radioID to see if it is a match
	if (nodeRadioID != radioID) return "null";								// Not the right nodeNumber / nodeID combo

	jp.getValueByKey(nodeObjectContainer, "dID", nodeDeviceID);				// Get the deviceID and compare
	if (nodeDeviceID == NULL) return "null";
	else return nodeDeviceID;
}

bool LoRA_Functions::nodeConfigured(int nodeNumber, int radioID)  {
	if (nodeNumber > 10) return false;

	const JsonParserGeneratorRK::jsmntok_t *nodesArrayContainer;			// Token for the outer array
	jp.getValueTokenByKey(jp.getOuterObject(), "nodes", nodesArrayContainer);
	const JsonParserGeneratorRK::jsmntok_t *nodeObjectContainer;			// Token for the objects in the array (I beleive)

	nodeObjectContainer = jp.getTokenByIndex(nodesArrayContainer, nodeNumber-1);
	if(nodeObjectContainer == NULL) return false;							// Ran out of entries - no match found
	
	jp.getValueByKey(nodeObjectContainer, "rID", radioID);					// Get the radioID for the node number in question

	if (radioID == current.get_nodeID()) return true;
	else {
		Log.info("Node not configured");  // See the raw JSON string
		return false;
	}
}

bool LoRA_Functions::nodeUpdate(int nodeNumber, float successPercent, bool persistNow)  {

	const JsonParserGeneratorRK::jsmntok_t *nodesArrayContainer;			// Token for the outer array
	jp.getValueTokenByKey(jp.getOuterObject(), "nodes", nodesArrayContainer);
	const JsonParserGeneratorRK::jsmntok_t *nodeObjectContainer;			// Token for the objects in the array (I beleive)

	nodeObjectContainer = jp.getTokenByIndex(nodesArrayContainer, nodeNumber-1);
	if(nodeObjectContainer == NULL) return false;							// Ran out of entries - no match found

	const JsonParserGeneratorRK::jsmntok_t *value;

	jp.getValueTokenByKey(nodeObjectContainer, "last", value);			// Update last connection time
	JsonModifier mod(jp);
	mod.startModify(value);
	mod.insertValue((int)Time.now());
	mod.finish();

	jp.getValueTokenByKey(nodeObjectContainer, "succ", value);			// Update the success percentage value
	mod.startModify(value);
	mod.insertValue((float)successPercent);
	mod.finish();

	if (persistNow) {
		nodeDatabase.saveNodeIDJson(jp.getBuffer());
	}
	return true;
}

byte LoRA_Functions::getType(int nodeNumber) {
	int type;

	const JsonParserGeneratorRK::jsmntok_t *nodesArrayContainer;			// Token for the outer array
	jp.getValueTokenByKey(jp.getOuterObject(), "nodes", nodesArrayContainer);
	const JsonParserGeneratorRK::jsmntok_t *nodeObjectContainer;			// Token for the objects in the array (I beleive)

	nodeObjectContainer = jp.getTokenByIndex(nodesArrayContainer, nodeNumber-1);
	if(nodeObjectContainer == NULL) {
		Log.info("From getType function Node number not found so returning %d",current.get_sensorType());
		return current.get_sensorType();									// Ran out of entries, go with what was reported by the node
	} 

	jp.getValueByKey(nodeObjectContainer, "type", type);
	Log.info("Returning sensor type %d",type);
	return type;
}

bool LoRA_Functions::changeType(int nodeNumber, int newType, bool persistNow) {
	if (nodeNumber > 10) return false;
	int type;

	const JsonParserGeneratorRK::jsmntok_t *nodesArrayContainer;			// Token for the outer array
	jp.getValueTokenByKey(jp.getOuterObject(), "nodes", nodesArrayContainer);
	const JsonParserGeneratorRK::jsmntok_t *nodeObjectContainer;			// Token for the objects in the array (I beleive)

	nodeObjectContainer = jp.getTokenByIndex(nodesArrayContainer, nodeNumber-1);
	if(nodeObjectContainer == NULL) return false;								// Ran out of entries 

	jp.getValueByKey(nodeObjectContainer, "type", type);
	if (type == newType) {
		return true;
	}

	Log.info("Changing sensor type from %d to %d", type, newType);

	const JsonParserGeneratorRK::jsmntok_t *value;

	jp.getValueTokenByKey(nodeObjectContainer, "type", value);

	JsonModifier mod(jp);

	mod.startModify(value);

	mod.insertValue((int)newType);
	mod.finish();

	if (persistNow) {
		nodeDatabase.saveNodeIDJson(jp.getBuffer());
	}

	return true;

}

byte LoRA_Functions::getAlert(int nodeNumber) {
	if (nodeNumber > 10) return 255;										// Not a configured node

	int pendingAlert;

	const JsonParserGeneratorRK::jsmntok_t *nodesArrayContainer;			// Token for the outer array
	jp.getValueTokenByKey(jp.getOuterObject(), "nodes", nodesArrayContainer);
	const JsonParserGeneratorRK::jsmntok_t *nodeObjectContainer;			// Token for the objects in the array (I beleive)

	nodeObjectContainer = jp.getTokenByIndex(nodesArrayContainer, nodeNumber-1);
	if(nodeObjectContainer == NULL) {
		Log.info("From getAlert function, Node number not found");
		return 255;															// Ran out of entries 
	} 

	jp.getValueByKey(nodeObjectContainer, "pend", pendingAlert);

	return pendingAlert;

}

bool LoRA_Functions::changeAlert(int nodeNumber, int newAlert, bool persistNow) {
	int currentAlert;

	const JsonParserGeneratorRK::jsmntok_t *nodesArrayContainer;			// Token for the outer array
	jp.getValueTokenByKey(jp.getOuterObject(), "nodes", nodesArrayContainer);
	const JsonParserGeneratorRK::jsmntok_t *nodeObjectContainer;			// Token for the objects in the array

	nodeObjectContainer = jp.getTokenByIndex(nodesArrayContainer, nodeNumber-1);	// find the entry for the node of interest
	if(nodeObjectContainer == NULL) return false;							// Ran out of entries - node number entry not found triggers alert

	jp.getValueByKey(nodeObjectContainer, "pend", currentAlert);			// Now we have the oject for the specific node
	if (currentAlert == newAlert) {
		return true;
	}
	Log.info("Changing pending alert from %d to %d", currentAlert, newAlert);

	const JsonParserGeneratorRK::jsmntok_t *value;							// Node we have the key value pair for the "pend"ing alerts	
	jp.getValueTokenByKey(nodeObjectContainer, "pend", value);

	JsonModifier mod(jp);													// Create a modifier object
	mod.startModify(value);													// Update the pending alert value for the selected node
	mod.insertValue((int)newAlert);
	mod.finish();

	if (persistNow) {
		nodeDatabase.saveNodeIDJson(jp.getBuffer());
	}

	return true;
}

void LoRA_Functions::printNodeData(bool publish) {
	int nodeNumber;
	int radioID;
	String nodeDeviceID;
	int lastConnect;
	int sensorType;
	float successPercent;
	int pendingAlert;
	char data[256];

	const JsonParserGeneratorRK::jsmntok_t *nodesArrayContainer;			// Token for the outer array
	jp.getValueTokenByKey(jp.getOuterObject(), "nodes", nodesArrayContainer);
	const JsonParserGeneratorRK::jsmntok_t *nodeObjectContainer;			// Token for the objects in the array (I beleive)

	for (int i=0; i<10; i++) {												// Iterate through the array looking for a match
		nodeObjectContainer = jp.getTokenByIndex(nodesArrayContainer, i);
		if(nodeObjectContainer == NULL) {
			break;								// Ran out of entries 
		} 
		jp.getValueByKey(nodeObjectContainer, "dID", nodeDeviceID);
		jp.getValueByKey(nodeObjectContainer,"rID", radioID);
		jp.getValueByKey(nodeObjectContainer, "node", nodeNumber);
		jp.getValueByKey(nodeObjectContainer, "last", lastConnect);
		jp.getValueByKey(nodeObjectContainer, "type", sensorType);
		jp.getValueByKey(nodeObjectContainer, "succ", successPercent);
		jp.getValueByKey(nodeObjectContainer, "pend", pendingAlert);

		snprintf(data, sizeof(data), "Node %d, deviceID: %s, checksum %d, lastConnected: %s, type %d, success %4.2f with pending alert %d", nodeNumber, nodeDeviceID.c_str(), radioID, Time.timeStr(lastConnect).c_str(), sensorType, successPercent, pendingAlert);
		Log.info("%s", data);
		if (Particle.connected() && publish) {
			Particle.publish("nodeData", data, PRIVATE);
			delay(1000);
		}
	}

	// Log.info(nodeDatabase.get_nodeIDJson());  // See the raw JSON string

}

bool LoRA_Functions::nodeConnectionsHealthy() {								// Connections are healthy if at least one node connected in last two periods
// Resets the LoRA Radio if not healthy
	
	int lastConnect;
	time_t secondsPerPeriod = sysStatus.get_frequencyMinutes() * 60;
	bool health = true;

	const JsonParserGeneratorRK::jsmntok_t *nodesArrayContainer;			// Token for the outer array
	jp.getValueTokenByKey(jp.getOuterObject(), "nodes", nodesArrayContainer);
	const JsonParserGeneratorRK::jsmntok_t *nodeObjectContainer;			// Token for the objects in the array (I beleive)

	for (int i=0; i<10; i++) {												// Iterate through the array looking for a match
		nodeObjectContainer = jp.getTokenByIndex(nodesArrayContainer, i);
		if(nodeObjectContainer == NULL) break;								// Ran out of entries 

		jp.getValueByKey(nodeObjectContainer, "last", lastConnect);

		if ((Time.now() - lastConnect) > secondsPerPeriod) {				// If any of the nodes fail to connect - will extend loRA dwell time
			health = false;
			break;															// Don't need to keep checking
		}
	}

	Log.info("Node connections are %s ", (health) ? "healthy":"unhealthy");
	if(!health) LoRA_Functions::initializeRadio();
	return health;
}

int LoRA_Functions::stringCheckSum(String str){												// This function is made for the Particle DeviceID
    int result = 0;
    for(unsigned int i = 0; i < str.length(); i++){
      int asciiCode = (int)str[i];

      if (asciiCode >=48 && asciiCode <58) {              // 0-9
        result += asciiCode - 48;
      } 
      else if (asciiCode >=65 && asciiCode < 71) {        // A-F
        result += 10 + asciiCode -65;
      }
      else if (asciiCode >=97 && asciiCode < 103) {       // a - f
        result += 10 + asciiCode -97;
      }
    }
    return result;
}





