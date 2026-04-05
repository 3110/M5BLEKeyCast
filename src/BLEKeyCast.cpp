#include "BLEKeyCast.hpp"

// --- Constants ---

static const char* TAG = "BLEKeyCast";

static constexpr uint8_t MAX_LED_BRIGHTNESS_PCT = 10;

static const rgb_color_t COLOR_OFF = {0, 0, 0};
static const rgb_color_t COLOR_BLE_CONNECTED = {0, 0, 255};
static const rgb_color_t COLOR_RECV_FLASH = {0, 255, 0};
static const rgb_color_t COLOR_CONFIG_MODE = {255, 255, 0};

// --- Utility ---

struct MacAddressStr
{
    char str[ESP_NOW_ETH_ALEN * 3];
};

static MacAddressStr macToStr(const uint8_t* mac) {
    MacAddressStr result;
    snprintf(result.str, sizeof(result.str), "%02x:%02x:%02x:%02x:%02x:%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return result;
}

static void logEspErr(const char* context, esp_err_t err) {
    if (err == ESP_OK) {
        ESP_LOGD(TAG, "%s: OK", context);
    } else {
        ESP_LOGE(TAG, "%s: %s", context, esp_err_to_name(err));
    }
}

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

// --- Wi-Fi ---

bool BLEKeyCast::setWifiChannel(uint8_t ch) {
    if (ch < 1 || ch > 13) {
        ESP_LOGE(TAG, "Invalid channel: %u", ch);
        return false;
    }

    esp_err_t err = esp_wifi_set_promiscuous(true);
    if (err != ESP_OK) {
        logEspErr("set_promiscuous(true)", err);
        return false;
    }

    err = esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);

    const esp_err_t err2 = esp_wifi_set_promiscuous(false);
    if (err2 != ESP_OK) {
        logEspErr("set_promiscuous(false)", err2);
    }

    if (err != ESP_OK) {
        logEspErr("set_channel", err);
        return false;
    }

    return true;
}

void BLEKeyCast::logCurrentWifiChannel() {
    uint8_t primary = 0;
    wifi_second_chan_t second = WIFI_SECOND_CHAN_NONE;
    const esp_err_t err = esp_wifi_get_channel(&primary, &second);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Wi-Fi Channel: %u", primary);
    } else {
        logEspErr("get_channel", err);
    }
}

// --- ESP-NOW ---

void BLEKeyCast::initEspNow() {
    if (esp_now_init() == ESP_OK) {
        ESP_LOGI(TAG, "ESP-NOW initialized");
    } else {
        ESP_LOGE(TAG, "ESP-NOW init failed, restarting...");
        ESP.restart();
    }
}

void BLEKeyCast::initBroadcastPeer(uint8_t ch) {
    memset(&(this->_broadcastPeer), 0, sizeof(this->_broadcastPeer));
    memset(this->_broadcastPeer.peer_addr, 0xFF, ESP_NOW_ETH_ALEN);
    this->_broadcastPeer.channel = ch;
    this->_broadcastPeer.encrypt = false;
}

bool BLEKeyCast::addPeer(const esp_now_peer_info_t& peer) {
    if (esp_now_is_peer_exist(peer.peer_addr)) {
        ESP_LOGW(TAG, "Peer already exists");
        return false;
    }
    const esp_err_t err = esp_now_add_peer(&peer);
    logEspErr("add_peer", err);
    return err == ESP_OK;
}

bool BLEKeyCast::sendData(const esp_now_peer_info_t& peer, const uint8_t* data,
                          size_t len) {
    ESP_LOGD(TAG, "ESP-NOW Send: '%c'(0x%02X)", *data, *data);
    const esp_err_t err = esp_now_send(peer.peer_addr, data, len);
    logEspErr("esp_now_send", err);
    return err == ESP_OK;
}

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

    pinMode(RGB_LED_PIN, OUTPUT);
    Serial.begin(SERIAL_BAUDRATE);

    ESP_LOGI(TAG, "== BLEKeyCast %s ==", VERSION);

    this->_btnA.begin();
    this->_btnA.read();
    if (this->_btnA.isPressed()) {
        enterConfigMode();
    }

    this->_bleKeyboard.begin();

    uint8_t baseMac[ESP_NOW_ETH_ALEN];
    esp_read_mac(baseMac, ESP_MAC_WIFI_STA);
    ESP_LOGI(TAG, "STA MAC: %s", macToStr(baseMac).str);

    WiFi.mode(WIFI_STA);
    WiFi.disconnect(false, true);
    delay(100);

    if (!setWifiChannel(CAST_KEY_CHANNEL)) {
        ESP_LOGE(TAG, "Failed to set Wi-Fi channel");
    }
    logCurrentWifiChannel();

    initEspNow();
    initBroadcastPeer(CAST_KEY_CHANNEL);
    addPeer(this->_broadcastPeer);
    esp_now_register_send_cb(onDataSendCb);
    esp_now_register_recv_cb(onDataRecvCb);

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
        sendData(this->_broadcastPeer, &(this->_key), sizeof(this->_key));
        sendBleKey(this->_key);
    }
    delay(10);
}
