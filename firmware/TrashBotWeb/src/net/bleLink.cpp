#include "bleLink.h"
#include <Arduino.h>
#include "../config/settings.h"
#include "../core/types.h"
#include "../core/eventLog.h"
#include "webLayer.h"

BleLink bleLink;

#if TRASHBOT_BLE

#include <NimBLEDevice.h>

static NimBLEServer*         bleServer = nullptr;
static NimBLECharacteristic* soundChar = nullptr;
static NimBLECharacteristic* cmdChar   = nullptr;

// Connection changes are noticed in the host task and reported from loop(),
// so the event log's "browser connected" and "phone connected" lines come
// from the same place.
static volatile int8_t pendingConnects = 0;

class ServerCallbacks : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer* /*s*/, NimBLEConnInfo& /*info*/) override {
        pendingConnects++;
        // Keep advertising so a second phone (or the same one, after a
        // drop) can still find the bin.
        NimBLEDevice::startAdvertising();
    }
    void onDisconnect(NimBLEServer* /*s*/, NimBLEConnInfo& /*info*/, int /*reason*/) override {
        pendingConnects--;
        NimBLEDevice::startAdvertising();
    }
};

class CmdCallbacks : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* c, NimBLEConnInfo& info) override {
        NimBLEAttValue v = c->getValue();
        if (v.size() == 0 || v.size() > WS_MAX_FRAME_BYTES) return;
        webLayer.ingest(CLIENT_ID_BLE + info.getConnHandle(), (const char*)v.data(), v.size());
    }
};

void BleLink::begin() {
    NimBLEDevice::init(BLE_DEVICE_NAME);
    NimBLEDevice::setMTU(247);
    NimBLEDevice::setPower(ESP_PWR_LVL_P9);

    bleServer = NimBLEDevice::createServer();
    bleServer->setCallbacks(new ServerCallbacks());

    NimBLEService* svc = bleServer->createService(BLE_SVC_UUID);
    soundChar = svc->createCharacteristic(BLE_SOUND_UUID,
                                          NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
    soundChar->setValue("0;0;");
    cmdChar = svc->createCharacteristic(BLE_CMD_UUID,
                                        NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
    cmdChar->setCallbacks(new CmdCallbacks());
    bleServer->start();                                  // registers the GATT table

    NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
    adv->addServiceUUID(BLE_SVC_UUID);
    adv->setName(BLE_DEVICE_NAME);
    adv->enableScanResponse(true);
    ready_ = adv->start();

    eventLog.push("BOOT", ready_ ? "BLE advertising as '%s'" : "BLE failed to start (%s)", BLE_DEVICE_NAME);
}

void BleLink::tick(uint32_t /*now*/) {
    while (pendingConnects > 0) {
        pendingConnects--;
        eventLog.push("USER_CONNECTED", "phone via Bluetooth (%u BLE link%s)",
                      clientCount(), clientCount() == 1 ? "" : "s");
    }
    while (pendingConnects < 0) {
        pendingConnects++;
        eventLog.push("USER_DISCONNECTED", "Bluetooth phone gone");
    }
}

void BleLink::sendSound(const SoundMsg& m) {
    if (!ready_ || !soundChar) return;
    char buf[16 + SND_NAME_MAX];
    int n = snprintf(buf, sizeof(buf), "%lu;%u;%s", (unsigned long)m.seq, m.event, m.clip);
    if (n <= 0) return;
    soundChar->setValue((const uint8_t*)buf, (size_t)n);
    if (clientCount()) soundChar->notify();
}

uint8_t BleLink::clientCount() const {
    return bleServer ? (uint8_t)bleServer->getConnectedCount() : 0;
}

bool BleLink::enabled() const { return true; }

#else   // TRASHBOT_BLE disabled: same interface, no radio.

void BleLink::begin() {
    eventLog.push("BOOT", "BLE link compiled out (TRASHBOT_BLE 0)");
}
void    BleLink::tick(uint32_t) {}
void    BleLink::sendSound(const SoundMsg&) {}
uint8_t BleLink::clientCount() const { return 0; }
bool    BleLink::enabled() const { return false; }

#endif
