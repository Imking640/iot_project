#include <WiFi.h>
#include <ESP32Servo.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include "time.h"

// WiFi Credentials
const char* ssid = "Pranjal";
const char* password = "Pranjal@21";

// Time config (NTP server)
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 19800; // GMT+5:30
const int   daylightOffset_sec = 0;

// External server to fetch threshold from
const char* thresholdServerUrl = "http://192.168.247.133/threshold"; // Replace with your server URL
const unsigned long thresholdUpdateInterval = 60000; // Update threshold every 60 seconds
unsigned long lastThresholdUpdateTime = 0;

// Hardware Pins
#define TRIG_PIN_1    32
#define ECHO_PIN_1    33
#define TRIG_PIN_2    12
#define ECHO_PIN_2    14
#define SERVO_PIN     5
#define IR_SENSOR_1   26
#define IR_SENSOR_2   27
#define BUZZER_PIN    4    // Buzzer control pin

Servo myServo;
int personCount = 0;
int maxCapacity = 5;  // Default max capacity, will be updated from server
WiFiServer server(80);  // Web server on port 80

void setup() {
  Serial.begin(115200);

  // Initialize hardware
  pinMode(TRIG_PIN_1, OUTPUT);
  pinMode(ECHO_PIN_1, INPUT);
  pinMode(TRIG_PIN_2, OUTPUT);
  pinMode(ECHO_PIN_2, INPUT);
  pinMode(IR_SENSOR_1, INPUT);
  pinMode(IR_SENSOR_2, INPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, HIGH);  // Buzzer initially off
  
  myServo.attach(SERVO_PIN);
  myServo.write(80);  // Initial position (door closed)

  // Connect to WiFi
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi...");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\n✅ WiFi Connected!");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());

  // Init and sync NTP
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  Serial.println("⏱ Time synchronized");

  // Fetch initial threshold from server
  fetchThresholdFromServer();

  server.begin();
  Serial.println("HTTP server started");
}

// Function to fetch threshold from external server
void fetchThresholdFromServer() {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    
    Serial.print("Fetching threshold from server... ");
    http.begin(thresholdServerUrl);
    
    // Send GET request
    int httpResponseCode = http.GET();
    
    if (httpResponseCode > 0) {
      String payload = http.getString();
      Serial.println("Response code: " + String(httpResponseCode));
      Serial.println("Payload: " + payload);
      
      // Parse JSON response
      DynamicJsonDocument doc(1024);
      DeserializationError error = deserializeJson(doc, payload);
      
      if (!error) {
        if (doc.containsKey("threshold")) {
          int newThreshold = doc["threshold"];
          
          // Apply threshold if valid
          if (newThreshold >= 0) {
            maxCapacity = newThreshold;
            Serial.print("✅ Threshold updated from server: ");
            Serial.println(maxCapacity);
          } else {
            Serial.println("❌ Invalid threshold value received");
          }
        } else {
          Serial.println("❌ JSON does not contain threshold key");
        }
      } else {
        Serial.print("❌ JSON parse error: ");
        Serial.println(error.c_str());
      }
    } else {
      Serial.print("❌ HTTP request failed, error: ");
      Serial.println(httpResponseCode);
    }
    
    http.end();
  } else {
    Serial.println("❌ WiFi not connected");
  }
}

String getFormattedTime() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    return "00:00:00";
  }

  char buffer[9];
  strftime(buffer, sizeof(buffer), "%H:%M:%S", &timeinfo);  // 24-hour format
  return String(buffer);
}

long getDistance(int trigPin, int echoPin) {
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);

  long duration = pulseIn(echoPin, HIGH, 30000); // Timeout after 30ms
  return duration > 0 ? (duration * 0.034 / 2) : -1;
}

