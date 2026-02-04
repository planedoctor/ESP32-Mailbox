#include <WiFi.h>
#include <ESP_Mail_Client.h>
#include <time.h>

// Wi-Fi credentials
#define WIFI_SSID "your_wifi_ssid"
#define WIFI_PASSWORD "your_wifi_password"
#define WIFI_TIMEOUT_MS 30000 

// SMTP settings
#define SMTP_HOST "smtp.gmail.com"
#define SMTP_PORT 465
#define AUTHOR_EMAIL "your_sender@gmail.com"
#define AUTHOR_PASSWORD "your_app_password"     // 16-char App Password from Google
#define RECIPIENT_EMAIL "your_recipient@gmail.com"

// I/O pins
#define INPUT_PIN 2
#define LED_PIN 15   // CORRECT Pin for Onboard LED on Xiao-ESP32C6

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
const char* TZ_INFO = "EST5EDT,M3.2.0/2,M11.1.0"; // Timezone (Eastern)

void blinkPulseCount(int count);
void sendEmail();
IRAM_ATTR void pulseISR();

// New helper function to ensure WiFi is alive
void verifyWiFi() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi lost. Reconnecting...");
    WiFi.disconnect();
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 10000) {
      delay(100);
    }
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LED_ON);  // *** LED ON during setup ***

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  unsigned long startAttemptTime = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < WIFI_TIMEOUT_MS) {
    // We keep the LED solidly ON while connecting
    delay(500); 
  }

  configTime(0, 0, "pool.ntp.org");
  setenv("TZ", TZ_INFO, 1);
  tzset();

  pinMode(INPUT_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(INPUT_PIN), pulseISR, FALLING);

  digitalWrite(LED_PIN, LED_OFF);
  Serial.println("System Ready.");
}

void loop() {
  // Check WiFi status occasionally
  static unsigned long lastWiFiCheck = 0;
  if (millis() - lastWiFiCheck > 60000) { // Check every minute
    verifyWiFi();
    lastWiFiCheck = millis();
  }
// Check if the pulse train is valid and we haven't sent an email yet
//  Currently set as true with 5-7 pulsecount change this variable
  if (pulseCount > 0 && (millis() - lastPulseTime > PULSE_TIMEOUT)) {
    if (pulseCount >= 5 && pulseCount <= 7 && !emailSent) {
      sendEmail();
      emailSent = true;
      blinkPulseCount(pulseCount); // *** blinkPulseCount function handles turning LED off at the end ***
    }
    pulseCount = 0;
    trainStartTime = 0;
  }

  if (emailSent && (millis() - lastPulseTime > TRAIN_TIMEOUT)) {
    emailSent = false;
  }
}

IRAM_ATTR void pulseISR() {
  unsigned long now = millis();
  if (now - lastPulseTime > 200) { 
    pulseCount++;
    lastPulseTime = now;
    if (pulseCount == 1) trainStartTime = now;
  }
}

void blinkPulseCount(int count) {
  // *** This function now guarantees the LED is OFF when it exits ***
  for (int i = 0; i < count; i++) {
    digitalWrite(LED_PIN, LED_ON); delay(200);
    digitalWrite(LED_PIN, LED_OFF); delay(200);
  }
}

void sendEmail() {
  verifyWiFi(); // Ensure connection right before sending
  
  ESP_Mail_Session session;
  session.server.host_name = SMTP_HOST;
  session.server.port = SMTP_PORT;
  session.login.email = AUTHOR_EMAIL;
  session.login.password = AUTHOR_PASSWORD;

  SMTP_Message message;
  message.sender.name = "ESP32 Mailbox";
  message.sender.email = AUTHOR_EMAIL;
  message.subject = "YOU'VE GOT MAIL";
  message.addRecipient("User", RECIPIENT_EMAIL);

  // Get current time
  time_t now;
  time(&now);
  struct tm timeinfo;
  localtime_r(&now, &timeinfo);
  char timeStr[64];
  strftime(timeStr, sizeof(timeStr), "%H:%M:%S on %m-%d-%Y", &timeinfo);

  String body = "Your mailbox was opened at ";
  body += timeStr;
  message.text.content = body.c_str();

  // Connect and Send
  if (!smtp.connect(&session)) {
    Serial.println("SMTP Connect Fail");
    return;
  }

  if (!MailClient.sendMail(&smtp, &message)) {
    Serial.println("Error sending Email: " + smtp.errorReason());
  } else {
    Serial.println("Email sent successfully!");
  }

  // CRITICAL: Clear the message and close the session to free memory
  message.clear();
  smtp.closeSession();
}
