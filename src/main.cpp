#include <Arduino.h>
#include <TFT_eSPI.h>
#include "touch_daxs15231b.h"
#include <WiFi.h>
#include <PubSubClient.h>
#include <vector>
#include <Adafruit_AHTX0.h>
#include <Wire.h>
#include "config.h"

TFT_eSPI tft = TFT_eSPI();
DAXS15231BTouch ts;
WiFiClient espClient;
PubSubClient mqttClient(espClient);
Adafruit_AHTX0 aht10;
float lastTemp = NAN, lastHum = NAN;
unsigned long lastAHT10Read = 0;

struct Zone {
    String name;
    bool isOpen;
    int x, y, w, h;
    int number;  // Add zone number to track which zone this is
};

// Forward declaration of drawAllZones
void drawAllZones();

// Change from fixed size to dynamic vector
std::vector<Zone> zones;  // Start empty, will grow as zones are discovered

// Modern UI colors - updated palette with consistent button borders
#define COLOR_BG         0x0000    // Black background
#define COLOR_BG_GRAD    0x0841    // Dark blue-gray for gradient
#define COLOR_HEADER_BG  0x10A2    // Darker teal for header
#define COLOR_ACTIVE     0x07FF    // Bright cyan for active zones
#define COLOR_INACTIVE   0x4A69    // Brighter blue for inactive zones
#define COLOR_TEXT       0xFFFF    // White text
#define COLOR_TEXT_DIM   0x8410    // Dimmed text for inactive state
#define COLOR_CARD_BG    0x1082    // Brighter blue-gray for card background
#define COLOR_BORDER_ON  0x07FF    // Bright cyan border for active state
#define COLOR_BORDER_OFF 0x03EF    // Dimmer cyan border for inactive state
#define COLOR_CARD_HOVER 0x3186    // Brighter hover state
#define COLOR_SHADOW     0x0000    // Black for shadows
#define COLOR_DOT_ON  0x07E0  // Green
#define COLOR_DOT_OFF 0xF800  // Red

// UI Layout constants - refined
const int HEADER_HEIGHT = 48;      // Taller header
const int HEADER_PADDING = 16;     // More padding in header
const int CARD_PADDING = 6;        // Slightly more padding
const int CARD_CORNER = 16;        // More rounded corners
const int STATUS_DOT_SIZE = 10;    // Larger status dot
const int STATUS_DOT_PADDING = 8;  // More padding around dot
const int CARD_SHADOW = 2;         // Shadow offset
const int TEXT_PADDING = 12;       // Padding for text

void addZone(int zoneNumber, const String& name = "", bool isOpen = false) {
    // Check if zone already exists by number
    for (auto& zone : zones) {
        if (zone.number == zoneNumber) {
            // Update existing zone's name if provided
            if (!name.isEmpty()) {
                zone.name = name;
            }
            return; // Zone already exists
        }
    }
    
    // Add new zone
    String zoneName = name.isEmpty() ? "Zone " + String(zoneNumber) : name;
    zones.push_back({zoneName, isOpen, 0, 0, 0, 0, zoneNumber});  // Add zoneNumber to constructor
    
    // Redraw the UI with new layout
    drawAllZones();
    Serial.printf("Added zone %d: %s\n", zoneNumber, zoneName.c_str());
}

void debugAllZones(const char *who) {
    for (int i = 0; i < zones.size(); ++i) {
        int row = i / 2;  // 2 columns
        int col = i % 2;
        
        // Calculate card position with padding (same as in drawAllZones)
        const int cardW = (tft.width() - (3 * CARD_PADDING)) / 2;
        const int cardH = (tft.height() - HEADER_HEIGHT - (9 * CARD_PADDING)) / 8;
        int x = CARD_PADDING + col * (cardW + CARD_PADDING);
        int y = HEADER_HEIGHT + CARD_PADDING + row * (cardH + CARD_PADDING);
        
        Serial.printf("%s - Zone %d: x=%d y=%d w=%d h=%d\n", who, i, x, y, cardW, cardH);
    }
}

void drawGradientBackground() {
    // Draw a subtle gradient background
    for (int y = 0; y < tft.height(); y++) {
        uint16_t color = tft.color565(
            map(y, 0, tft.height(), 0, 8),    // R
            map(y, 0, tft.height(), 0, 16),   // G
            map(y, 0, tft.height(), 0, 24)    // B
        );
        tft.drawFastHLine(0, y, tft.width(), color);
    }
}

