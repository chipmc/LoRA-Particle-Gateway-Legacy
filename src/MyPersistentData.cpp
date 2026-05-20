#include "Particle.h"
#include "MB85RC256V-FRAM-RK.h"
#include "StorageHelperRK.h"
#include "GatewayPlatform.h"
#include "MyPersistentData.h"


MB85RC64 fram(Wire, 0);

// *******************  SysStatus Storage Object **********************
//
// ********************************************************************

sysStatusData *sysStatusData::_instance;

// [static]
sysStatusData &sysStatusData::instance() {
    if (!_instance) {
        _instance = new sysStatusData();
    }
    return *_instance;
}

sysStatusData::sysStatusData() : StorageHelperRK::PersistentDataFRAM(::fram, 0, &sysData.sysHeader, sizeof(SysData), SYS_DATA_MAGIC, SYS_DATA_VERSION) {

};

sysStatusData::~sysStatusData() {
}

void sysStatusData::setup() {
    fram.begin();
    reinitializedThisBoot = false;
    sysStatus
    //    .withLogData(true)
        .withSaveDelayMs(500)
        .load();

    // Log.info("sizeof(SysData): %u", sizeof(SysData));
}

void sysStatusData::loop() {
    sysStatus.flush(false);
}

bool sysStatusData::wasReinitializedThisBoot() const {
    return reinitializedThisBoot;
}

bool sysStatusData::validate(size_t dataSize) {
    bool valid = PersistentDataFRAM::validate(dataSize);
    if (valid) {
        // If test1 < 0 or test1 > 100, then the data is invalid
        if (sysStatus.get_openTime() > 12 || sysStatus.get_closeTime() <12) {
            Log.info("data not valid openTime=%d and closeTime=%d", sysStatus.get_openTime(), sysStatus.get_closeTime());
            valid = false;
        }
        else if (sysStatus.get_frequencyMinutes() <=0 || sysStatus.get_frequencyMinutes() > 60) {
            Log.info("data not valid frequency minutes =%d", sysStatus.get_frequencyMinutes());
            valid = false;
        }
        else if (sysStatus.get_nodeNumber() != 0) {
            Log.info("data not valid node number =%d", sysStatus.get_nodeNumber());
            valid = false;
        }
    }
    if (!valid) Log.info("sysStatus data is %s",(valid) ? "valid": "not valid");
    return valid;
}

void sysStatusData::initialize() {
    PersistentDataFRAM::initialize();
    reinitializedThisBoot = true;

    Log.info("data initialized");

    // Initialize the default value to 10 if the structure is reinitialized.
    // Be careful doing this, because when MyData is extended to add new fields,
    // the initialize method is not called! This is only called when first
    // initialized.
    sysStatus.set_nodeNumber(0);                     // Default for a Gateway
    sysStatus.set_structuresVersion(1);
    sysStatus.set_magicNumber(27617);
    sysStatus.set_connectivityMode(0);
    sysStatus.set_resetCount(0);
    sysStatus.set_messageCount(0);
    sysStatus.set_lastTimeSync(0);
    sysStatus.set_frequencyMinutes(60);
    sysStatus.set_updatedFrequencyMinutes(0);
    sysStatus.set_alertCodeGateway(0);
    sysStatus.set_alertTimestampGateway(0);
    sysStatus.set_openTime(6);
    sysStatus.set_closeTime(22);
    sysStatus.set_verizonSIM(false);

    // If you manually update fields here, be sure to update the hash
    updateHash();
    flush(true);
}

uint8_t sysStatusData::get_nodeNumber() const {
    return getValue<uint8_t>(offsetof(SysData, nodeNumber));
}

void sysStatusData::set_nodeNumber(uint8_t value) {
    setValue<uint8_t>(offsetof(SysData, nodeNumber), value);
}

uint8_t sysStatusData::get_structuresVersion() const {
    return getValue<uint8_t>(offsetof(SysData, structuresVersion));
}

void sysStatusData::set_structuresVersion(uint8_t value) {
    setValue<uint8_t>(offsetof(SysData, structuresVersion), value);
}

uint16_t sysStatusData::get_magicNumber() const {
    return getValue<uint16_t>(offsetof(SysData, magicNumber));
}

void sysStatusData::set_magicNumber(uint16_t value) {
    setValue<uint16_t>(offsetof(SysData, magicNumber), value);
}

