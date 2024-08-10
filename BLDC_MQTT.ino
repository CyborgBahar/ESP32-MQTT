#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <FastLED.h>
#include "HardwareSerial.h"
#include "DFRobotDFPlayerMini.h"


// Network credentials Here
//onst char* ssid = "sensors";
//const char* password = "Spd455Ym";

// MQTT Broker
const char* mqtt_broker = "broker.emqx.io";
const char* mqtt_username = "emqx";
const char* mqtt_password = "public";
const int mqtt_port = 1883;

const char* mqttStripFade = "strip_fade";
const char* mqttStripFade2 = "strip_fade2";
const char* mqtt_topic_color = "color_picker";
const char* mqtt_topic_color_off = "color_picker_off";     // Define a new topic for turning off color picker
const char* mqtt_topic_music = "music_control";            // Define a new topic for music control
const char* mqtt_topic_music_next = "music_next";          // Define a new topic for music control
const char* mqtt_topic_music_previous = "music_previous";  // Define a new topic for music control
const char* mqtt_topic_music_loop = "music_loop";
const char* mqtt_topic_music_random = "music_random";
const char* mqtt_topic_song_title = "song_title"; // Topic for song title

const int DATA_PIN1 = 12;
const int NUM_LEDS = 50;  // Number of LEDs in the strip
CRGB leds1[NUM_LEDS];

WiFiClient espClient;
PubSubClient client(espClient);

bool motorState = false;                      // Variable to store the state of the motor
bool motorRunning = false;                    // Tracks whether the motor is currently running or paused
bool ledPatternRunning = false;   
bool ledPattern2Running = false;               // Flag to control the LED pattern
unsigned long patternStartMillis = 0;         // Timer for pattern duration
const unsigned long patternDuration = 10000;  // Duration for the pattern in milliseconds

// Timing variables for motor control
unsigned long previousMillis = 0;  // Stores the last time the motor state was updated
const long runInterval = 500;     // Duration for which the motor will run (in milliseconds)
const long pauseInterval = 500;    // Duration for which the motor will pause (in milliseconds)

// Define the pins for RX2 and TX2
#define RX2_PIN 26
#define TX2_PIN 27

// Create a HardwareSerial object for UART2
HardwareSerial mySerial(2);

// Create the Player object
DFRobotDFPlayerMini player;
void printDetail(uint8_t type, int value);

void setup() {
  Serial.begin(115200);

  // Connecting to WiFi
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.println("Connecting to WiFi..");
  }
  Serial.println("Connected to the Wi-Fi network");

  // Connecting to MQTT broker
  client.setServer(mqtt_broker, mqtt_port);
  client.setCallback(callback);
  while (!client.connected()) {
    String client_id = "esp32-client-";
    client_id += String(WiFi.macAddress());
    Serial.printf("The client %s connects to the public MQTT broker\n", client_id.c_str());
    if (client.connect(client_id.c_str(), mqtt_username, mqtt_password)) {
      Serial.println("Public EMQX MQTT broker connected");
    } else {
      Serial.print("failed with state ");
      Serial.print(client.state());
      delay(2000);
    }
  }

  pinMode(MOTOR_PIN, OUTPUT);  // Set motor pin as output
  pinMode(DIR_PIN, OUTPUT);    // Set direction pin as output

  // Initialize FastLED strips
  FastLED.addLeds<NEOPIXEL, DATA_PIN1>(leds1, NUM_LEDS);
  FastLED.setMaxPowerInVoltsAndMilliamps(5, 500);
  FastLED.clear();
  FastLED.setBrightness(255);
  FastLED.show();

  // Ensure motor direction is always set to counterclockwise
  digitalWrite(DIR_PIN, LOW);

  // Publish and subscribe
  client.subscribe(mqttStripFade);
  client.subscribe(mqttStripFade2);
  client.subscribe(mqtt_topic_color_off);
  client.subscribe(mqtt_topic_color);
  client.subscribe(mqtt_topic_music);
  client.subscribe(mqtt_topic_music_next);
  client.subscribe(mqtt_topic_music_previous);
  client.subscribe(mqtt_topic_music_random);
  client.subscribe(mqtt_topic_music_loop);
  client.subscribe(mqtt_topic_song_title);

  // Initialize UART2 on pins RX2 and TX2
  mySerial.begin(9600, SERIAL_8N1, RX2_PIN, TX2_PIN);
  Serial.println("UART2 initialized");

  // Start communication with DFPlayer Mini
   if (player.begin(mySerial)) {
    Serial.println("DFPlayer Mini initialized successfully");

    // Check if SD card is present
    if (player.readType() == DFPlayerCardOnline) {
      Serial.println("SD card detected");

      // Set volume to a moderate level (0 to 30)
      int volumeLevel = 20;
      if (volumeLevel < 0) volumeLevel = 0;
      if (volumeLevel > 30) volumeLevel = 30;
      player.volume(volumeLevel);
      Serial.printf("Volume set to %d\n", volumeLevel);
    } else {
      Serial.println("No SD card detected");
    }
  } else {
    Serial.println("Connecting to DFPlayer Mini failed!");
  }
}