void drawHeader() {
    // Draw header background with gradient
    for (int y = 0; y < HEADER_HEIGHT; y++) {
        uint16_t color = tft.color565(
            map(y, 0, HEADER_HEIGHT, 0, 16),    // R
            map(y, 0, HEADER_HEIGHT, 32, 48),   // G
            map(y, 0, HEADER_HEIGHT, 32, 48)    // B
        );
        tft.drawFastHLine(0, y, tft.width(), color);
    }
    
    // Draw title with modern styling
    tft.setTextDatum(TL_DATUM);
    
    // Draw shadow
    tft.setTextColor(COLOR_SHADOW, COLOR_HEADER_BG);
    tft.drawString("Zone Controller", HEADER_PADDING + 2, HEADER_PADDING + 2, 4);
    
    // Draw main text
    tft.setTextColor(COLOR_TEXT, COLOR_HEADER_BG);
    tft.setTextFont(4);
    tft.drawString("Zone Controller", HEADER_PADDING, HEADER_PADDING, 4);
    
    // Draw subtle line under header using the dimmer border color
    tft.drawFastHLine(0, HEADER_HEIGHT - 1, tft.width(), COLOR_BORDER_OFF);
    // Draw temperature and humidity on the right
    char buf[32];
    if (!isnan(lastTemp) && !isnan(lastHum)) {
        snprintf(buf, sizeof(buf), "%2.1f°C  %2.0f%%", lastTemp, lastHum);
        tft.setTextDatum(TR_DATUM);
        tft.setTextColor(COLOR_TEXT, COLOR_HEADER_BG);
        tft.setTextFont(4);
        tft.drawString(buf, tft.width() - HEADER_PADDING, HEADER_PADDING, 4);
    }
}

void drawZoneCard(int index) {
    const Zone& zone = zones[index];
    
    // Draw card shadow
    tft.fillRoundRect(zone.x + CARD_SHADOW, zone.y + CARD_SHADOW, 
                     zone.w, zone.h, CARD_CORNER, COLOR_SHADOW);
    
    // Card colors - make active state even brighter
    uint16_t baseColor = zone.isOpen ? COLOR_ACTIVE : COLOR_INACTIVE;
    uint16_t borderColor = zone.isOpen ? COLOR_BORDER_ON : COLOR_BORDER_OFF;
    uint16_t bgColor = zone.isOpen ? COLOR_CARD_BG + 0x0841 : COLOR_CARD_BG;
    
    // Draw card background
    tft.fillRoundRect(zone.x, zone.y, zone.w, zone.h, CARD_CORNER, bgColor);
    
    // Draw card border with consistent style
    tft.drawRoundRect(zone.x, zone.y, zone.w, zone.h, CARD_CORNER, borderColor);
    
    // Draw status indicator: green when on, red when off
    int dotX = zone.x + STATUS_DOT_PADDING;
    int dotY = zone.y + (zone.h - STATUS_DOT_SIZE) / 2;
    uint16_t dotColor = zone.isOpen ? COLOR_DOT_ON : COLOR_DOT_OFF;
    tft.fillCircle(dotX + STATUS_DOT_SIZE/2, dotY + STATUS_DOT_SIZE/2, 
                  STATUS_DOT_SIZE/2, dotColor);
    tft.drawCircle(dotX + STATUS_DOT_SIZE/2, dotY + STATUS_DOT_SIZE/2, 
                  STATUS_DOT_SIZE/2, COLOR_SHADOW);
    
    // Draw zone name with proper padding
    tft.setTextDatum(ML_DATUM);
    tft.setTextColor(zone.isOpen ? COLOR_TEXT : COLOR_TEXT_DIM, bgColor);
    
    // Calculate text position and available width (fixed)
    int leftMargin = STATUS_DOT_PADDING + STATUS_DOT_SIZE + STATUS_DOT_PADDING;
    int textX = zone.x + leftMargin;
    int textY = zone.y + zone.h/2;
    int maxTextWidth = zone.w - leftMargin - TEXT_PADDING;
    int maxTextHeight = zone.h - 2 * TEXT_PADDING;
    
    uint8_t fontSizes[] = {4, 2};
    int spaceIdx = zone.name.indexOf(' ');
    if (spaceIdx != -1) {
        // Try to fit as two lines first
        String line1 = zone.name.substring(0, spaceIdx);
        String line2 = zone.name.substring(spaceIdx + 1);
        for (uint8_t font : fontSizes) {
            tft.setTextFont(font);
            int lineH = tft.fontHeight();
            int line1W = tft.textWidth(line1);
            int line2W = tft.textWidth(line2);
            int maxLineW = max(line1W, line2W);
            int lineSpacing = 8;
            if (maxLineW <= maxTextWidth && (lineH * 2 + lineSpacing) <= maxTextHeight) {
                tft.drawString(line1, textX, textY - (lineH/2 + lineSpacing/2));
                tft.drawString(line2, textX, textY + (lineH/2 + lineSpacing/2));
                return;
            }
        }
        // If not, try to fit as one line
        for (uint8_t font : fontSizes) {
            tft.setTextFont(font);
            int lineH = tft.fontHeight();
            if (tft.textWidth(zone.name) <= maxTextWidth && lineH <= maxTextHeight) {
                tft.drawString(zone.name, textX, textY);
                return;
            }
        }
    } else {
        // No space, just try to fit as one line
        for (uint8_t font : fontSizes) {
            Serial.printf("looking for font fit[%s][len=%d][maxTextWidth=%d]: %d\n", zone.name.c_str(), tft.textWidth(zone.name), maxTextWidth , font);
            tft.setTextFont(font);
            if (tft.textWidth(zone.name) <= maxTextWidth) {
                Serial.printf("FOUND font fit[%s][len=%d][maxTextWidth=%d]: %d\n", zone.name.c_str(), tft.textWidth(zone.name), maxTextWidth , font);
                tft.drawString(zone.name, textX, textY);
                return;
            }
        }
    }
    Serial.printf("BORKED IT, GOING WITH 2 [%s][len=%d][maxTextWidth=%d]: %d\n", zone.name.c_str(), tft.textWidth(zone.name), maxTextWidth , 2);
    // If nothing fits, just draw with smallest font (may clip)
    tft.setTextFont(2);
    tft.drawString(zone.name, textX, textY);
}

