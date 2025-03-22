#include <Arduino.h>

#define VARIANT_HTTP 1
#define VARIANT_MQTT 2

#define VARIANT VARIANT_MQTT

#if VARIANT == VARIANT_HTTP
#define USE_HTTP
#elif VARIANT == VARIANT_MQTT
#define USE_MQTT
#endif

// libs
#include <ArduinoJson.h>
#ifdef USE_HTTP
#include <HTTPClient.h>
#endif
#ifdef USE_MQTT
#include <PubSubClient.h>
#endif
#include <WiFi.h>
#include <array>
#include <optional>
#include <FastLED.h>

#define DEBUG

#ifdef USE_HTTP
HTTPClient http;

uint64_t lastTime = 0;
#endif

#ifdef USE_MQTT
WiFiClient espClient;
PubSubClient client(espClient);

#define MQTT_TOPIC "realraum/sperrzu/online"
#define ONLINE_PAYLOAD "{\"online\":true}"
#define OFFLINE_PAYLOAD "{\"online\":false}"
#define MQTT_SERVER "mqtt.realraum.at"
#endif

int retry = 0;

std::optional<uint64_t> millisBlinkingStarted = 0;

JsonDocument doc;

bool backDoorLocked = false;
bool frontDoorLocked = false;
bool w2Locked = false;

bool w1frontAjar = false;
bool w1backAjar = false;
bool w2ajar = false;

#define LED_PIN 5
#define LED_PER_DINGSBUMS 3
#define DINGSBUMS_COUNT 2
#define NUM_LEDS LED_PER_DINGSBUMS * DINGSBUMS_COUNT

#define VORNE_INDEX 0
#define HINTEN_INDEX 1

std::array<CRGB, NUM_LEDS> dingsbums;

#ifdef USE_MQTT

void callback(char* topic, byte* payload, unsigned int length)
{
  // json parse
  String payloadStr;

  for (int i = 0; i < length; i++)
  {
    payloadStr += (char)payload[i];
  }

  Serial.printf("Message arrived [%s] %s\n", topic, payloadStr.c_str());

  doc.clear();

  if (const auto error = deserializeJson(doc, payloadStr); error)
  {
    Serial.print("deserializeJson() failed: ");
    Serial.println(error.c_str());
    return;
  }

  // ==== MQTT ====
  // backDoorLocked: topic: "realraum/backdoorcx/lock", from payload: x.Locked
  // frontDoorLocked: topic: "realraum/frontdoor/lock", from payload: x.Locked
  // w2Locked: topic: "realraum/w2frontdoor/lock", from payload: x.Locked

  // w1ajar-0: topic: "realraum/frontdoor/ajar", from payload: x.Shut
  // w1ajar-1: topic: "realraum/backdoorcx/ajar", from payload: x.Shut
  // w2ajar-0: topic: "realraum/w2frontdoor/ajar", from payload: x.Shut
  
  String topicStr = topic;

  #define TOPIC_CASE(match, expr) \
    if (strcmp(topic, match) == 0) { \
      Serial.printf("Matched topic: %s\n", match); \
      expr; \
      return; \
    }

  TOPIC_CASE("realraum/backdoorcx/lock", backDoorLocked = doc["Locked"]);
  TOPIC_CASE("realraum/frontdoor/lock", frontDoorLocked = doc["Locked"]);
  TOPIC_CASE("realraum/w2frontdoor/lock", w2Locked = doc["Locked"]);
  TOPIC_CASE("realraum/frontdoor/ajar", w1frontAjar = doc["Shut"]);
  TOPIC_CASE("realraum/backdoorcx/ajar", w1backAjar = doc["Shut"]);
  TOPIC_CASE("realraum/w2frontdoor/ajar", w2ajar = doc["Shut"]);

  #undef TOPIC_CASE

  Serial.printf("Unknown topic: %s\n", topic);
}

#endif

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
  WiFi.setAutoReconnect(false);
  WiFi.persistent(false);

  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.printf("Connecting to WiFi %s...\n", WIFI_SSID);
  }

  Serial.println("Connected to the WiFi network");

  FastLED.setBrightness(255);