uint8_t sysStatusData::get_connectivityMode() const {
    return getValue<uint8_t>(offsetof(SysData, connectivityMode));
}

void sysStatusData::set_connectivityMode(uint8_t value) {
    setValue<uint8_t>(offsetof(SysData, connectivityMode), value);
}

uint8_t sysStatusData::get_resetCount() const {
    return getValue<uint8_t>(offsetof(SysData, resetCount));
}

void sysStatusData::set_resetCount(uint8_t value) {
    setValue<uint8_t>(offsetof(SysData, resetCount), value);
}

uint8_t sysStatusData::get_messageCount() const {
    return getValue<uint8_t>(offsetof(SysData, messageCount));
}

void sysStatusData::set_messageCount(uint8_t value) {
    setValue<uint8_t>(offsetof(SysData, messageCount), value);
}

time_t sysStatusData::get_lastTimeSync() const {
    return getValue<time_t>(offsetof(SysData, lastTimeSync));
}

void sysStatusData::set_lastTimeSync(time_t value) {
    setValue<time_t>(offsetof(SysData, lastTimeSync), value);
}

time_t sysStatusData::get_lastConnection() const {
    return getValue<time_t>(offsetof(SysData, lastConnection));
}

void sysStatusData::set_lastConnection(time_t value) {
    setValue<time_t>(offsetof(SysData, lastConnection), value);
}

uint16_t sysStatusData::get_lastConnectionDuration() const {
    return getValue<uint16_t>(offsetof(SysData,lastConnectionDuration));
}

void sysStatusData::set_lastConnectionDuration(uint16_t value) {
    setValue<uint16_t>(offsetof(SysData,lastConnectionDuration), value);
}

uint16_t sysStatusData::get_frequencyMinutes() const {
    return getValue<uint16_t>(offsetof(SysData,frequencyMinutes));
}

void sysStatusData::set_frequencyMinutes(uint16_t value) {
    setValue<uint16_t>(offsetof(SysData, frequencyMinutes), value);
}

uint16_t sysStatusData::get_updatedFrequencyMinutes() const {
    return getValue<uint16_t>(offsetof(SysData,updatedFrequencyMinutes));
}

void sysStatusData::set_updatedFrequencyMinutes(uint16_t value) {
    setValue<uint16_t>(offsetof(SysData, updatedFrequencyMinutes), value);
}

uint8_t sysStatusData::get_alertCodeGateway() const {
    return getValue<uint8_t>(offsetof(SysData, alertCodeGateway));
}

void sysStatusData::set_alertCodeGateway(uint8_t value) {
    setValue<uint8_t>(offsetof(SysData, alertCodeGateway), value);
}

time_t sysStatusData::get_alertTimestampGateway() const {
    return getValue<time_t>(offsetof(SysData, alertTimestampGateway));
}

void sysStatusData::set_alertTimestampGateway(time_t value) {
    setValue<time_t>(offsetof(SysData, alertTimestampGateway), value);
}

uint8_t sysStatusData::get_openTime() const {
    return getValue<uint8_t>(offsetof(SysData, openTime));
}

void sysStatusData::set_openTime(uint8_t value) {
    setValue<uint8_t>(offsetof(SysData, openTime), value);
}

uint8_t sysStatusData::get_closeTime() const {
    return getValue<uint8_t>(offsetof(SysData, closeTime));
}

void sysStatusData::set_closeTime(uint8_t value) {
    setValue<uint8_t>(offsetof(SysData, closeTime), value);
}

bool sysStatusData::get_verizonSIM() const {
    return getValue<bool>(offsetof(SysData, verizonSIM));
}

void sysStatusData::set_verizonSIM(bool value) {
    setValue<bool>(offsetof(SysData, verizonSIM), value);
}

uint8_t sysStatusData::get_sensorType() const {
    return getValue<uint8_t>(offsetof(SysData, sensorType));
}

void sysStatusData::set_sensorType(uint8_t value) {
    setValue<uint8_t>(offsetof(SysData, sensorType), value);
}

// *****************  Current Status Storage Object *******************
// Offset of 100 bytes - make room for SysStatus
// ********************************************************************

currentStatusData *currentStatusData::_instance;

// [static]
currentStatusData &currentStatusData::instance() {
    if (!_instance) {
        _instance = new currentStatusData();
    }
    return *_instance;
}