void drawAllZones() {
    // Clear screen and draw gradient background
    drawGradientBackground();
    drawHeader();
    
    if (zones.empty()) {
        // Show "No zones configured" message
        tft.setTextColor(COLOR_TEXT_DIM, COLOR_BG);
        tft.setTextDatum(MC_DATUM);
        tft.drawString("No zones configured", tft.width()/2, tft.height()/2, 4);
        return;
    }
    
    // Calculate card dimensions for 2-column grid with proper spacing
    const int numCols = 2;
    const int numRows = (zones.size() + 1) / 2;  // Round up division
    const int cardW = (tft.width() - (3 * CARD_PADDING)) / numCols;
    const int cardH = (tft.height() - HEADER_HEIGHT - ((numRows + 1) * CARD_PADDING)) / numRows;
    
    // Draw all zone cards
    for (int i = 0; i < zones.size(); ++i) {
        int row = i / numCols;
        int col = i % numCols;
        
        // Calculate card position with padding
        int x = CARD_PADDING + col * (cardW + CARD_PADDING);
        int y = HEADER_HEIGHT + CARD_PADDING + row * (cardH + CARD_PADDING);
        
        // Update zone coordinates
        zones[i].x = x;
        zones[i].y = y;
        zones[i].w = cardW;
        zones[i].h = cardH;
        
        drawZoneCard(i);
    }
}

int getTouchedZone(int tx, int ty) {
    // Print zone 0 info for every touch
    if (!zones.empty()) {
        Serial.printf("ZONE 0 (for touch): x=%d y=%d w=%d h=%d\n", zones[0].x, zones[0].y, zones[0].w, zones[0].h);
        Serial.printf("ZONE 1 (for touch): x=%d y=%d w=%d h=%d\n", zones[1].x, zones[1].y, zones[1].w, zones[1].h);
        Serial.printf("ZONE 16 (for touch): x=%d y=%d w=%d h=%d\n", zones[15].x, zones[15].y, zones[15].w, zones[15].h);

    }
    for (int i = 0; i < zones.size(); ++i) {
        Zone &z = zones[i];
        if (tx >= z.x && tx < z.x + z.w && ty >= z.y && ty < z.y + z.h) {
            Serial.printf("Touch at (%d,%d) in zone %d: x=%d y=%d w=%d h=%d\n", tx, ty, i, z.x, z.y, z.w, z.h);
            return i;
        }
    }
    Serial.printf("Touch at (%d,%d) not in any zone\n", tx, ty);
    return -1;
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
    String t = String(topic);
    String p;
    for (unsigned int i = 0; i < length; i++) p += (char)payload[i];
    
    Serial.printf("MQTT CALLBACK topic[%s], payload[%s]\n", t.c_str(), p.c_str());
    
    // Extract zone number from topic
    int zoneNumber = 0;
    if (t.startsWith(BASE_TOPIC "/zone") && t.indexOf("/") > 0) {
        String zoneStr = t.substring(t.indexOf("/zone") + 5);
        zoneStr = zoneStr.substring(0, zoneStr.indexOf("/"));
        zoneNumber = zoneStr.toInt();
    }
    
    if (zoneNumber > 0) {
        String stateTopic = BASE_TOPIC "/zone" + String(zoneNumber) + "/state";
        String nameTopic = BASE_TOPIC "/zone" + String(zoneNumber) + "/name";
        
        if (t == stateTopic) {
            // Add zone if it doesn't exist
            addZone(zoneNumber);
            
            // Update state for the zone
            for (auto& zone : zones) {
                if (zone.number == zoneNumber) {  // Changed from name check to number check
                    zone.isOpen = (p == "ON");
                    drawZoneCard(&zone - &zones[0]);  // Get index of zone
                    Serial.printf("Zone %d state updated to [%s]\n", zoneNumber, p.c_str());
                    break;
                }
            }
        }
        else if (t == nameTopic) {
            // Add zone if it doesn't exist
            addZone(zoneNumber, p);
            
            // Update name for the zone
            for (auto& zone : zones) {
                if (zone.number == zoneNumber) {  // Changed from name check to number check
                    zone.name = p;
                    drawZoneCard(&zone - &zones[0]);  // Get index of zone
                    Serial.printf("Zone %d name updated to [%s]\n", zoneNumber, p.c_str());
                    break;
                }
            }
        }
    }
}

