#include <Arduino.h>
#include <WiFi.h>
#include <WebSocketsClient.h>
#include "mbedtls/base64.h"
#include <ArduinoJson.h>

class PhoenixSocket
{
public:
  typedef std::function<void()> ConnectCallback;
  typedef std::function<void(const char *)> ErrorCallback;
  typedef std::function<void(uint16_t)> CloseCallback;
  typedef std::function<void(const char *, const char *, const JsonObject &)> MessageCallback;

  PhoenixSocket(const char *server, uint16_t port, const char *path);
  void begin();
  void loop();
  void joinChannel(const char *topic, const char *token);
  void sendEvent(const char *topic, const char *event, const char *payload);
  void sendBinary(const char *topic, const char *event, double value);

  void onConnect(ConnectCallback callback) { onConnectCallback = callback; }
  void onError(ErrorCallback callback) { onErrorCallback = callback; }
  void onClose(CloseCallback callback) { onCloseCallback = callback; }
  void onMessage(MessageCallback callback) { onMessageCallback = callback; }

private:
  WebSocketsClient webSocket;
  const char *server;
  uint16_t port;
  const char *path;

  ConnectCallback onConnectCallback;
  ErrorCallback onErrorCallback;
  CloseCallback onCloseCallback;
  MessageCallback onMessageCallback;

  static void webSocketEvent(WStype_t type, uint8_t *payload, size_t length);
  void sendJoinMessage(const char *topic, const char *token);
  void sendJsonMessage(const char *topic, const char *event, const char *payload);
  void handleIncomingMessage(const char *payload);

  static PhoenixSocket *instance;
};

PhoenixSocket *PhoenixSocket::instance = nullptr;

PhoenixSocket::PhoenixSocket(const char *server, uint16_t port, const char *path)
    : server(server), port(port), path(path)
{
  instance = this;
}

void PhoenixSocket::begin()
{
  webSocket.begin(server, port, path);
  webSocket.onEvent(webSocketEvent);
  webSocket.setReconnectInterval(5000);
}

void PhoenixSocket::loop()
{
  webSocket.loop();
}

void PhoenixSocket::webSocketEvent(WStype_t type, uint8_t *payload, size_t length)
{
  if (instance == nullptr)
    return;

  switch (type)
  {
  case WStype_DISCONNECTED:
    if (instance->onCloseCallback)
    {
      instance->onCloseCallback(1000); // Assuming normal closure
    }
    break;
  case WStype_CONNECTED:
    if (instance->onConnectCallback)
    {
      instance->onConnectCallback();
    }
    break;
  case WStype_TEXT:
    instance->handleIncomingMessage((const char *)payload);
    break;
  case WStype_BIN:
    Serial.printf("Received binary message of length %u\n", length);
    break;
  case WStype_ERROR:
    if (instance->onErrorCallback)
    {
      instance->onErrorCallback("WebSocket error occurred");
    }
    break;
  default:
    break;
  }
}

void PhoenixSocket::handleIncomingMessage(const char *payload)
{
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, payload);

  if (error)
  {
    Serial.print("deserializeJson() failed: ");
    Serial.println(error.c_str());
    return;
  }

  const char *topic = doc["topic"];
  const char *event = doc["event"];
  JsonObject messagePayload = doc["payload"];

  if (onMessageCallback && strcmp(event, "phx_reply") != 0)
  {
    onMessageCallback(topic, event, messagePayload);
  }
}

void PhoenixSocket::joinChannel(const char *topic, const char *token)
{
  sendJoinMessage(topic, token);
}

void PhoenixSocket::sendJoinMessage(const char *topic, const char *token)
{
  char buffer[256];
  snprintf(buffer, sizeof(buffer),
           "{\"topic\":\"%s\",\"event\":\"phx_join\",\"payload\":{\"token\":\"%s\"},\"ref\":\"1\"}",
           topic, token);
  webSocket.sendTXT(buffer);
}

void PhoenixSocket::sendEvent(const char *topic, const char *event, const char *payload)
{
  sendJsonMessage(topic, event, payload);
}

void PhoenixSocket::sendJsonMessage(const char *topic, const char *event, const char *payload)
{
  char buffer[256];
  snprintf(buffer, sizeof(buffer),
           "{\"topic\":\"%s\",\"event\":\"%s\",\"payload\":%s,\"ref\":\"1\"}",
           topic, event, payload);
  webSocket.sendTXT(buffer);
}

void PhoenixSocket::sendBinary(const char *topic, const char *event, double value)
{
  uint8_t binaryData[sizeof(double)];
  memcpy(binaryData, &value, sizeof(double));

  size_t encodedLength;
  unsigned char encodedData[12];
  mbedtls_base64_encode(encodedData, sizeof(encodedData), &encodedLength, binaryData, sizeof(double));

  char jsonMessage[256];
  snprintf(jsonMessage, sizeof(jsonMessage),
           "{\"topic\":\"%s\",\"event\":\"%s\",\"ref\":\"1\",\"payload\":{\"value\":\"%s\"}}",
           topic, event, encodedData);

  webSocket.sendTXT(jsonMessage);
}