currentStatusData::currentStatusData() : StorageHelperRK::PersistentDataFRAM(::fram, 100, &currentData.currentHeader, sizeof(CurrentData), CURRENT_DATA_MAGIC, CURRENT_DATA_VERSION) {
};

currentStatusData::~currentStatusData() {
}

void currentStatusData::setup() {
    fram.begin();
    reinitializedThisBoot = false;

    current
    //    .withLogData(true)
        .withSaveDelayMs(500)
        .load();

    // Log.info("sizeof(CurrentData): %u", sizeof(CurrentData));
}

void currentStatusData::loop() {
    current.flush(false);
}

bool currentStatusData::wasReinitializedThisBoot() const {
    return reinitializedThisBoot;
}

bool currentStatusData::validate(size_t dataSize) {
    bool valid = PersistentDataFRAM::validate(dataSize);
    if (valid) {
        if (current.get_hourlyCount() < 0 || current.get_hourlyCount() > 1024) {
            Log.info("current data not valid hourlyCount=%d" , current.get_hourlyCount());
            valid = false;
        }
    }
    if (!valid) Log.info("current data is %s",(valid) ? "valid": "not valid");
    return valid;
}

void currentStatusData::initialize() {
    PersistentDataFRAM::initialize();
    reinitializedThisBoot = true;

    Log.info("Current Data Initialized");

    currentStatusData::resetEverything();

    // If you manually update fields here, be sure to update the hash
    updateHash();
    flush(true);
}


void currentStatusData::resetEverything() {                             // The device is waking up in a new day or is a new install
    Log.info("Resetting current data defaults");
  current.set_nodeNumber(11);
  current.set_tempNodeNumber(0);
  current.set_nodeID(0);
  current.set_alertCodeNode(0);
  current.set_alertTimestampNode(0);
  current.set_dailyCount(0);                                            // Reset the counts in FRAM as well
  current.set_hourlyCount(0);
  current.set_messageCount(0);
  current.set_successCount(0);
  current.set_lastCountTime(Time.now());
  sysStatus.set_resetCount(0);                                          // Reset the reset count as well
  sysStatus.set_messageCount(0);
}

uint8_t currentStatusData::get_nodeNumber() const {
    return getValue<uint8_t>(offsetof(CurrentData, nodeNumber));
}

void currentStatusData::set_nodeNumber(uint8_t value) {
    setValue<uint8_t>(offsetof(CurrentData, nodeNumber), value);
}

uint8_t currentStatusData::get_tempNodeNumber() const {
    return getValue<uint8_t>(offsetof(CurrentData, tempNodeNumber));
}

void currentStatusData::set_tempNodeNumber(uint8_t value) {
    setValue<uint8_t>(offsetof(CurrentData, tempNodeNumber), value);
}

uint16_t currentStatusData::get_nodeID() const {
    return getValue<uint16_t>(offsetof(CurrentData, nodeID));
}

void currentStatusData::set_nodeID(uint16_t value) {
    setValue<uint16_t>(offsetof(CurrentData, nodeID), value);
}

uint8_t currentStatusData::get_internalTempC() const {
    return getValue<uint8_t>(offsetof(CurrentData, internalTempC));
}

void currentStatusData::set_internalTempC(uint8_t value) {
    setValue<uint8_t>(offsetof(CurrentData, internalTempC), value);
}

double currentStatusData::get_stateOfCharge() const {
    return getValue<double>(offsetof(CurrentData, stateOfCharge));
}

void currentStatusData::set_stateOfCharge(double value) {
    setValue<double>(offsetof(CurrentData, stateOfCharge), value);
}

uint8_t currentStatusData::get_batteryState() const {
    return getValue<uint8_t>(offsetof(CurrentData, batteryState));
}

void currentStatusData::set_batteryState(uint8_t value) {
    setValue<uint8_t>(offsetof(CurrentData, batteryState), value);
}

uint8_t currentStatusData::get_resetCount() const {
    return getValue<uint8_t>(offsetof(CurrentData, resetCount));
}

void currentStatusData::set_resetCount(uint8_t value) {
    setValue<uint8_t>(offsetof(CurrentData, resetCount), value);
}

int16_t currentStatusData::get_RSSI() const {
    return getValue<int16_t>(offsetof(CurrentData, RSSI));
}

