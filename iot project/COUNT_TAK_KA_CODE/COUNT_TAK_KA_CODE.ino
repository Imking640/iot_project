#include <WiFi.h>
#include <ESP32Servo.h>
#include "time.h"

// WiFi Credentials
const char* ssid = "Pranjal";
const char* password = "Pranjal@21";

// Time config (NTP server)
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 19800; // GMT+5:30
const int   daylightOffset_sec = 0;

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

  server.begin();
  Serial.println("HTTP server started");
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
  const int MAX_CAPACITY = 5;
  
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
        if (personCount < MAX_CAPACITY) {
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

void handleWebClient() {
  WiFiClient client = server.available();
  
  if (client) {
    Serial.println("New client connected");
    String request = client.readStringUntil('\r');
    
    if (request.indexOf("/count") != -1) {
      // Send JSON response
      client.println("HTTP/1.1 200 OK");
      client.println("Content-Type: application/json");
      client.println("Access-Control-Allow-Origin: *");
      client.println();
      
      String timeStr = getFormattedTime();
      client.print("{\"count\": ");
      client.print(personCount);
      client.print(", \"time\": \"");
      client.print(timeStr);
      client.println("\"}");

      Serial.println("Sent count with time: " + String(personCount) + " @ " + timeStr);
    }

    client.stop();
    Serial.println("Client disconnected");
  }
}

void loop() {
  // Get sensor readings
  long distance1 = getDistance(TRIG_PIN_1, ECHO_PIN_1);
  long distance2 = getDistance(TRIG_PIN_2, ECHO_PIN_2);

  // Check for person detection (within 15cm)
  if ((distance1 > 0 && distance1 <= 15) || (distance2 > 0 && distance2 <= 15)) {
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