void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message received on topic: ");
  Serial.print(topic);
  Serial.print(". Message: ");
  String payloadStr;
  String message = String((char*)payload);
  for (int i = 0; i < length; i++)
    payloadStr += (char)payload[i];
  Serial.println(payloadStr);


  // Handle LED color topics
  if (strcmp(topic, mqtt_topic_color) == 0) {
    StaticJsonDocument<128> doc;  // Adjust the size based on your payload size
    DeserializationError error = deserializeJson(doc, message);
    if (error) {
      Serial.print("deserializeJson() failed: ");
      Serial.println(error.f_str());
      return;
    }
    // Extract RGB values
    int r = doc["r"];
    int g = doc["g"];
    int b = doc["b"];

    // Update LED strip with the received color
    for (int i = 0; i < NUM_LEDS; i++) {
      leds1[i] = CRGB(r, g, b);
    }
    FastLED.show();
  }

  else if (strcmp(topic, mqtt_topic_color_off) == 0) {
    // Turn off LED strip immediately
    turnOff();  // Function to turn off the LEDs
  }


  else if (strcmp(topic, mqttStripFade) == 0) {
    if (payload[0] == '1') {
      Serial.println("Received command to run LED fade");
      ledPatternRunning = true;
      patternStartMillis = millis();  // Start the pattern
    } else if (payload[0] == '0') {
      turnOff();                  // Turn off LEDs
      ledPatternRunning = false;  // Reset the flag
    }
  }

   else if (strcmp(topic, mqttStripFade2) == 0) {
    if (payload[0] == '1') {
      Serial.println("Received command to run LED fade 2 color");
      ledPattern2Running = true;
      patternStartMillis = millis();  // Start the pattern
    } else if (payload[0] == '0') {
      turnOff();                  // Turn off LEDs
      ledPattern2Running = false;  // Reset the flag
    }
  }

  // Handle music control
  else if (strcmp(topic, mqtt_topic_music) == 0) {
    int command = payloadStr.toInt();  // Convert the payload to an integer
    if (command == 1) {
      Serial.println("Playing music");
      player.play(1);  // Play the specified track
        publishSongTitle();
    } else if (command == 0) {
      Serial.println("Stopping music");
      player.pause();  // Pause the playback
    }
  }

   if (strcmp(topic, mqtt_topic_music_next) == 0) {
    int command = payloadStr.toInt();  // Convert the payload to an integer
    if (command == 1) {
      Serial.println("Playing next track");
      player.next();  // Play the specified track
      publishSongTitle();
    }
  } else if (strcmp(topic, mqtt_topic_music_previous) == 0) {
    int command = payloadStr.toInt();  // Convert the payload to an integer
    if (command == 1) {
      Serial.println("Playing previous track");
      player.previous();  // Play the specified track
      publishSongTitle();
    }
  }


  // Handle music control
  else if (strcmp(topic, mqtt_topic_music_loop) == 0) {
    int command = payloadStr.toInt();  // Convert the payload to an integer
    if (command == 1) {
      Serial.println("Loop enabled ");
      player.enableLoop();  // Play the specified track
    } else if (command == 0) {
      Serial.println("Loop disabled");
      player.disableLoop();  // Pause the playback
    }
  }


  // Handle music control
  else if (strcmp(topic, mqtt_topic_music_random) == 0) {
    int command = payloadStr.toInt();  // Convert the payload to an integer
    if (command == 1) {
      Serial.println("Randoming all ");
      player.randomAll();  // Play the specified track
    }
  }
}

void loop() {
  client.loop();  // Handle MQTT messages

  // Check if the LED pattern should be running
  if (ledPatternRunning) {
    fade();  // Update LED pattern
  }

 // Check if the LED pattern should be running
  if (ledPattern2Running) {
    fade2();  // Update LED pattern
  }

void White() {
  for (int i = 0; i < NUM_LEDS; i++) {
    leds1[i] = CRGB::White;
  }
  FastLED.show();
}

void turnOff() {
  for (int i = 0; i < NUM_LEDS; i++) {
    leds1[i] = CRGB(0, 0, 0);  // Set all LEDs to black (off)
  }
  FastLED.show();
}
void fade() {
  static unsigned long lastUpdateMillis = 0;  // Last update time
  static int brightness = 0;                  // Brightness level
  static int fadeDirection = 1;               // Fade direction (1 for up, -1 for down)

  unsigned long currentMillis = millis();

  // Update the pattern every 100 milliseconds
  if (currentMillis - lastUpdateMillis >= 200) {
    lastUpdateMillis = currentMillis;

    // Adjust brightness
    brightness += fadeDirection * 5;
    if (brightness >= 255 || brightness <= 0) {
      fadeDirection = -fadeDirection;  // Reverse the direction
      brightness = constrain(brightness, 0, 255);  // Ensure brightness stays within bounds
    }

    // Set all LEDs to the current brightness level of white
    for (int i = 0; i < NUM_LEDS; i++) {
      leds1[i] = CRGB(brightness, brightness, brightness);
    }
    FastLED.show();
  }
}


void fade2() {
  static unsigned long lastUpdateMillis = 0;  // Last update time
  static uint8_t hue = 0;                     // Starting hue

  unsigned long currentMillis = millis();

  // Update the pattern every 20 milliseconds
  if (currentMillis - lastUpdateMillis >= 100) {
    lastUpdateMillis = currentMillis;

    for (int i = 0; i < NUM_LEDS; i++) {
      leds1[i] = CHSV(hue, 255, 255);  // Set each LED to the current hue
    }
    FastLED.show();

    hue++;  // Increment the hue to create a smooth color transition
  }
}

void publishSongTitle() {
  int currentTrack = player.readCurrentFileNumber();
  String songTitle = "Track " + String(currentTrack); // Simplified; you might retrieve the actual song name here
  client.publish(mqtt_topic_song_title, songTitle.c_str());
}