#ifdef USE_MQTT
  client.setServer(MQTT_SERVER, 1883);
  client.setCallback(callback);
#endif
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

    // get err str
    wl_status_t err = WiFi.status();

    Serial.printf("WiFi not connected, status: %d\n", err);

    Serial.printf("Connecting to WiFi \"%s\"...\n", WIFI_SSID);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    // wait 5 seconds
    delay(5000);
    retry++;
  }

#ifdef USE_MQTT
  if (!client.connected())
  {
    Serial.println("Connecting to MQTT...");

    while (!client.connected())
    {
      std::fill(dingsbums.begin(), dingsbums.end(), CRGB::White);
      FastLED.show();

      Serial.println("Connecting to MQTT...");

      if (client.connect("dingsbums", MQTT_TOPIC, 0, true, OFFLINE_PAYLOAD))
      {
        Serial.println("Connected to MQTT");

        client.subscribe("realraum/backdoorcx/lock");
        client.subscribe("realraum/frontdoor/lock");
        client.subscribe("realraum/w2frontdoor/lock");
        client.subscribe("realraum/frontdoor/ajar");
        client.subscribe("realraum/backdoorcx/ajar");
        client.subscribe("realraum/w2frontdoor/ajar");

        client.publish(MQTT_TOPIC, ONLINE_PAYLOAD);
      }
      else
      {
        Serial.print("failed with state ");
        Serial.print(client.state());
        delay(2000);
      }
    }
  }

  client.loop();
#endif

#ifdef USE_HTTP
  if (lastTime == 0 || millis() - lastTime > 1000)
  {
    http.begin("http://realraum.at/status.json");
    http.setTimeout(1000);
    int httpCode = http.GET();

    lastTime = millis();

    if (httpCode > 0)
    {
      doc.clear();

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
      w2ajar = doc["sensors"]["ext_door_ajar"][0]["value"];

      // w1 ajar = !x.sensors.ext_door_ajar[4].value || !x.sensors.ext_door_ajar[5].value
      w1frontAjar = doc["sensors"]["ext_door_ajar"][4]["value"];
      w1backAjar = doc["sensors"]["ext_door_ajar"][5]["value"];
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
#endif

  bool stayOn = millisBlinkingStarted && millis() - millisBlinkingStarted.value() > 5000;

  bool irgendwasBlinktlol = false;

  bool blink = stayOn || (millis() % 300 < 150);

  std::fill(dingsbums.begin(), dingsbums.end(), CRGB::Black);

  Serial.printf("w2ajar: %d, w2Locked: %d, w1frontAjar: %d, w1backAjar: %d, frontDoorLocked: %d, backDoorLocked: %d\n", w2ajar, w2Locked, w1frontAjar, w1backAjar, frontDoorLocked, backDoorLocked);

  // w2 soll das machen: wenn alles zu, rot. sonst grün
  if (!w2ajar) {
    setDingsbums(HINTEN_INDEX, blink ? CRGB::Blue : CRGB::Black);
    irgendwasBlinktlol = true;
  } else if (w2Locked) {
    setDingsbums(HINTEN_INDEX, CRGB::Red);
  } else {
    setDingsbums(HINTEN_INDEX, CRGB::Green);
  }

  if (!w1frontAjar || !w1backAjar) {
    setDingsbums(VORNE_INDEX, blink ? CRGB::Blue : CRGB::Black);
    irgendwasBlinktlol = true;
  } else if (frontDoorLocked && !backDoorLocked) {
    setDingsbums(VORNE_INDEX, CRGB::Orange);
  } else if (frontDoorLocked && backDoorLocked) {
    setDingsbums(VORNE_INDEX, CRGB::Red);
  } else {
    setDingsbums(VORNE_INDEX, CRGB::Green);
  }

  if (irgendwasBlinktlol && !millisBlinkingStarted) {
    millisBlinkingStarted = millis();
  } else if (!irgendwasBlinktlol && millisBlinkingStarted) {
    millisBlinkingStarted = std::nullopt;
  }

  FastLED.show();
}