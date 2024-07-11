#include <WiFi.h>
#include <EEPROM.h>
#include <WiFiManager.h>
#include <FirebaseESP32.h>
#include <ArduinoJson.h>
#include <time.h>

const int irSensorPin = 26;

#define JSON_CONFIG_ADDRESS 0 
#define JSON_CONFIG_SIZE 512 // EEPROM size should be multiple of 4
#define FIREBASE_HOST "https://arduino-nano-d1d34-default-rtdb.firebaseio.com"
#define FIREBASE_AUTH "GtpFdlGij2yqL6xxUMjWQLONikpN28vHV6OBhNws"

#define THRESHOLD 20

struct TouchPad {
  int pin;
  int value;
  int state = 0, prevState = 0;
  int toggleState = 0;
};

TouchPad touchPads[6];

void touchPadInit(TouchPad *pad, int pin);
void touchPadScan(TouchPad *pad);
void sendDataToFirebase(const char* path, const char* status);
void initializeTime();

FirebaseData firebaseData;
FirebaseAuth auth;
FirebaseConfig config;

char testString[50] = "";
char username[50];
WiFiManager wm;
bool shouldSaveConfig = false;

void saveConfigCallback() {
    Serial.println("Should save config");
    shouldSaveConfig = true;
}

void configModeCallback(WiFiManager *myWiFiManager) {
    Serial.println("Entered Configuration Mode");
    Serial.print("Config SSID: ");
    Serial.println(myWiFiManager->getConfigPortalSSID());
    Serial.print("Config IP Address: ");
    Serial.println(WiFi.softAPIP());
}

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("Starting minimal setup...");

    EEPROM.begin(JSON_CONFIG_SIZE); 
    Serial.println("EEPROM initialized");

    wm.setSaveConfigCallback(saveConfigCallback);
    wm.setAPCallback(configModeCallback);

    WiFiManagerParameter custom_text_box("username", "Username", testString, 50);
    wm.addParameter(&custom_text_box);

    Serial.println("Starting WiFiManager AutoConnect...");
    bool res = wm.autoConnect("ESP32-AP", "password");
    if (!res) {
        Serial.println("Failed to connect to WiFi");
        delay(3000);
        ESP.restart();
    } else {
        Serial.println("Connected to WiFi");
        Serial.print("IP address: ");
        Serial.println(WiFi.localIP());
    }

    strncpy(username, custom_text_box.getValue(), sizeof(username));
    Serial.println(username);

    config.host = FIREBASE_HOST;
    config.signer.tokens.legacy_token = FIREBASE_AUTH;
    Firebase.begin(&config, &auth);
    Firebase.reconnectWiFi(true);
    Serial.println("Firebase initialized");

    pinMode(irSensorPin, INPUT_PULLUP);

    // Initialize touch sensors
    touchPadInit(&touchPads[0], 12);
    touchPadInit(&touchPads[1], 14);
    touchPadInit(&touchPads[2], 27);
    touchPadInit(&touchPads[3], 33);
    touchPadInit(&touchPads[4], 32);
    touchPadInit(&touchPads[5], 13);

    initializeTime();

    Serial.println("Setup complete");
}

void loop() {
    static int lastIrSensorState = -1;
    int irSensorState = digitalRead(irSensorPin);

    if (irSensorState != lastIrSensorState) {
        lastIrSensorState = irSensorState;
        const char* irStatus = (irSensorState == HIGH) ? "box is closed" : "box is opened";
        sendDataToFirebase("/irSensorData", irStatus);
    }

    for (int i = 0; i < 6; i++) {
        touchPadScan(&touchPads[i]);

        if (touchPads[i].toggleState) {
            String path = String("/touchPad") + String(i + 1) + String("/state");
            Serial.println("Touch detected on " + path);
            sendDataToFirebase(path.c_str(), "touch detected");
            touchPads[i].toggleState = 0;
        }
    }
}

void touchPadInit(TouchPad *pad, int pin) {
    pad->pin = pin;
    touchAttachInterrupt(pad->pin, []{}, THRESHOLD); // Initialize touch pin with threshold
}

void touchPadScan(TouchPad *pad) {
    pad->value = touchRead(pad->pin);

    if (pad->value < THRESHOLD) {
        pad->state = 1;
    } else {
        pad->state = 0;
    }

    if (pad->state == 1 && pad->prevState == 0) {
        pad->toggleState = 1;
    }

    pad->prevState = pad->state;
}

void sendDataToFirebase(const char* path, const char* status) {
    time_t now = time(NULL);
    struct tm* timeinfo = localtime(&now);
    char formattedTimestamp[20];
    strftime(formattedTimestamp, sizeof(formattedTimestamp), "%Y-%m-%d %H:%M:%S", timeinfo);
    String fullPath = String(username) + String(path);

    FirebaseJson json;
    json.set("status", status);
    json.set("timestamp", formattedTimestamp);

    if (Firebase.setJSON(firebaseData, fullPath.c_str(), json)) {
        Serial.println("Status and timestamp sent successfully");
    } else {
        Serial.print("Failed to send status and timestamp: ");
        Serial.println(firebaseData.errorReason());
    }
}

void initializeTime() {
    configTime(19800, 0, "pool.ntp.org");  // 19800 is the offset in seconds for IST (UTC+5:30)
    Serial.println("Waiting for NTP time sync");
    while (!time(nullptr)) {
        delay(1000);
        Serial.print(".");
    }
    Serial.println("\nTime synchronized");
}
