#pragma once

#include <BleKeyboard.h>  // https://github.com/T-vK/ESP32-BLE-Keyboard
#include <EspNowManager.h>
#include <JC_Button.h>
#include <Preferences.h>

#include <string>

struct rgb_color_t
{
    uint8_t R;
    uint8_t G;
    uint8_t B;
} __attribute__((packed));

class BLEKeyCast {
public:
    static constexpr const char* VERSION = "v0.0.3";
    BLEKeyCast();
    void begin();
    void update();

private:
    static constexpr const char* PREF_NAMESPACE = "cast-key";
    static constexpr const char* PREF_KEY = "key";
    static constexpr const char* PREF_DEVICE_NAME = "name";
    static constexpr uint8_t DEVICE_NAME_MAX_LEN = 20;

    BleKeyboard _bleKeyboard;
    EspNowManager _espNowManager;

    Button _btnA;
    uint8_t _key;
    bool _bleConnected;
    volatile bool _recvPending;
    volatile uint8_t _recvKey;

    // LED
    void fillLED(const rgb_color_t& color, uint8_t brightness = 100);
    void flashLED(const rgb_color_t& color, uint8_t count = 2,
                  uint16_t onMs = 60, uint16_t offMs = 40);
    void updateLEDForState();

    // BLE
    void sendBleKey(uint8_t k);

    // Preferences / Config
    static std::string loadDeviceName();
    void saveDeviceName(const std::string& name);
    uint8_t loadKey();
    void saveKey(uint8_t k);
    void enterConfigMode();

    // ESP-NOW callbacks
    static BLEKeyCast* _instance;
    static void onDataSendCb(const uint8_t* mac, esp_now_send_status_t status);
    static void onDataRecvCb(const uint8_t* mac, const uint8_t* data,
                             int dataLen);
};