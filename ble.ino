#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>

// Удаляем конфигурацию и использование WDT
#define SIM800L_IP5306_VERSION_20200811
#define DUMP_AT_COMMANDS
#define TINY_GSM_DEBUG SerialMon
#include "utilities.h"
#define SerialMon Serial
#define SerialAT  Serial1
#define TINY_GSM_MODEM_SIM800          // Modem is SIM800
#define TINY_GSM_RX_BUFFER      1024   // Set RX buffer to 1Kb
#include <TinyGsmClient.h>
#ifdef DUMP_AT_COMMANDS
#include <StreamDebugger.h>
StreamDebugger debugger(SerialAT, SerialMon);
TinyGsm modem(debugger);
#else
TinyGsm modem(SerialAT);
#endif

#define uS_TO_S_FACTOR 1000000ULL  /* Conversion factor for micro seconds to seconds */
#define TIME_TO_SLEEP  60        /* Time ESP32 will go to sleep (in seconds) */

// Server details
const char server[] = "iot.mit.kh.ua";
const char resource[] = "/devices"; // Endpoint to send data
const int port = 80;

// Your GPRS credentials (leave empty, if missing)
const char apn[]      = ""; // Your APN
const char gprsUser[] = ""; // User
const char gprsPass[] = ""; // Password
const char simPIN[]   = ""; // SIM card PIN code, if any

TinyGsmClient client(modem);

BLEScan* pBLEScan;
int scanTime = 5; // Scan time in seconds

void setupModem()
{
    pinMode(MODEM_PWRKEY, OUTPUT);
    pinMode(MODEM_POWER_ON, OUTPUT);

    // Turn on the Modem power
    digitalWrite(MODEM_POWER_ON, HIGH);

    // Initialize the indicator as an output
    pinMode(LED_GPIO, OUTPUT);
    digitalWrite(LED_GPIO, LED_OFF);
}

void connectToGPRS()
{
    SerialMon.println("Connecting to GPRS...");
    modem.restart();
    modem.gprsConnect(apn, gprsUser, gprsPass);
    SerialMon.println("Connected to GPRS");
}

void sendDataToServer(String deviceName, float temperature, float pressure, uint8_t battery, String macAddress)
{
    String jsonData = "{";
    jsonData += "\"deviceName\":\"" + deviceName + "\",";
    jsonData += "\"comment\":\"На высоком давлении\",";
    jsonData += "\"lat\":49.9935,";
    jsonData += "\"lng\":36.2304,";
    jsonData += "\"temp\":" + String(temperature) + ",";
    jsonData += "\"pressure\":" + String(pressure) + ",";
    jsonData += "\"macAddress\":\"" + macAddress + "\"";
    jsonData += "}";

    SerialMon.println("Sending data to server...");
    if (client.connect(server, port)) {
        client.println("POST " + String(resource) + " HTTP/1.1");
        client.println("Host: " + String(server));
        client.println("Content-Type: application/json");
        client.println("Content-Length: " + String(jsonData.length()));
        client.println();
        client.print(jsonData);

        // Wait for response
        unsigned long timeout = millis();
        while (client.available() == 0) {
            if (millis() - timeout > 5000) {
                SerialMon.println(">>> Client Timeout !");
                client.stop();
                return;
            }
        }

        // Read response from the server
        while (client.available()) {
            String line = client.readStringUntil('\r');
            SerialMon.println(line);
        }
    }
    client.stop();
}

void processManufacturerData(uint8_t* data, size_t length)
{
    float pressure;
    memcpy(&pressure, &data[2], sizeof(float));

    float temperature = (data[7] + (data[8] << 8)) / 100.0;
    uint8_t battery = data[10];
    String macAddress = "00:14:22:01:23:45"; 

    sendDataToServer("LTP_00027", temperature, pressure, battery, macAddress);
}

class MyAdvertisedDeviceCallbacks: public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice advertisedDevice) {
        String macAddress = advertisedDevice.getAddress().toString().c_str();

        if (macAddress == "5c:02:72:97:f2:4d") {
            SerialMon.println("Device found!");

            if (advertisedDevice.haveName()) {
                SerialMon.print("Device Name: ");
                SerialMon.println(advertisedDevice.getName().c_str());
            }

            SerialMon.print("Device MAC Address: ");
            SerialMon.println(macAddress);

            String manufacturerData = advertisedDevice.getManufacturerData();
            if (manufacturerData.length() > 0) {
                SerialMon.println("Manufacturer data received:");
                for (size_t i = 0; i < manufacturerData.length(); ++i) {
                    SerialMon.print("0x");
                    SerialMon.print((int)manufacturerData[i], HEX);
                    SerialMon.print(" ");
                }
                SerialMon.println();

                processManufacturerData((uint8_t*)manufacturerData.c_str(), manufacturerData.length());
            }
        } else {
            SerialMon.println("Device MAC address does not match, ignoring...");
        }
    }
};


void setup() {
    SerialMon.begin(115200);
    SerialAT.begin(115200);

    BLEDevice::init("BLE Scanner");
    pBLEScan = BLEDevice::getScan(); 
    pBLEScan->setActiveScan(true);
    pBLEScan->setInterval(200);
    pBLEScan->setWindow(180);
    pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());

    setupModem();
    connectToGPRS();
}

void loop() {
    SerialMon.println("Starting BLE scan...");
    BLEScanResults scanResults = *pBLEScan->start(scanTime, false);
    SerialMon.println("Scan complete.");
    pBLEScan->clearResults();

    SerialMon.println("Entering deep sleep...");
    esp_deep_sleep(TIME_TO_SLEEP * uS_TO_S_FACTOR);
}