void currentStatusData::set_RSSI(int16_t value) {
    setValue<int16_t>(offsetof(CurrentData, RSSI), value);
}

int16_t currentStatusData::get_SNR() const {
    return getValue<int16_t>(offsetof(CurrentData, SNR));
}

void currentStatusData::set_SNR(int16_t value) {
    setValue<int16_t>(offsetof(CurrentData, SNR), value);
}

uint8_t currentStatusData::get_messageCount() const {
    return getValue<uint8_t>(offsetof(CurrentData, messageCount));
}

void currentStatusData::set_messageCount(uint8_t value) {
    setValue<uint8_t>(offsetof(CurrentData, messageCount), value);
}

uint8_t currentStatusData::get_successCount() const {
    return getValue<uint8_t>(offsetof(CurrentData, successCount));
}

void currentStatusData::set_successCount(uint8_t value) {
    setValue<uint8_t>(offsetof(CurrentData, successCount), value);
}

time_t currentStatusData::get_lastCountTime() const {
    return getValue<time_t>(offsetof(CurrentData, lastCountTime));
}

void currentStatusData::set_lastCountTime(time_t value) {
    setValue<time_t>(offsetof(CurrentData, lastCountTime), value);
}

uint16_t currentStatusData::get_hourlyCount() const {
    return getValue<uint16_t>(offsetof(CurrentData, hourlyCount));
}

void currentStatusData::set_hourlyCount(uint16_t value) {
    setValue<uint16_t>(offsetof(CurrentData, hourlyCount), value);
}

uint16_t currentStatusData::get_dailyCount() const {
    return getValue<uint16_t>(offsetof(CurrentData, dailyCount));
}

void currentStatusData::set_dailyCount(uint16_t value) {
    setValue<uint16_t>(offsetof(CurrentData, dailyCount), value);
}

uint8_t currentStatusData::get_alertCodeNode() const {
    return getValue<uint8_t>(offsetof(CurrentData, alertCodeNode));
}

void currentStatusData::set_alertCodeNode(uint8_t value) {
    setValue<uint8_t>(offsetof(CurrentData, alertCodeNode), value);
}

time_t currentStatusData::get_alertTimestampNode() const {
    return getValue<time_t>(offsetof(CurrentData, alertTimestampNode));
}

void currentStatusData::set_alertTimestampNode(time_t value) {
    setValue<time_t>(offsetof(CurrentData, alertTimestampNode), value);
}

bool currentStatusData::get_openHours() const {
    return getValue<bool>(offsetof(CurrentData, openHours));
}

void currentStatusData::set_openHours(bool value) {
    setValue<bool>(offsetof(CurrentData, openHours), value);
}

uint8_t currentStatusData::get_sensorType() const {
    return getValue<uint8_t>(offsetof(CurrentData, sensorType));
}

void currentStatusData::set_sensorType(uint8_t value) {
    setValue<uint8_t>(offsetof(CurrentData, sensorType), value);
}

uint8_t currentStatusData::get_hops() const {
    return getValue<uint8_t>(offsetof(CurrentData, hops));
}

void currentStatusData::set_hops(uint8_t value) {
    setValue<uint8_t>(offsetof(CurrentData, hops), value);
}

uint16_t currentStatusData::get_productVersion() const {
    return getValue<uint16_t>(offsetof(CurrentData, productVersion));
}

void currentStatusData::set_productVersion(uint16_t value) {
    setValue<uint16_t>(offsetof(CurrentData, productVersion), value);
}

// *******************  nodeID Storage Object **********************
//
// ******************** Offset of 200         **********************

namespace {

const char *const EMPTY_NODE_DB_JSON = "{\"nodes\":[]}";
const uint32_t NODE_DB_PERSIST_WARN_MS = 25;
const uint32_t NODE_DB_PERSIST_STRONG_WARN_MS = 50;
const uint32_t NODE_DB_PERSIST_CRITICAL_MS = 100;
const uint32_t NODE_DB_PERSIST_DAY_SECONDS = 86400;
const uint32_t NODE_DB_PERSIST_BYTES_PER_SAVE = sizeof(nodeIDData::NodeData) * 2;

bool bufferIsFilledWith(const uint8_t *buffer, size_t length, uint8_t value) {
    for (size_t index = 0; index < length; index++) {
        if (buffer[index] != value) {
            return false;
        }
    }
    return true;
}

bool nodeDbRegionLooksBlank(const nodeIDData::NodeData &data) {
    const uint8_t *bytes = reinterpret_cast<const uint8_t *>(&data);
    return bufferIsFilledWith(bytes, sizeof(nodeIDData::NodeData), 0xFF) || bufferIsFilledWith(bytes, sizeof(nodeIDData::NodeData), 0x00);
}

}

