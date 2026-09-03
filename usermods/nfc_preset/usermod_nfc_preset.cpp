#include "wled.h"
#include <Adafruit_PN532.h>
#include "NfcCatalog.h"
#include "nfc_wled.h"        // generated from nfc-schema: nfc::wled::catalog() + preset config

// Tap an NFC tag holding a https://sacredimagination.co URL -> apply a preset for a few
// seconds -> fall back to another preset. The tag's ?c=1..5 query parameter selects which
// preset; tag detection + URL matching live in the shared nfc-schema reader.

static constexpr uint8_t NFC_COLORS = nfc::wled::PRESET_COUNT;  // preset slots (from nfc-schema)

#define NFC_FIRST_PAGE     4     // NTAG/Ultralight user memory starts here
#define NFC_MAX_TAG_BYTES 64     // read cap; a typical tag decodes after ~32
#define NFC_FIELD_TIMEOUT 80     // ms the PN532 holds its field open looking for a tag
#define NFC_TASK_STACK  3072

class NfcPresetUsermod : public Usermod {
  private:
    bool     enabled      = false;
    int8_t   rxPin        = -1;
    int8_t   txPin        = -1;
    uint8_t  colorPreset[NFC_COLORS] = {1, 2, 3, 4, 5};
    uint8_t  revertPreset = 0;      // 0 = stay on the tapped preset
    uint16_t holdSeconds  = 5;
    uint16_t pollMs       = 200;

    Adafruit_PN532* pn532 = nullptr;
    TaskHandle_t    task  = nullptr;
    QueueHandle_t   taps  = nullptr;

    volatile bool taskRunning = false;
    volatile bool taskStop    = false;
    volatile bool probed      = false;   // the task has finished looking for the PN532
    volatile bool readerFound = false;
    volatile uint32_t tapCount = 0, missCount = 0;

    bool initDone = false;

    uint8_t lastUid[7] = {0};       // reader task only: fire once per tap, not per poll
    uint8_t lastUidLen = 0;
    bool    armed      = true;

    uint8_t       tapActive = 0;    // preset currently held by a tap, 0 = none
    bool          tapLanded = false;
    unsigned long revertAt  = 0;
    uint8_t       lastColor = 0;

    static const char _name[];
    static const char _enabled[];

    static void taskWrapper(void* arg) { static_cast<NfcPresetUsermod*>(arg)->readerTask(); }

    bool readPage(uint8_t page, uint8_t* out) {
      for (uint8_t attempt = 0; attempt < 2; attempt++) {
        if (pn532->mifareultralight_ReadPage(page, out)) return true;
        delay(5);
      }
      return false;
    }

    // Read pages until the message parses rather than to a fixed size: a short URL decodes
    // in ~8 exchanges instead of 16, and any tag capacity works without a config knob.
    bool readTag(uint8_t& color) {
      uint8_t tag[NFC_MAX_TAG_BYTES];
      for (uint8_t page = 0; page < NFC_MAX_TAG_BYTES / 4; page++) {
        if (!readPage(NFC_FIRST_PAGE + page, tag + page * 4)) return false;
        nfc::PollResult r;
        if (nfc::wled::catalog().decodeTag(tag, (size_t)(page + 1) * 4, r)) {
          const uint8_t c = r.fields.u8("c", 0);
          color = (c >= nfc::wled::PRESET_SELECT_MIN && c <= nfc::wled::PRESET_SELECT_MAX)
                    ? c : nfc::wled::PRESET_SELECT_DEFAULT;
          return true;
        }
      }
      return false;
    }

    void pollOnce() {
      while (Serial1.available()) Serial1.read();   // drop anything stale on the bus

      uint8_t uid[7], uidLen = 0;
      if (!pn532->readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLen, NFC_FIELD_TIMEOUT)) {
        armed = true;                               // field empty: re-arm for the next tap
        return;
      }
      if (!armed && uidLen == lastUidLen && memcmp(uid, lastUid, uidLen) == 0) return;

      uint8_t color;
      if (!readTag(color)) { missCount++; return; }

