#include <DHT.h>
#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFiMulti.h>
#include <WebSocketsClient.h>
#include <timer.h>

#define WIFI_SSID "Paulo Sergio"
#define WIFI_PWD "pvfm1994"

#define WSS_ERROR_PIN 19

#define DHT_PIN 2
#define DHT_TYPE DHT11

// AWS_GATEWAY_API_ID.execute-api.AWS_REGION.amazonaws.com
#define WS_HOST "owmyj4aod8.execute-api.sa-east-1.amazonaws.com" // IP or domain
#define WS_PORT 443                                              // 443 for SSL
// the deviceId is arbitrary, but MUST be unique for each device
#define WS_PATH "/dev?clientType=device&deviceId=esp32-websocket-client" // Path to websocket
#define WS_FINGERPRINT ""                                                // Fingerprint
#define WS_PROTOCOL "wss"                                                // Protocol

#define JSON_DOC_SIZE 2048
#define MSG_SIZE 256

WiFiMulti wifiMulti;
WebSocketsClient wsClient;
uniuno::Timer timer;

DHT dht(DHT_PIN, DHT_TYPE);

// Send error message to the server.
void sendErrorMessage(const char *error)
{
  char msg[MSG_SIZE];

  sprintf(msg, "{\"action\":\"msg\",\"type\":\"error\",\"body\":\"%s\"}", error);

  wsClient.sendTXT(msg);
}

// Send confirmation of receipt.
void sendOkMessage()
{
  wsClient.sendTXT("{\"action\":\"msg\",\"type\":\"status\",\"body\":\"ok\"}");
}

void handlePaylod(uint8_t *payload)
{
  // Use https://arduinojson.org/v6/assistant to compute the capacity (in bytes).
  StaticJsonDocument<JSON_DOC_SIZE> doc;

  DeserializationError error = deserializeJson(doc, payload);

  // Test if parsing succeeds.
  if (error)
  {
    Serial.print(F("deserializeJson() failed: "));
    Serial.println(error.f_str());

    sendErrorMessage(error.c_str());
    return;
  }

  // These are common server errors. There may be others.
  if (doc["message"] == "Forbidden" || doc["message"] == "Internal server error")
  {
    digitalWrite(WSS_ERROR_PIN, HIGH);
    Serial.print("Error: ");
    Serial.println(doc["message"].as<String>());
    Serial.println("");
    delay(5000);
    return;
  }

  // DEBUGGING ONLY
  Serial.println(doc.as<String>());
  return;
}

void webSocketEvent(WStype_t type, uint8_t *payload, size_t length)
{
  switch (type)
  {
  case WStype_CONNECTED:
    Serial.printf("[WSc] Connected!\n");
    break;

  case WStype_DISCONNECTED:
    Serial.printf("[WSc] Disconnected!\n");
    break;

  case WStype_TEXT:
    Serial.printf("[WSc] Message received.\n");
    handlePaylod(payload);
    break;
  }
}

void sendDHTData()
{
  float humidity = dht.readHumidity();
  float tempCelsius = dht.readTemperature();

  if (isnan(humidity) || isnan(tempCelsius))
  {
    sendErrorMessage("Failed to read from DHT sensor!");
    delay(2000);
    return;
  }

  char msg[MSG_SIZE];

  sprintf(msg, "{\"action\":\"msg\",\"type\":\"sensor\",\"body\":{ \"name\":\"DHT11\", \"data\":[{\"label\":\"Humidity\",\"value\":%.2f, \"unit\":\"%%%\"},{\"label\":\"Temperature\",\"value\":%.2f, \"unit\":\"ºC\"}]}}", humidity, tempCelsius);

  Serial.println(msg);

  wsClient.sendTXT(msg);
}

void setup()
{
  // put your setup code here, to run once:
  Serial.begin(115200);
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(WSS_ERROR_PIN, OUTPUT);

  wifiMulti.addAP(WIFI_SSID, WIFI_PWD);

  Serial.println("\nConnecting to WiFi...");
  while (wifiMulti.run() != WL_CONNECTED)
  {
    digitalWrite(LED_BUILTIN, HIGH);
    delay(500);
    digitalWrite(LED_BUILTIN, LOW);
    delay(500);
    digitalWrite(LED_BUILTIN, HIGH);
    delay(500);
    digitalWrite(LED_BUILTIN, LOW);
    delay(500);
    digitalWrite(LED_BUILTIN, HIGH);
    delay(500);
    digitalWrite(LED_BUILTIN, LOW);
  }

  Serial.println("WiFi connected!");

  Serial.println(WiFi.localIP());

  wsClient.beginSSL(WS_HOST, WS_PORT, WS_PATH, WS_FINGERPRINT, WS_PROTOCOL); // Connect to the websocket server.
  wsClient.onEvent(webSocketEvent);                                          // Bind the event handler.
  dht.begin();                                                               // Initialize the DHT sensor.

  timer.set_interval(sendDHTData, 500); // Send DHT data every 500ms.
  timer.attach_to_loop();               // Attach the timer to the loop.
}

void loop()
{
  digitalWrite(LED_BUILTIN, wifiMulti.run() == WL_CONNECTED ? HIGH : LOW);
  wsClient.loop();
  timer.tick();
}