void checkPersonEntryExit() {
  Serial.println("Detecting entry/exit...");
  unsigned long startTime = millis();
  unsigned long ir1Time = 0, ir2Time = 0;
  const unsigned long timeout = 5000;
  
  while (millis() - startTime < timeout) {
    if (digitalRead(IR_SENSOR_1) == LOW && ir1Time == 0) {
      ir1Time = millis();
      Serial.println("IR1 triggered (Entry side)");
    }
    if (digitalRead(IR_SENSOR_2) == LOW && ir2Time == 0) {
      ir2Time = millis();
      Serial.println("IR2 triggered (Exit side)");
    }

    if (ir1Time > 0 && ir2Time > 0) {
      if (ir1Time < ir2Time) {
        if (personCount < maxCapacity) {  // Using the threshold value here
          personCount++;
          Serial.println("✅ Person entered");
          // Buzzer for entry
          digitalWrite(BUZZER_PIN, LOW);  // Turn buzzer ON
          delay(500);  // Beep for 500 ms
          digitalWrite(BUZZER_PIN, HIGH);  // Turn buzzer OFF
        } else {
          Serial.println("❌ Max capacity! Entry denied.");
          // Buzzer for capacity limit
          digitalWrite(BUZZER_PIN, LOW);  // Turn buzzer ON
          delay(2000);  // Beep for 2 seconds
          digitalWrite(BUZZER_PIN, HIGH);  // Turn buzzer OFF
        }
      } else {
        personCount = max(0, personCount - 1);
        Serial.println("✅ Person exited");
        // Buzzer for exit
        digitalWrite(BUZZER_PIN, LOW);  // Turn buzzer ON
        delay(500);  // Beep for 500 ms
        digitalWrite(BUZZER_PIN, HIGH);  // Turn buzzer OFF
      }
      break;
    }
    delay(50);
  }
  Serial.print("👥 Current count: ");
  Serial.println(personCount);
}

void sendCORSHeaders(WiFiClient &client) {
  client.println("HTTP/1.1 200 OK");
  client.println("Access-Control-Allow-Origin: *");
  client.println("Access-Control-Allow-Methods: GET, POST, OPTIONS");
  client.println("Access-Control-Allow-Headers: Content-Type");
  client.println("Content-Type: application/json");
  client.println("Connection: close");
  client.println();
}