      armed = false;
      lastUidLen = uidLen;
      memcpy(lastUid, uid, uidLen);
      xQueueSend(taps, &color, 0);
      tapCount++;
    }

    void readerTask() {
      taskRunning = true;

      Serial1.setPins(rxPin, txPin);                // probing here, not in setup(), so a
      pn532->begin();                               // missing reader can't delay WLED boot
      readerFound = pn532->getFirmwareVersion() != 0;
      if (readerFound) pn532->SAMConfig();
      probed = true;

      while (readerFound && !taskStop) {
        pollOnce();
        vTaskDelay(pdMS_TO_TICKS(pollMs));
      }

      taskRunning = false;    // stopReader() owns the handle; it clears it once we are out
      vTaskDelete(NULL);
    }

    void startReader() {
      if (!enabled || rxPin < 0 || txPin < 0) return;

      const managed_pin_type pins[] = {{rxPin, false}, {txPin, true}};
      if (!PinManager::allocateMultiplePins(pins, 2, PinOwner::UM_NFC)) {
        DEBUG_PRINTLN(F("NFC: pin allocation failed."));
        return;
      }

      taps  = xQueueCreate(4, sizeof(uint8_t));
      pn532 = new Adafruit_PN532(255, &Serial1);
      armed = true;
      lastUidLen = 0;

      xTaskCreatePinnedToCore(taskWrapper, "nfc", NFC_TASK_STACK, this, 1, &task, 0);
    }

    void stopReader() {
      if (task) {
        taskStop = true;
        unsigned long deadline = millis() + 2000;
        while (taskRunning && millis() < deadline) delay(10);
        taskStop = false;
      }
      task = nullptr;
      if (pn532) { Serial1.end(); delete pn532; pn532 = nullptr; }
      if (taps)  { vQueueDelete(taps); taps = nullptr; }
      PinManager::deallocatePin(rxPin, PinOwner::UM_NFC);
      PinManager::deallocatePin(txPin, PinOwner::UM_NFC);
      probed = readerFound = false;
    }

    void applyTap(uint8_t c) {
      if (c < 1 || c > NFC_COLORS) return;
      uint8_t p = colorPreset[c - 1];
      if (p == 0 || p > 250) return;
      lastColor = c;
      applyPreset(p, CALL_MODE_BUTTON_PRESET);
      tapActive = (holdSeconds && revertPreset) ? p : 0;
      tapLanded = false;
      revertAt  = millis() + (unsigned long)holdSeconds * 1000;
    }

  public:

    void setup() override {
      startReader();
      initDone = true;
    }

    void loop() override {
      if (!initDone || strip.isUpdating()) return;

      if (tapActive) {
        if (!tapLanded) {
          if (currentPreset == tapActive) tapLanded = true;
        } else if (currentPreset != tapActive) {
          tapActive = 0;                            // taken over from the app, drop the revert
        } else if ((int32_t)(millis() - revertAt) >= 0) {
          tapActive = 0;
          applyPreset(revertPreset, CALL_MODE_BUTTON_PRESET);
        }
      }

      uint8_t c;
      if (taps && xQueueReceive(taps, &c, 0) == pdTRUE) applyTap(c);
    }

    void addToJsonInfo(JsonObject& root) override {
      if (!enabled) return;
      JsonObject user = root["u"];
      if (user.isNull()) user = root.createNestedObject("u");

      JsonArray status = user.createNestedArray(F("NFC reader"));
      if (!probed)           status.add(F("starting"));
      else if (!readerFound) status.add(F("not found"));
      else { status.add((uint32_t)tapCount); status.add(F(" taps")); }

      if (lastColor) {
        JsonArray last = user.createNestedArray(F("NFC last tag"));
        last.add(lastColor);
        last.add(F(" (c)"));
      }
      if (missCount) user.createNestedArray(F("NFC unknown tags")).add((uint32_t)missCount);
    }

    void addToConfig(JsonObject& root) override {
      JsonObject top = root.createNestedObject(FPSTR(_name));
      top[FPSTR(_enabled)] = enabled;
      JsonArray pins = top.createNestedArray("pin");
      pins.add(rxPin);
      pins.add(txPin);
      JsonArray presets = top.createNestedArray(F("presets"));
      for (uint8_t i = 0; i < NFC_COLORS; i++) presets.add(colorPreset[i]);
      top[F("revert")] = revertPreset;
      top[F("hold")]   = holdSeconds;
      top[F("poll")]   = pollMs;
    }

    bool readFromConfig(JsonObject& root) override {
      JsonObject top = root[FPSTR(_name)];
      if (top.isNull()) {
        DEBUG_PRINT(FPSTR(_name));
        DEBUG_PRINTLN(F(": no config found. (Using defaults.)"));
        return false;
      }

      int8_t oldRx = rxPin, oldTx = txPin;
      bool   oldEnabled = enabled;

      getJsonValue(top[FPSTR(_enabled)], enabled);
      getJsonValue(top["pin"][0], rxPin);
      getJsonValue(top["pin"][1], txPin);
      for (uint8_t i = 0; i < NFC_COLORS; i++) getJsonValue(top[F("presets")][i], colorPreset[i]);
      getJsonValue(top[F("revert")], revertPreset);
      getJsonValue(top[F("hold")], holdSeconds);
      getJsonValue(top[F("poll")], pollMs);

      // preset/timing changes take effect immediately; only re-init the hardware if it moved
      if (initDone && (rxPin != oldRx || txPin != oldTx || enabled != oldEnabled)) {
        int8_t newRx = rxPin, newTx = txPin;
        rxPin = oldRx; txPin = oldTx;   // stopReader() must free the pins it actually took
        stopReader();
        rxPin = newRx; txPin = newTx;
        startReader();
      }
      return true;
    }

    void appendConfigData() override {
      oappend(F("addInfo('NFC:pin[]',0,'','RX &larr; PN532 TX');"));
      oappend(F("addInfo('NFC:pin[]',1,'','TX &rarr; PN532 RX');"));
      oappend(F("addInfo('NFC:presets[]',0,'','preset for ?c=1');"));
      oappend(F("addInfo('NFC:presets[]',1,'','preset for ?c=2');"));
      oappend(F("addInfo('NFC:presets[]',2,'','preset for ?c=3');"));
      oappend(F("addInfo('NFC:presets[]',3,'','preset for ?c=4');"));
      oappend(F("addInfo('NFC:presets[]',4,'','preset for ?c=5');"));
      oappend(F("addInfo('NFC:revert',1,'','preset to return to (0 = stay)');"));
      oappend(F("addInfo('NFC:hold',1,'','seconds');"));
      oappend(F("addInfo('NFC:poll',1,'','ms between polls');"));
    }

    uint16_t getId() override { return USERMOD_ID_NFC; }
};

const char NfcPresetUsermod::_name[]    PROGMEM = "NFC";
const char NfcPresetUsermod::_enabled[] PROGMEM = "enabled";

static NfcPresetUsermod nfc_preset;
REGISTER_USERMOD(nfc_preset);
