#include <Arduino.h>

// libs
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <array>
#include <optional>
#include <FastLED.h>

#define DEBUG

HTTPClient http;

int retry = 0;
uint64_t lastTime = 0;

JsonDocument doc;

bool backDoorLocked = false;
bool frontDoorLocked = false;
bool w2Locked = false;

bool w1ajar = false;
bool w2ajar = false;

#define LED_PIN 5
#define LED_PER_DINGSBUMS 3
#define DINGSBUMS_COUNT 2
#define NUM_LEDS LED_PER_DINGSBUMS * DINGSBUMS_COUNT

#define VORNE_INDEX 0
#define HINTEN_INDEX 1

std::array<CRGB, NUM_LEDS> dingsbums;

void setDingsbums(int dingsbumsIndex, CRGB color)
{
  // Serial.printf("Setting dingsbums %d to color %d %d %d\n", dingsbumsIndex, color.r, color.g, color.b);

  for (int i = 0; i < LED_PER_DINGSBUMS; i++)
  {
    const auto idx = dingsbumsIndex * LED_PER_DINGSBUMS + i;
    assert(idx < NUM_LEDS);

    dingsbums[idx] = color;
  }
}

void setup()
{
  FastLED.addLeds<NEOPIXEL, LED_PIN>(dingsbums.data(), NUM_LEDS);

  std::fill(dingsbums.begin(), dingsbums.end(), CRGB::White);
  FastLED.show();

  Serial.begin(115200);

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  WiFi.setHostname("dingsbums");
  WiFi.setAutoReconnect(true);
  WiFi.persistent(true);

  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.printf("Connecting to WiFi %s...\n", WIFI_SSID);
  }

  Serial.println("Connected to the WiFi network");

  FastLED.setBrightness(255);
}

void loop()
{
  // if not connected to wifi, try to connect
  if (WiFi.status() != WL_CONNECTED)
  {
    std::fill(dingsbums.begin(), dingsbums.end(), CRGB::White);
    FastLED.show();

    if (retry > 5)
    {
      delay(5000);
      ESP.restart();
      return;
    }

    Serial.println("Connecting to WiFi...");
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    // wait 5 seconds
    delay(5000);
    retry++;
  }

  if (lastTime == 0 || millis() - lastTime > 1000)
  {
    http.begin("http://realraum.at/status.json");
    http.setTimeout(200);
    int httpCode = http.GET();

    lastTime = millis();

    if (httpCode > 0)
    {
      if (const auto error = deserializeJson(doc, http.getString()); error)
      {
        Serial.print("deserializeJson() failed: ");
        Serial.println(error.c_str());
        return;
      }

      // Serial.println("HTTP request OK");

      // x.sensors.door_locked[4].value = backdoorLocked
      // x.sensors.door_locked[3].value = frontdoorLocked
      // x.sensors.door_locked[2].value = w2Locked (niemand in w2)

      backDoorLocked = doc["sensors"]["door_locked"][4]["value"];
      frontDoorLocked = doc["sensors"]["door_locked"][3]["value"];
      w2Locked = doc["sensors"]["door_locked"][2]["value"];

      // w2 ajar = x.sensors.ext_door_ajar[0].value
      w2ajar = !doc["sensors"]["ext_door_ajar"][0]["value"];

      // w1 ajar = !x.sensors.ext_door_ajar[4].value || !x.sensors.ext_door_ajar[5].value
      w1ajar = !doc["sensors"]["ext_door_ajar"][4]["value"] || !doc["sensors"]["ext_door_ajar"][5]["value"];
    }
    else
    {
      Serial.println("Error on HTTP request");
    }

    http.end();

#ifdef DEBUG
    Serial.printf("Backdoor: %s\n", backDoorLocked ? "locked" : "unlocked");
    Serial.printf("Frontdoor: %s\n", frontDoorLocked ? "locked" : "unlocked");
    Serial.printf("W2: %s\n", w2Locked ? "locked" : "unlocked");
    Serial.printf("W2 ajar: %s\n", w2ajar ? "yes" : "no");
#endif
  }

  bool blink = millis() % 300 < 150;

  std::fill(dingsbums.begin(), dingsbums.end(), CRGB::Black);

  // w2 soll das machen: wenn alles zu, rot. sonst grün
  if (w2ajar) {
    setDingsbums(HINTEN_INDEX, blink ? CRGB::Blue : CRGB::Black);
  } else if (w2Locked) {
    setDingsbums(HINTEN_INDEX, CRGB::Red);
  } else {
    setDingsbums(HINTEN_INDEX, CRGB::Green);
  }

  if (w1ajar) {
    setDingsbums(VORNE_INDEX, blink ? CRGB::Blue : CRGB::Black);
  } else if (frontDoorLocked && !backDoorLocked) {
    setDingsbums(VORNE_INDEX, CRGB::Orange);
  } else if (frontDoorLocked && backDoorLocked) {
    setDingsbums(VORNE_INDEX, CRGB::Red);
  } else {
    setDingsbums(VORNE_INDEX, CRGB::Green);
  }

  FastLED.show();
}