void handleWebClient() {
  WiFiClient client = server.available();
  
  if (client) {
    Serial.println("New client connected");
    
    // Wait for data to be available
    unsigned long timeout = millis();
    while (client.connected() && !client.available()) {
      if (millis() - timeout > 1000) {
        Serial.println("Client connection timeout");
        client.stop();
        return;
      }
      delay(1);
    }
    
    // Read the first line of HTTP request
    String request = client.readStringUntil('\r');
    Serial.println("Request: " + request);
    
    // Extract the HTTP method and path
    int methodEnd = request.indexOf(' ');
    int pathEnd = request.indexOf(' ', methodEnd + 1);
    String method = request.substring(0, methodEnd);
    String path = request.substring(methodEnd + 1, pathEnd);
    
    // Handle OPTIONS request (CORS preflight)
    if (method == "OPTIONS") {
      sendCORSHeaders(client);
      client.stop();
      return;
    }
    
    // Handle count endpoint
    if (path == "/count") {
      sendCORSHeaders(client);
      
      String timeStr = getFormattedTime();
      client.print("{\"count\": ");
      client.print(personCount);
      client.print(", \"time\": \"");
      client.print(timeStr);
      client.print("\", \"threshold\": ");
      client.print(maxCapacity);
      client.println("}");

      Serial.println("Sent count with time: " + String(personCount) + " @ " + timeStr);
    }
    // Handle threshold endpoint (still kept for manual overrides)
    else if (path == "/threshold" && method == "POST") {
      // Skip HTTP headers
      while (client.available()) {
        String line = client.readStringUntil('\r');
        client.read(); // Skip \n
        if (line.length() <= 1) { // Empty line signifies end of headers
          break;
        }
      }
      
      // Read the JSON body
      String jsonBody = "";
      while (client.available()) {
        jsonBody += (char)client.read();
      }
      
      Serial.println("Received JSON: " + jsonBody);
      
      // Parse JSON
      DynamicJsonDocument doc(1024);
      DeserializationError error = deserializeJson(doc, jsonBody);
      
      if (!error) {
        if (doc.containsKey("threshold")) {
          int newThreshold = doc["threshold"];
          
          // Apply threshold if valid
          if (newThreshold >= 0) {
            maxCapacity = newThreshold;
            Serial.print("✅ New threshold set manually: ");
            Serial.println(maxCapacity);
            
            // Send success response
            sendCORSHeaders(client);
            client.print("{\"success\": true, \"message\": \"Threshold updated to ");
            client.print(maxCapacity);
            client.println("\"}");
          } else {
            // Send error for invalid threshold
            client.println("HTTP/1.1 400 Bad Request");
            client.println("Content-Type: application/json");
            client.println("Access-Control-Allow-Origin: *");
            client.println();
            client.println("{\"success\": false, \"message\": \"Invalid threshold value\"}");
          }
        } else {
          // Send error for missing threshold
          client.println("HTTP/1.1 400 Bad Request");
          client.println("Content-Type: application/json");
          client.println("Access-Control-Allow-Origin: *");
          client.println();
          client.println("{\"success\": false, \"message\": \"Missing threshold parameter\"}");
        }
      } else {
        // Send error for JSON parsing
        client.println("HTTP/1.1 400 Bad Request");
        client.println("Content-Type: application/json");
        client.println("Access-Control-Allow-Origin: *");
        client.println();
        client.println("{\"success\": false, \"message\": \"Invalid JSON format\"}");
        Serial.print("❌ JSON parse error: ");
        Serial.println(error.c_str());
      }
    }
    // Add a new endpoint to trigger manual threshold update
    else if (path == "/update-threshold" && method == "GET") {
      fetchThresholdFromServer();
      
      sendCORSHeaders(client);
      client.print("{\"success\": true, \"message\": \"Threshold update requested\", \"current_threshold\": ");
      client.print(maxCapacity);
      client.println("}");
    }
    else {
      // Send 404 Not Found for unknown endpoints
      client.println("HTTP/1.1 404 Not Found");
      client.println("Content-Type: application/json");
      client.println("Access-Control-Allow-Origin: *");
      client.println();
      client.println("{\"success\": false, \"message\": \"Endpoint not found\"}");
    }

    client.stop();
    Serial.println("Client disconnected");
  }
}

void loop() {
  // Check if it's time to update the threshold
  unsigned long currentMillis = millis();
  if (currentMillis - lastThresholdUpdateTime >= thresholdUpdateInterval) {
    lastThresholdUpdateTime = currentMillis;
    fetchThresholdFromServer();
  }
  
  // Get sensor readings
  long distance1 = getDistance(TRIG_PIN_1, ECHO_PIN_1);
  long distance2 = getDistance(TRIG_PIN_2, ECHO_PIN_2);

  if (distance1 > 0 && distance1 <= 15 && personCount >= maxCapacity)
  {
          Serial.println("❌ Max capacity! Entry denied.");
          // Buzzer for capacity limit
          digitalWrite(BUZZER_PIN, LOW);  // Turn buzzer ON
          delay(2000);  // Beep for 2 seconds
          digitalWrite(BUZZER_PIN, HIGH);  // Turn buzzer OFF
        }

  // Check for person detection (within 15cm)
  if ((distance1 > 0 && distance1 <= 15 && personCount < maxCapacity) || (distance2 > 0 && distance2 <= 15)) {
    Serial.println("🚪 Object detected! Opening door...");
    
    for (int angle = 80; angle <= 180; angle += 2) {
      myServo.write(angle);
      delay(15);
    }

    checkPersonEntryExit();
    delay(3000); // Allow some time for the person to pass

    Serial.println("🚪 Closing door");
    for (int angle = 180; angle >= 80; angle -= 2) {
      myServo.write(angle);
      delay(15);
    }
  }

  handleWebClient();
  delay(100);
}
