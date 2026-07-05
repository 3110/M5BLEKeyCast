#include "BLEKeyCast.hpp"

// --- Constants ---

static const char* TAG = "BLEKeyCast";

static constexpr uint8_t MAX_LED_BRIGHTNESS_PCT = 10;

static const rgb_color_t COLOR_OFF = {0, 0, 0};
static const rgb_color_t COLOR_BLE_CONNECTED = {0, 0, 255};
static const rgb_color_t COLOR_RECV_FLASH = {0, 255, 0};
static const rgb_color_t COLOR_CONFIG_MODE = {255, 255, 0};

// --- Static member ---

BLEKeyCast* BLEKeyCast::_instance = nullptr;

// --- Constructor ---

BLEKeyCast::BLEKeyCast()
    : _bleKeyboard(loadDeviceName(), CAST_KEY_DEVICE_MANUFACTURER),
      _btnA(BUTTON_PIN),
      _key(0),
      _bleConnected(false),
      _recvPending(false),
      _recvKey(0) {
}

// --- LED ---

void BLEKeyCast::fillLED(const rgb_color_t& color, uint8_t brightness) {
    const float scale = brightness * MAX_LED_BRIGHTNESS_PCT / (100.0f * 255.0f);
    for (size_t i = 0; i < NUM_RGB_LEDS; ++i) {
        neopixelWrite(RGB_LED_PIN, color.R * scale, color.G * scale,
                      color.B * scale);
        delayMicroseconds(50);
    }
    delay(1);
}

void BLEKeyCast::flashLED(const rgb_color_t& color, uint8_t count,
                          uint16_t onMs, uint16_t offMs) {
    for (uint8_t i = 0; i < count; ++i) {
        fillLED(color);
        delay(onMs);
        fillLED(COLOR_OFF);
        delay(offMs);
    }
}

void BLEKeyCast::updateLEDForState() {
    fillLED(this->_bleConnected ? COLOR_BLE_CONNECTED : COLOR_OFF);
}

// --- BLE ---

void BLEKeyCast::sendBleKey(uint8_t k) {
    if (this->_bleKeyboard.isConnected()) {
        this->_bleKeyboard.write(k);
        ESP_LOGD(TAG, "BLE Key Sent: '%c'(0x%02X)", k, k);
    }
}

// --- ESP-NOW callbacks ---

void BLEKeyCast::onDataSendCb(const uint8_t* mac,
                              esp_now_send_status_t status) {
    ESP_LOGD(TAG, "Sent to %s: %s", macToStr(mac).str,
             status == ESP_NOW_SEND_SUCCESS ? "OK" : "FAIL");
}

void BLEKeyCast::onDataRecvCb(const uint8_t* mac, const uint8_t* data,
                              int dataLen) {
    if (!_instance)
        return;
    if (dataLen == 1) {
        ESP_LOGD(TAG, "Recv from %s: '%c'(0x%02X)", macToStr(mac).str, *data,
                 *data);
        _instance->_recvKey = *data;
        _instance->_recvPending = true;
    } else {
        ESP_LOGE(TAG, "Recv from %s: invalid (len=%d)", macToStr(mac).str,
                 dataLen);
    }
}

// --- Preferences / Config ---

std::string BLEKeyCast::loadDeviceName() {
    Preferences prefs;
    prefs.begin(PREF_NAMESPACE, true);
    const std::string name = prefs.isKey(PREF_DEVICE_NAME)
                                 ? prefs.getString(PREF_DEVICE_NAME).c_str()
                                 : CAST_KEY_DEVICE_NAME;
    prefs.end();
    return name;
}

void BLEKeyCast::saveDeviceName(const std::string& name) {
    Preferences prefs;
    prefs.begin(PREF_NAMESPACE);
    prefs.putString(PREF_DEVICE_NAME, name.c_str());
    prefs.end();
}

uint8_t BLEKeyCast::loadKey() {
    Preferences prefs;
    prefs.begin(PREF_NAMESPACE, true);
    const uint8_t k =
        prefs.isKey(PREF_KEY) ? prefs.getUChar(PREF_KEY) : CAST_KEY_SEND_KEY;
    prefs.end();
    return k;
}

void BLEKeyCast::saveKey(uint8_t k) {
    Preferences prefs;
    prefs.begin(PREF_NAMESPACE);
    prefs.putUChar(PREF_KEY, k);
    prefs.end();
}