void connectToWifi() {
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    Serial.print("Connecting to WiFi");
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\nWiFi connected!");
}

void mqttReconnect() {
    while (!mqttClient.connected()) {
        Serial.print("Attempting MQTT connection...");
        String clientId = "ZonePanel-" + String(random(0xffff), HEX);
        if (mqttClient.connect(clientId.c_str(), MQTT_USER, MQTT_PASS)) {
            Serial.println("connected");
            // Subscribe to all possible zone topics
            for (int i = 1; i <= 15; ++i) {  // Changed from ZONE_COUNT to 15
                String stateTopic = BASE_TOPIC "/zone" + String(i) + "/state";
                String nameTopic = BASE_TOPIC "/zone" + String(i) + "/name";
                mqttClient.subscribe(stateTopic.c_str(), 1);
                mqttClient.subscribe(nameTopic.c_str(), 1);
            }
        } else {
            Serial.print("failed, rc=");
            Serial.print(mqttClient.state());
            Serial.println(" try again in 5 seconds");
            delay(5000);
        }
    }
}

void setup() {
    Serial.begin(115200);    
    // Remove ensureZones() call since we start with empty zones list

    tft.init();
    tft.setRotation(0);
    drawAllZones();  // This will show "No zones configured" initially

    ts.begin();
    connectToWifi();
    mqttClient.setServer(MQTT_HOST, MQTT_PORT);
    mqttClient.setCallback(mqttCallback);
    Wire.begin(21, 22); // SDA yellow, SCL blue
    delay(100);
    // I2C scanner for debugging
    Serial.println("I2C scan...");
    for (uint8_t addr = 1; addr < 127; addr++) {
        Wire.beginTransmission(addr);
        if (Wire.endTransmission() == 0) {
            Serial.print("Found device at 0x");
            Serial.println(addr, HEX);
        }
    }
    Serial.println("Scan done.");
    if (!aht10.begin(&Wire)) {
      Serial.println("AHT10 not found");
    } else {
      Serial.println("AHT10 initialized");
    }
}

void loop() {
    if (!mqttClient.connected()) {
        mqttReconnect();
    }
    mqttClient.loop();
    
    // Handle touch
    ts.read();
    if (ts.isTouched) {
        TP_Point p = ts.points[0];
        int fx = 320 - p.x;
        int fy = 480 - p.y;
        int idx = getTouchedZone(fx, fy);
        if (idx != -1) {
            bool newState = !zones[idx].isOpen;
            String topic = BASE_TOPIC "/zone" + String(idx+1) + "/set";
            mqttClient.publish(topic.c_str(), newState ? "ON" : "OFF");
            zones[idx].isOpen = newState;
            drawZoneCard(idx);
            delay(120);
            drawZoneCard(idx);
            while (ts.isTouched) {
                ts.read();
                delay(10);
            }
        }
    }
    // Update AHT10 every 2 seconds
    if (millis() - lastAHT10Read > 2000) {
        sensors_event_t humidity, temp;
        aht10.getEvent(&humidity, &temp);
        if (!isnan(temp.temperature) && !isnan(humidity.relative_humidity)) {
            lastTemp = temp.temperature;
            lastHum = humidity.relative_humidity;
            drawHeader(); // Redraw header with new values
        }
        lastAHT10Read = millis();
    }
    delay(20);
}