nodeIDData *nodeIDData::_instance;

// [static]
nodeIDData &nodeIDData::instance() {
    if (!_instance) {
        _instance = new nodeIDData();
    }
    return *_instance;
}

nodeIDData::nodeIDData() : StorageHelperRK::PersistentDataFRAM(::fram, 200, &nodeData.nodeHeader, sizeof(NodeData), NODEID_DATA_MAGIC, NODEID_DATA_VERSION) {

};

nodeIDData::~nodeIDData() {
}

void nodeIDData::setup() {
    fram.begin();

    nodeDatabase
    //    .withLogData(true)
        .withSaveDelayMs(500)
        .load();

    // Log.info("sizeof(NodeData): %u", sizeof(NodeData)); 
}

bool nodeIDData::load() {
    WITH_LOCK(*this) {
        NodeData primaryData;
        memset(&primaryData, 0, sizeof(primaryData));
        const bool primaryReadOk = fram.readData(NODEID_PRIMARY_OFFSET, (uint8_t *)&primaryData.nodeHeader, sizeof(NodeData));

        memcpy(&nodeData, &primaryData, sizeof(nodeData));
        if (primaryReadOk && validate(nodeData.nodeHeader.size)) {
            return true;
        }

        NodeData backupData;
        memset(&backupData, 0, sizeof(backupData));
        const bool backupReadOk = fram.readData(NODEID_BACKUP_OFFSET, (uint8_t *)&backupData.nodeHeader, sizeof(NodeData));

        memcpy(&nodeData, &backupData, sizeof(nodeData));
        if (backupReadOk && validate(nodeData.nodeHeader.size)) {
            Log.warn("NodeDB restored from FRAM backup");
            if (!fram.writeData(NODEID_PRIMARY_OFFSET, (const uint8_t *)&nodeData.nodeHeader, sizeof(NodeData))) {
                Log.error("NodeDB restore failed to rewrite FRAM primary copy");
                sysStatus.set_alertCodeGateway(1);
                sysStatus.flush(true);
            }
            return true;
        }

        if (!primaryReadOk || !backupReadOk) {
            Log.error("NodeDB FRAM read failed: primary=%s backup=%s", primaryReadOk ? "ok" : "failed", backupReadOk ? "ok" : "failed");
        }

        if (primaryReadOk && backupReadOk && nodeDbRegionLooksBlank(primaryData) && nodeDbRegionLooksBlank(backupData)) {
            Log.info("FRAM first boot detected");
        }

        initialize();
    }

    return true;
}

void nodeIDData::save() {
    const system_tick_t saveStart = millis();
    uint32_t mirrorDurationMs = 0;
    WITH_LOCK(*this) {
        const system_tick_t mirrorStart = millis();
        const bool backupOk = fram.writeData(NODEID_BACKUP_OFFSET, (const uint8_t *)&nodeData.nodeHeader, sizeof(NodeData));
        const bool primaryOk = fram.writeData(NODEID_PRIMARY_OFFSET, (const uint8_t *)&nodeData.nodeHeader, sizeof(NodeData));
        mirrorDurationMs = millis() - mirrorStart;

        if (!backupOk || !primaryOk) {
            Log.error("NodeDB save failed: primary=%s backup=%s", primaryOk ? "ok" : "failed", backupOk ? "ok" : "failed");
        }
    }
    const uint32_t totalDurationMs = millis() - saveStart;
    recordPersistSave(mirrorDurationMs, totalDurationMs);
    PersistentDataBase::save();
}

void nodeIDData::loop() {
    maybeLogPersist24h();
}

bool nodeIDData::resetNodeIDs() {
    const bool saved = saveNodeIDJson(EMPTY_NODE_DB_JSON, true);
    if (saved) {
        Log.info("NodeDB reset to empty database");
    }
    return saved;
}

