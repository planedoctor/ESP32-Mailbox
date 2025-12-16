//
#include <WiFi.h>
#include <ESP_Mail_Client.h>
#include <time.h>

// Wi-Fi credentials
#define WIFI_SSID "your_wifi_ssid"
#define WIFI_PASSWORD "your_wifi_password"
#define WIFI_TIMEOUT_MS 30000  // 30 seconds

// SMTP settings
#define SMTP_HOST "smtp.gmail.com"
#define SMTP_PORT 465
#define AUTHOR_EMAIL "your_sender@gmail.com"
#define AUTHOR_PASSWORD "your_app_password"   // 16-char App Password from Google
#define RECIPIENT_EMAIL "your_recipient@gmail.com"

// I/O pins
#define INPUT_PIN 2
#define LED_PIN 15   // CORRECT Pin for Onboard LED on Xiao-ESP32C6

// LED logic (active-LOW on Xiao ESP32C6)
#define LED_ON  LOW
#define LED_OFF HIGH

// Pulse detection
volatile int pulseCount = 0;
volatile unsigned long lastPulseTime = 0;
const unsigned long PULSE_TIMEOUT = 1500;  // ms between pulses
const unsigned long TRAIN_TIMEOUT = 6000;  // max time for 5-7 pulse train
unsigned long trainStartTime = 0;
bool emailSent = false;

// Email session
SMTPSession smtp;
ESP_Mail_Session session;

// Timezone (Eastern)
const char* TZ_INFO = "EST5EDT,M3.2.0/2,M11.1.0";

// Function prototypes
void blinkPulseCount(int count);
void sendEmail();
IRAM_ATTR void pulseISR();

void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LED_ON);   // *** TWEAK 1: LED ON during setup ***

  Serial.println("Starting setup...");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to Wi-Fi: ");
  Serial.println(WIFI_SSID);

  unsigned long startAttemptTime = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < WIFI_TIMEOUT_MS) {
    // We keep the LED solidly ON while connecting
    delay(500); 
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Wi-Fi connection failed!");
    for (int i = 0; i < 10; i++) { // error blink pattern
      digitalWrite(LED_PIN, LED_ON); delay(200);
      digitalWrite(LED_PIN, LED_OFF); delay(200);
    }
    while (true) delay(1000);  // halt on failure
  }

  Serial.println("Wi-Fi connected. IP: " + WiFi.localIP().toString());

  // Time setup (NTP)
  configTime(0, 0, "pool.ntp.org");
  setenv("TZ", TZ_INFO, 1);
  tzset();

  time_t now;
  Serial.println("Waiting for NTP sync...");
  do {
    time(&now);
    delay(500);
  } while (now < 8 * 3600);

  Serial.println("Time synchronized.");

  // Input pin setup
  pinMode(INPUT_PIN, INPUT_PULLUP); // Added PULLUP for stability
  attachInterrupt(digitalPinToInterrupt(INPUT_PIN), pulseISR, FALLING);

  // SMTP configuration
  smtp.debug(2);
  session.server.host_name = SMTP_HOST;
  session.server.port = SMTP_PORT;
  session.login.email = AUTHOR_EMAIL;
  session.login.password = AUTHOR_PASSWORD;
  session.login.user_domain = "gmail.com";

  Serial.println("Setup complete. Monitoring GPIO2...");
  digitalWrite(LED_PIN, LED_OFF);  // *** TWEAK 2: Turn LED OFF when ready to monitor ***
}

void loop() {
  // Check for completed pulse train
  if (pulseCount > 0 && (millis() - lastPulseTime > PULSE_TIMEOUT)) {
    Serial.print("Train ended with count: ");
    Serial.println(pulseCount);

    // Check if the pulse train is valid and we haven't sent an email yet
    //  Currently set as true with 5-7 pulsecount change this variable
    if (pulseCount >= 5 && pulseCount <= 7 &&
        (millis() - trainStartTime < TRAIN_TIMEOUT) && !emailSent) {
      
      sendEmail();
      emailSent = true;
      blinkPulseCount(pulseCount);
      // *** TWEAK 3: blinkPulseCount function handles turning LED off at the end ***
    }

    // Reset all pulse tracking variables
    pulseCount = 0;
    trainStartTime = 0;
    // The loop naturally keeps the LED OFF unless blinkPulseCount runs
  }

  // Reset email flag after a long timeout
  if (emailSent && (millis() - lastPulseTime > TRAIN_TIMEOUT)) {
    emailSent = false;
  }

  delay(100);
}

// Interrupt: count pulses
IRAM_ATTR void pulseISR() {
  // ISRs should be as fast as possible. Avoid Serial prints here in final code.
  unsigned long now = millis();
  if (now - lastPulseTime > 200) {  // simple debounce
    pulseCount++;
    lastPulseTime = now;
    if (pulseCount == 1) trainStartTime = now;
  }
}

// Blink LED for visual pulse count
void blinkPulseCount(int count) {
  // *** TWEAK 4: This function now guarantees the LED is OFF when it exits ***
  for (int i = 0; i < count; i++) {
    digitalWrite(LED_PIN, LED_ON);
    delay(200);
    digitalWrite(LED_PIN, LED_OFF);
    delay(200);
  }
  // Ensure the final state is OFF
  digitalWrite(LED_PIN, LED_OFF); 
}

// Send email
void sendEmail() {
  // ... (Your email sending logic remains the same, it doesn't touch the LED)
  Serial.println("Sending email...");

  if (!smtp.connected()) {
    smtp.connect(&session);
    if (!smtp.connected()) {
      Serial.println("SMTP reconnect failed.");
      return;
    }
  }

  time_t now;
  struct tm timeinfo;
  time(&now);
  localtime_r(&now, &timeinfo);
  char timeStr[64];
  strftime(timeStr, sizeof(timeStr), "%H:%M:%S on %m-%d-%Y", &timeinfo);

  SMTP_Message message;
  message.sender.name = "ESP32 Mailbox";
  message.sender.email = AUTHOR_EMAIL;
  message.subject = "YOU'VE GOT MAIL";
  message.addRecipient("Recipient", RECIPIENT_EMAIL);

  String body = "Your mailbox was opened at ";
  body += timeStr;
  body += "\nYOU'VE GOT MAIL!";
  message.text.content = body.c_str();
  message.text.charSet = "us-ascii";
  message.text.transfer_encoding = Content_Transfer_Encoding::enc_7bit;

  if (!MailClient.sendMail(&smtp, &message, true)) {
    Serial.printf("Email error: %s\n", smtp.errorReason().c_str());
  } else {
    Serial.println("Email sent successfully!");
  }
}
