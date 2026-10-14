/************************************************************
  AUTOMATIC PILL DISPENSER
  ESP32 + 5 buttons + 360-degree continuous-rotation servo
  OLED + buzzer + Raspberry Pi USB serial + Blynk IoT
************************************************************/

#define BLYNK_TEMPLATE_ID "TMPL34wqQEi22"
#define BLYNK_TEMPLATE_NAME "AUTO PILL DISPENSER"
#define BLYNK_AUTH_TOKEN "87Maly6ajWc2edCZKfNPz584FrBX2k66"

#include <WiFi.h>
#include <BlynkSimpleEsp32.h>
#include <ESP32Servo.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// Wi-Fi
char ssid[] = "Darshan";
char pass[] = "123456780";

// Pins
const int SERVO_PIN = 18;
const int BUZZER_PIN = 23;
const int BUTTON_PINS[5] = {25, 26, 27, 32, 33};

// OLED I2C
const int OLED_SDA = 21;
const int OLED_SCL = 22;
Adafruit_SSD1306 oled(128, 64, &Wire, -1);
bool oledReady = false;

// Servo
Servo pillServo;

// Continuous-rotation servo pulse settings.
// Calibrate these values for your motor.
// 1500 us is usually stop; 1600/1400 are example speeds.
const int SERVO_STOP_US = 1500;
const int SERVO_CW_US = 1600;
const int SERVO_CCW_US = 1400;

// Approximate milliseconds per complete 360-degree rotation.
// Measure and adjust with your actual loaded platform.
const unsigned long FULL_TURN_MS = 4000;

// Requested target positions for buttons 1 to 5.
const int TARGET_DEGREES[5] = {60, 120, 180, 240, 300};

const char* MEDICINES[5] = {
  "Paracetamol",
  "Vitamin C",
  "Calcium",
  "Aspirin",
  "Vitamin D"
};

// Button state and debounce
bool previousButtonState[5] = {HIGH, HIGH, HIGH, HIGH, HIGH};
unsigned long lastButtonTime[5] = {0, 0, 0, 0, 0};
const unsigned long DEBOUNCE_MS = 250;

// Movement tracking
int currentPosition = 0;
int selectedIndex = -1;
int movementTarget = 0;
unsigned long movementStart = 0;
unsigned long movementDuration = 0;
bool moving = false;
String movementSource = "";

String serialBuffer = "";

// OLED display
void showScreen(String line1, String line2 = "", String line3 = "") {
  if (!oledReady) return;

  oled.clearDisplay();
  oled.setTextSize(1);
  oled.setTextColor(SSD1306_WHITE);

  oled.setCursor(0, 5);
  oled.println(line1);

  oled.setCursor(0, 25);
  oled.println(line2);

  oled.setCursor(0, 45);
  oled.println(line3);

  oled.display();
}

// Buzzer
void beep(int durationMs = 120) {
  digitalWrite(BUZZER_PIN, HIGH);
  delay(durationMs);
  digitalWrite(BUZZER_PIN, LOW);
}

// Blynk notification
void notifyBlynk(String message) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("STATUS,WiFi disconnected; notification not sent");
    return;
  }

  if (!Blynk.connected()) {
    Serial.println("STATUS,Blynk disconnected; notification not sent");
    return;
  }

  Blynk.logEvent("pill_dispenser", message);
  Serial.println("STATUS,Blynk event submitted");
}

// Begin movement to the selected target
void startDispense(int index, String source) {
  if (index < 0 || index >= 5) return;

  if (moving) {
    Serial.println("ERROR,Servo is busy");
    return;
  }

  selectedIndex = index;
  movementSource = source;
  movementTarget = TARGET_DEGREES[index];

  // Calculate relative movement, choosing the shorter path.
  int delta = movementTarget - currentPosition;
  if (delta > 180) delta -= 360;
  if (delta < -180) delta += 360;

  Serial.printf("START,%d,%s,%s\n",
                index + 1, MEDICINES[index],
                source.c_str());

  showScreen("Preparing:", MEDICINES[index],
             "Target " + String(movementTarget) + " deg");

  beep();

  if (delta == 0) {
    movementDuration = 0;
  } else {
    movementDuration =
      (unsigned long)(abs(delta) * FULL_TURN_MS / 360.0);

    int pulse = (delta > 0) ? SERVO_CW_US : SERVO_CCW_US;
    pillServo.writeMicroseconds(pulse);
  }

  movementStart = millis();
  moving = true;

  Serial.printf("STATUS,Target=%d,Delta=%d,Duration=%lu ms\n",
                movementTarget, delta, movementDuration);
}