bool nodeIDData::validate(size_t dataSize) {
    bool valid = PersistentDataFRAM::validate(dataSize);
    if (!valid) {
        SYSTEM_VERBOSE_LOG("NodeDB FRAM contents failed validation");
    }
    return valid;
}

void nodeIDData::initialize() {
    Log.info("NodeDB invalid; repairing empty database");
    PersistentDataFRAM::initialize();
    if (!saveNodeIDJson(EMPTY_NODE_DB_JSON, true)) {
        Log.error("NodeDB repair failed while writing empty database");
        sysStatus.set_alertCodeGateway(1);
        sysStatus.flush(true);
        return;
    }

    NodeData primaryVerify;
    NodeData backupVerify;
    memset(&primaryVerify, 0, sizeof(primaryVerify));
    memset(&backupVerify, 0, sizeof(backupVerify));

    const bool primaryReadOk = fram.readData(NODEID_PRIMARY_OFFSET, (uint8_t *)&primaryVerify.nodeHeader, sizeof(NodeData));
    const bool backupReadOk = fram.readData(NODEID_BACKUP_OFFSET, (uint8_t *)&backupVerify.nodeHeader, sizeof(NodeData));

    bool primaryValid = false;
    bool backupValid = false;

    if (primaryReadOk) {
        memcpy(&nodeData, &primaryVerify, sizeof(nodeData));
        primaryValid = validate(nodeData.nodeHeader.size);
    }
    if (backupReadOk) {
        memcpy(&nodeData, &backupVerify, sizeof(nodeData));
        backupValid = validate(nodeData.nodeHeader.size);
    }

    if (!primaryReadOk || !backupReadOk || !primaryValid || !backupValid) {
        Log.error("NodeDB repair verification failed: primaryRead=%s primaryValid=%s backupRead=%s backupValid=%s", primaryReadOk ? "ok" : "failed", primaryValid ? "ok" : "failed", backupReadOk ? "ok" : "failed", backupValid ? "ok" : "failed");
        sysStatus.set_alertCodeGateway(1);
        sysStatus.flush(true);
        return;
    }

    memcpy(&nodeData, &primaryVerify, sizeof(nodeData));
    Log.info("NodeDB repair written and verified");
    Log.info("FRAM init complete");
}


String nodeIDData::get_nodeIDJson() const {
    char buffer[sizeof(NodeData::nodeIDJson) + 1];
    size_t length = 0;
    WITH_LOCK(*this) {
        length = strnlen(nodeData.nodeIDJson, sizeof(nodeData.nodeIDJson));
        memcpy(buffer, nodeData.nodeIDJson, length);
    }
    buffer[length] = 0;
    return String(buffer);
}

size_t nodeIDData::get_nodeIDJsonLength() const {
    size_t length = 0;
    WITH_LOCK(*this) {
        length = strnlen(nodeData.nodeIDJson, sizeof(nodeData.nodeIDJson));
    }
    return length;
}

bool nodeIDData::set_nodeIDJson(const char *str) {
	return setValueString(offsetof(NodeData, nodeIDJson), sizeof(NodeData::nodeIDJson), str);
}

bool nodeIDData::saveNodeIDJson(const char *str, bool force) {
    if (!str) {
        Log.error("NodeDB save rejected: null JSON pointer");
        return false;
    }

    const size_t jsonLength = strlen(str);
    if (jsonLength >= sizeof(NodeData::nodeIDJson)) {
        Log.error("NodeDB save rejected: len=%u max=%u", (unsigned)jsonLength, (unsigned)(sizeof(NodeData::nodeIDJson) - 1));
        return false;
    }

    const bool hadPendingPersist = hasPendingPersist();
    if (!set_nodeIDJson(str)) {
        Log.error("NodeDB save rejected: len=%u", (unsigned)jsonLength);
        return false;
    }

    if (!force) {
        if (!hadPendingPersist && hasPendingPersist()) {
            Log.info("NodeDB marked dirty");
        }
        return true;
    }

    flush(true);
    if (hadPendingPersist || hasPendingPersist() == false) {
        Log.info("NodeDB atomic save complete");
    }
    return true;
}

bool nodeIDData::hasPendingPersist() const {
    bool pending = false;
    WITH_LOCK(*this) {
        pending = (lastUpdate != 0);
    }
    return pending;
}

uint32_t nodeIDData::getDirtySinceMs() const {
    uint32_t dirtySinceMs = 0;
    WITH_LOCK(*this) {
        dirtySinceMs = lastUpdate;
    }
    return dirtySinceMs;
}