void BLEKeyCast::enterConfigMode() {
    ESP_LOGI(TAG, "=== Config Mode ===");
    fillLED(COLOR_CONFIG_MODE);

    while (true) {
        const std::string currentName = loadDeviceName();
        const uint8_t currentKey = loadKey();

        Serial.printf("[1] Set key  (current: '%c')", currentKey);
        Serial.println();
        Serial.printf("[2] Set name (current: '%s')", currentName.c_str());
        Serial.println();
        Serial.println("[0] Exit");
        Serial.print("Enter choice: ");

        while (!Serial.available()) {
            delay(10);
        }
        const uint8_t choice = Serial.read();

        if (choice == '1') {
            Serial.write(choice);
            Serial.println();
            Serial.print("Enter a key character: ");
            while (!Serial.available()) {
                delay(10);
            }
            const uint8_t k = Serial.read();
            if (k >= 0x20 && k <= 0x7E) {
                Serial.write(k);
                Serial.println();
                saveKey(k);
                ESP_LOGI(TAG, "Key saved: '%c'(0x%02X)", k, k);
            } else {
                ESP_LOGW(TAG, "Invalid input, cancelled");
            }
        } else if (choice == '2') {
            Serial.write(choice);
            Serial.println();
            Serial.printf("Enter device name (max %d chars, press Enter): ",
                          DEVICE_NAME_MAX_LEN);
            std::string name = "";
            while (true) {
                if (Serial.available()) {
                    const char ch = (char)Serial.read();
                    if (ch == '\n' || ch == '\r') {
                        if (Serial.peek() == '\n') {
                            Serial.read();
                        }
                        Serial.println();
                        if (!name.empty()) {
                            saveDeviceName(name);
                            ESP_LOGI(TAG, "Name saved: '%s'", name.c_str());
                        }
                        break;
                    } else if (ch >= 0x20 && ch <= 0x7E &&
                               name.length() < DEVICE_NAME_MAX_LEN) {
                        Serial.write(ch);
                        name += ch;
                    }
                }
                delay(10);
            }
        } else if (choice == '0') {
            Serial.write(choice);
            Serial.println();
            fillLED(COLOR_OFF);
            ESP_LOGI(TAG, "Exiting config mode, restarting...");
            delay(500);
            ESP.restart();
        }
    }
}

// --- begin / update ---

void BLEKeyCast::begin() {
    _instance = this;

    esp_log_level_set("*", static_cast<esp_log_level_t>(LOG_LOCAL_LEVEL));

    pinMode(RGB_LED_PIN, OUTPUT);
    Serial.begin(SERIAL_BAUDRATE);

    ESP_LOGI(TAG, "== BLEKeyCast %s ==", VERSION);

    this->_btnA.begin();
    this->_btnA.read();
    if (this->_btnA.isPressed()) {
        enterConfigMode();
    }

    this->_bleKeyboard.begin();

    if (!this->_espNowManager.begin()) {
        ESP_LOGE(TAG, "Failed to initialize ESP-NOW");
        return;
    }
    this->_espNowManager.registerCallback(onDataSendCb);
    this->_espNowManager.registerCallback(onDataRecvCb);

    this->_key = loadKey();

    ESP_LOGI(TAG, "Device: %s (%s)", loadDeviceName().c_str(),
             CAST_KEY_DEVICE_MANUFACTURER);
    ESP_LOGI(TAG, "Key: '%c'(0x%02X), Channel: %d", this->_key, this->_key,
             CAST_KEY_CHANNEL);

    fillLED(COLOR_OFF);
}

void BLEKeyCast::update() {
    const bool connected = this->_bleKeyboard.isConnected();
    if (connected != this->_bleConnected) {
        this->_bleConnected = connected;
        ESP_LOGI(TAG, "BLE %s", connected ? "Connected" : "Disconnected");
        updateLEDForState();
    }

    if (this->_recvPending) {
        const uint8_t k = this->_recvKey;
        this->_recvPending = false;
        sendBleKey(k);
        flashLED(COLOR_RECV_FLASH);
        updateLEDForState();
    }

    this->_btnA.read();
    if (this->_btnA.wasPressed()) {
        this->_espNowManager.broadcast(&(this->_key), sizeof(this->_key));
        sendBleKey(this->_key);
    }
    delay(10);
}