// Stop servo and report completion
void finishDispense() {
  pillServo.writeMicroseconds(SERVO_STOP_US);

  if (selectedIndex >= 0 && selectedIndex < 5) {
    currentPosition = movementTarget;

    showScreen("Selected:", MEDICINES[selectedIndex],
               "Follow prescription");

    beep(250);

    Serial.printf("DONE,%d,%s,%s\n",
                  selectedIndex + 1,
                  MEDICINES[selectedIndex],
                  movementSource.c_str());

    String message = String("Compartment ") +
                     String(selectedIndex + 1) + ": " +
                     MEDICINES[selectedIndex] +
                     " selected. Source: " + movementSource;

    notifyBlynk(message);
  }

  selectedIndex = -1;
  movementSource = "";
  moving = false;
}

// Handle commands from Raspberry Pi
void handleCommand(String cmd) {
  cmd.trim();
  if (cmd.length() == 0) return;

  Serial.println("PI_COMMAND," + cmd);

  if (cmd.startsWith("DISPENSE,")) {
    int number = cmd.substring(9).toInt();

    if (number >= 1 && number <= 5) {
      startDispense(number - 1, "PI");
    } else {
      Serial.println("ERROR,Use DISPENSE,1 through DISPENSE,5");
    }
    return;
  }

  if (cmd.startsWith("REMINDER,")) {
    // Example: REMINDER,morning,Aspirin,2
    String details = cmd.substring(9);

    showScreen("Scheduled reminder:", details,
               "Check prescribed dose");

    Serial.println("REMINDER_RECEIVED," + details);
    notifyBlynk("Scheduled medicine reminder: " + details);
    beep(200);
    return;
  }

  if (cmd == "PING") {
    Serial.println("PONG,ESP32");
    return;
  }

  // Only issue HOME after physically aligning the platform
  // to its known zero-degree reference.
  if (cmd == "HOME") {
    if (!moving) {
      pillServo.writeMicroseconds(SERVO_STOP_US);
      currentPosition = 0;
      showScreen("Home position set",
                 "Position reset",
                 "Align platform first");
      Serial.println("HOME,OK");
    } else {
      Serial.println("ERROR,Servo busy");
    }
    return;
  }

  Serial.println("ERROR,Unknown command");
}

// Read USB serial commands
void readSerialCommands() {
  while (Serial.available() > 0) {
    char c = (char)Serial.read();

    if (c == '\n') {
      handleCommand(serialBuffer);
      serialBuffer = "";
    } else if (c != '\r') {
      if (serialBuffer.length() < 150) {
        serialBuffer += c;
      } else {
        serialBuffer = "";
        Serial.println("ERROR,Command too long");
      }
    }
  }
}

// Setup
void setup() {
  Serial.begin(115200);

  for (int i = 0; i < 5; i++) {
    pinMode(BUTTON_PINS[i], INPUT_PULLUP);
  }

  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  Wire.begin(OLED_SDA, OLED_SCL);
  oledReady = oled.begin(SSD1306_SWITCHCAPVCC, 0x3C);

  if (oledReady) {
    showScreen("Pill Dispenser", "Starting...");
  } else {
    Serial.println("WARNING,OLED not detected at 0x3C");
  }

  pillServo.setPeriodHertz(50);
  pillServo.attach(SERVO_PIN, 500, 2500);
  pillServo.writeMicroseconds(SERVO_STOP_US);

  // Connect to Wi-Fi
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, pass);
  showScreen("Connecting WiFi", ssid);

  unsigned long startTime = millis();
  while (WiFi.status() != WL_CONNECTED &&
         millis() - startTime < 12000) {
    delay(250);
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("WIFI,CONNECTED");
    Blynk.config(BLYNK_AUTH_TOKEN);
    Blynk.connect(3000);
  } else {
    Serial.println("WIFI,FAILED");
  }

  Serial.println(Blynk.connected()
                 ? "BLYNK,CONNECTED"
                 : "BLYNK,NOT_CONNECTED");

  showScreen("System Ready", "Press button 1-5",
             "USB serial active");

  beep(100);
  Serial.println("READY,ESP32");
}

// Main loop
void loop() {
  if (WiFi.status() == WL_CONNECTED) {
    Blynk.run();

    static unsigned long lastReconnect = 0;
    if (!Blynk.connected() &&
        millis() - lastReconnect > 10000) {
      lastReconnect = millis();
      Blynk.connect(1000);
    }
  }

  readSerialCommands();

  // Complete movement without a long blocking delay.
  if (moving &&
      millis() - movementStart >= movementDuration) {
    finishDispense();
  }

  // Physical buttons
  if (!moving) {
    for (int i = 0; i < 5; i++) {
      bool state = digitalRead(BUTTON_PINS[i]);

      if (previousButtonState[i] == HIGH &&
          state == LOW &&
          millis() - lastButtonTime[i] > DEBOUNCE_MS) {

        lastButtonTime[i] = millis();

        Serial.printf("BUTTON,%d,%s\n",
                      i + 1, MEDICINES[i]);

        startDispense(i, "BUTTON");
      }

      previousButtonState[i] = state;
    }
  }
}