bool nodeIDData::persistIfDirty() {
    const NodeDbPersistStats before = getPersistStats();
    if (!hasPendingPersist()) {
        return false;
    }

    Log.info("Persisting deferred NodeDB update");
    flush(true);

    const NodeDbPersistStats after = getPersistStats();
    if (after.saveCount == before.saveCount) {
        return false;
    }

    Log.info("NodeDB atomic save complete");
    if (after.lastMs > NODE_DB_PERSIST_CRITICAL_MS) {
        Log.error("PersistCrit: deferredSave=%ums mirror=%ums", after.lastMs, after.lastMirrorMs);
    }
    return true;
}

NodeDbPersistStats nodeIDData::getPersistStats() const {
    NodeDbPersistStats snapshot;
    WITH_LOCK(*this) {
        snapshot = persistStatsData;
    }
    return snapshot;
}

void nodeIDData::recordPersistSave(uint32_t mirrorDurationMs, uint32_t totalDurationMs) {
    NodeDbPersistStats snapshot;

    WITH_LOCK(*this) {
        persistStatsData.saveCount++;
        persistStatsData.totalMs += totalDurationMs;
        persistStatsData.lastMs = (uint16_t)min(totalDurationMs, (uint32_t)UINT16_MAX);
        persistStatsData.lastMirrorMs = (uint16_t)min(mirrorDurationMs, (uint32_t)UINT16_MAX);
        if (persistStatsData.lastMs > persistStatsData.maxMs) {
            persistStatsData.maxMs = persistStatsData.lastMs;
        }

        if (Time.isValid()) {
            const time_t now = Time.now();
            if (persistDayWindowStart == 0) {
                persistDayWindowStart = now;
            }

            const time_t minuteBucket = now / 60;
            if (persistMinuteBucket != minuteBucket) {
                persistMinuteBucket = minuteBucket;
                persistSavesThisMinute = 0;
            }
            persistSavesThisMinute++;

            persistStatsData.dailySaveCount++;
            persistStatsData.dailyBytesWritten += NODE_DB_PERSIST_BYTES_PER_SAVE;
            if (persistSavesThisMinute > persistStatsData.dailyMaxSavesPerMinute) {
                persistStatsData.dailyMaxSavesPerMinute = persistSavesThisMinute;
            }
        }

        snapshot = persistStatsData;
    }

    if (totalDurationMs > NODE_DB_PERSIST_CRITICAL_MS) {
        Log.error("PersistCrit: save=%lums mirror=%lums", (unsigned long)totalDurationMs, (unsigned long)mirrorDurationMs);
    }
    else if (totalDurationMs > NODE_DB_PERSIST_STRONG_WARN_MS) {
        Log.warn("PersistWarn: save=%lums mirror=%lums", (unsigned long)totalDurationMs, (unsigned long)mirrorDurationMs);
    }
    else if (totalDurationMs > NODE_DB_PERSIST_WARN_MS) {
        Log.warn("Persist: save=%lums mirror=%lums", (unsigned long)totalDurationMs, (unsigned long)mirrorDurationMs);
    }
}

void nodeIDData::maybeLogPersist24h() {
    if (!Time.isValid()) {
        return;
    }

    NodeDbPersistStats snapshot;
    bool shouldLog = false;

    WITH_LOCK(*this) {
        const time_t now = Time.now();
        if (persistDayWindowStart == 0) {
            persistDayWindowStart = now;
            return;
        }
        if ((now - persistDayWindowStart) < (time_t)NODE_DB_PERSIST_DAY_SECONDS) {
            return;
        }

        snapshot = persistStatsData;
        persistDayWindowStart = now;
        persistStatsData.dailySaveCount = 0;
        persistStatsData.dailyBytesWritten = 0;
        persistStatsData.dailyMaxSavesPerMinute = 0;
        persistMinuteBucket = now / 60;
        persistSavesThisMinute = 0;
        shouldLog = true;
    }

    if (shouldLog) {
        Log.info("Persist24h: saves=%lu bytes=%lu maxBurst=%u max=%ums", (unsigned long)snapshot.dailySaveCount, (unsigned long)snapshot.dailyBytesWritten, snapshot.dailyMaxSavesPerMinute, snapshot.maxMs);
    }
}
