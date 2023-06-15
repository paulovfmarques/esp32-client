#include <DHT.h>
#include <esp_wifi.h>
#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFiMulti.h>
#include <WebSocketsClient.h>
#include <timer.h>

// Definição das constantes utilizadas para conexão Wi-Fi e websocket
#define WIFI_SSID "Paulo Sergio"
#define WIFI_PWD "pvfm1994"

#define LED_PIN_15 15

#define DHT_PIN 2
#define DHT_TYPE DHT11

#define WS_HOST "d441ny46de.execute-api.sa-east-1.amazonaws.com" // IP ou domínio
#define WS_PORT 443                                              // 443 para SSL
#define WS_PATH "/dev?clientType=device&deviceId="               // Caminho para websocket
#define WS_FINGERPRINT ""                                        // Impressão digital
#define WS_PROTOCOL "wss"                                        // Protocolo

#define JSON_DOC_SIZE 2048
#define MSG_SIZE 256

// Instanciação dos objetos
WiFiMulti wifiMulti;
WebSocketsClient wsClient;
uniuno::Timer timer;

DHT dht(DHT_PIN, DHT_TYPE);

// Função para enviar uma mensagem de erro ao servidor
void sendErrorMessage(const char *error)
{
  char msg[MSG_SIZE];

  sprintf(msg, "{\"action\":\"msg\",\"type\":\"error\",\"body\":\"%s\"}", error);

  wsClient.sendTXT(msg);
}

// Função para tratar o payload recebido
void IRAM_ATTR handlePaylod(uint8_t *payload)
{
  // Criação do documento JSON para armazenar o payload
  StaticJsonDocument<JSON_DOC_SIZE> doc;

  // Desserialização do payload
  DeserializationError error = deserializeJson(doc, payload);

  // Verificação se houve erro na deserialização
  if (error)
  {
    sendErrorMessage(error.c_str());
    return;
  }

  // Verificação se houve algum erro comum do servidor
  if (doc["message"] == "Forbidden" || doc["message"] == "Internal server error")
  {
    sendErrorMessage(doc["message"].as<String>().c_str());
    delay(5000);
    return;
  }

  // Verificação da ação
  const char *type = doc["type"];
  if (type == nullptr)
  {
    // A ação não foi especificada. Trate isso como um erro.
    sendErrorMessage("Action not specified");
    return;
  }

  // Se a ação for "toggle_led", leia o ledPin da mensagem
  if (strcmp(type, "toggle_led") == 0)
  {
    int ledPin = doc["ledPin"];
    if (ledPin <= 0)
    {
      sendErrorMessage("Invalid LED pin");
      return;
    }

    // Alternar o estado do LED.
    bool ledState = digitalRead(ledPin);
    digitalWrite(ledPin, !ledState);

    // Verificar se a operação digitalWrite foi bem-sucedida.
    bool newLedState = digitalRead(ledPin);

    if (newLedState != !ledState)
    {
      sendErrorMessage("Failed to toggle LED");
      return;
    }

    // Enviar o estado atualizado do LED de volta para a interface do usuário.
    char msg[MSG_SIZE];

    sprintf(msg, "{\"action\":\"msg\",\"type\":\"led_state\",\"body\":{ \"ledPin\":%d, \"state\":\"%s\"}}", ledPin, newLedState ? "on" : "off");

    wsClient.sendTXT(msg);
  }
}

// Função para tratar eventos de websocket
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

void IRAM_ATTR notifyLedState()
{
  bool currentLEDState = digitalRead(LED_PIN_15);

  char msg[MSG_SIZE];
  sprintf(msg, "{\"action\":\"msg\",\"type\":\"led_state\",\"body\":{ \"ledPin\":%d, \"state\":\"%s\"}}", LED_PIN_15, currentLEDState ? "on" : "off");

  wsClient.sendTXT(msg);
}

// Função para enviar dados do DHT
void IRAM_ATTR sendDHTData()
{
  float humidity = dht.readHumidity();
  float tempCelsius = dht.readTemperature();

  // Verificação se a leitura foi realizada corretamente
  if (isnan(humidity) || isnan(tempCelsius))
  {
    sendErrorMessage("Failed to read from DHT sensor!");
    delay(2000);
    return;
  }

  // Criação da mensagem com os dados do sensor
  char msg[MSG_SIZE];

  sprintf(msg, "{\"action\":\"msg\",\"type\":\"sensor\",\"body\":{ \"name\":\"DHT11\", \"data\":[{\"label\":\"Humidity\",\"value\":%.2f, \"unit\":\"%%%\"},{\"label\":\"Temperature\",\"value\":%.2f, \"unit\":\"ºC\"}]}}", humidity, tempCelsius);

  wsClient.sendTXT(msg);
}

// Função de configuração
void setup()
{
  // Início da comunicação serial
  Serial.begin(115200);

  // Definição dos pinos como saída
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(LED_PIN_15, OUTPUT);

  // Adição do ponto de acesso Wi-Fi
  wifiMulti.addAP(WIFI_SSID, WIFI_PWD);

  Serial.println("\nConnecting to WiFi...");
  while (wifiMulti.run() != WL_CONNECTED)
  {
    digitalWrite(LED_BUILTIN, HIGH);
    delay(500);
    digitalWrite(LED_BUILTIN, LOW);
  }

  Serial.println("WiFi connected!");

  // Impressão do IP local
  Serial.println(WiFi.localIP());

  // Obtenção do ID do dispositivo
  uint64_t chipid = ESP.getEfuseMac();
  String deviceId = String((uint16_t)(chipid >> 32), HEX) + String((uint32_t)chipid, HEX);
  deviceId.toUpperCase();

  // Criação do caminho final para o websocket
  String finalPath = String(WS_PATH) + deviceId;

  delay(1000);

  // Início da conexão com o servidor websocket
  wsClient.beginSSL(WS_HOST, WS_PORT, finalPath, WS_FINGERPRINT, WS_PROTOCOL);

  delay(1000);

  // Vinculação do manipulador de eventos
  wsClient.onEvent(webSocketEvent);

  // Início do sensor DHT
  dht.begin();

  // Definição do intervalo para envio dos dados do DHT
  timer.set_interval(sendDHTData, 1000);

  // Definição do intervalo para envio do estado do LED
  timer.set_interval(notifyLedState, 750);

  // Vinculação do timer ao loop
  timer.attach_to_loop();
}

// Função de loop
void loop()
{
  digitalWrite(LED_BUILTIN, wifiMulti.run() == WL_CONNECTED ? HIGH : LOW);
  wsClient.loop();
  timer.tick();
}
