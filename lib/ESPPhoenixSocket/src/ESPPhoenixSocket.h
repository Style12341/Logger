#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <WebSocketsClient.h>
#include "mbedtls/base64.h"
#include <ArduinoJson.h>
#include <functional>
#include <memory>

class PhoenixSocket
{
public:
  // Modern callback types using std::function
  using ConnectCallback = std::function<void()>;
  using ErrorCallback = std::function<void(const String &)>;
  using CloseCallback = std::function<void(uint16_t)>;
  using MessageCallback = std::function<void(const String &, const String &, const JsonObject &)>;

  // Constructor using const references for strings
  PhoenixSocket(const String &server, uint16_t port, const String &path)
      : _server(server), _port(port), _path(path), _ref_counter(0)
  {
    instance_ = this;
  }

  // Rule of five (C++11 version)
  ~PhoenixSocket() = default;
  PhoenixSocket(const PhoenixSocket &) = delete;
  PhoenixSocket &operator=(const PhoenixSocket &) = delete;
  PhoenixSocket(PhoenixSocket &&) = delete;
  PhoenixSocket &operator=(PhoenixSocket &&) = delete;

  void begin()
  {
    _webSocket.begin(_server.c_str(), _port, _path.c_str());
    _webSocket.onEvent(webSocketEvent);
    _webSocket.setReconnectInterval(5000);
    _webSocket.enableHeartbeat(15000, 3000, 2);
  }

  void loop()
  {
    _webSocket.loop();
  }

  // Method signatures using const references
  void joinChannel(const String &topic, const String &payload)
  {
    sendJoinMessage(topic, payload);
  }

  void sendEvent(const String &topic, const String &event, const String &payload)
  {
    sendJsonMessage(topic, event, payload);
  }

  // Callback setters
  void onConnect(ConnectCallback callback) { _onConnectCallback = std::move(callback); }
  void onError(ErrorCallback callback) { _onErrorCallback = std::move(callback); }
  void onClose(CloseCallback callback) { _onCloseCallback = std::move(callback); }
  void onMessage(MessageCallback callback) { _onMessageCallback = std::move(callback); }

private:
  WebSocketsClient _webSocket;
  String _server;
  uint16_t _port;
  String _path;
  uint32_t _ref_counter;

  ConnectCallback _onConnectCallback;
  ErrorCallback _onErrorCallback;
  CloseCallback _onCloseCallback;
  MessageCallback _onMessageCallback;

  static PhoenixSocket *instance_;

  static void webSocketEvent(WStype_t type, uint8_t *payload, size_t length)
  {
    if (!instance_)
      return;
    instance_->handleWebSocketEvent(type, payload, length);
  }

  void handleWebSocketEvent(WStype_t type, uint8_t *payload, size_t length)
  {
    switch (type)
    {
    case WStype_DISCONNECTED:
      if (_onCloseCallback)
        _onCloseCallback(1000);
      break;
    case WStype_CONNECTED:
      if (_onConnectCallback)
        _onConnectCallback();
      break;
    case WStype_TEXT:
      handleIncomingMessage(reinterpret_cast<char *>(payload));
      break;
    case WStype_ERROR:
      if (_onErrorCallback)
        _onErrorCallback("WebSocket error occurred");
      break;
    default:
      break;
    }
  }

  void handleIncomingMessage(const char *payload)
  {
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, payload);

    if (error)
    {
      if (_onErrorCallback)
      {
        _onErrorCallback(String("JSON parse error: ") + error.c_str());
      }
      return;
    }

    const char *topic = doc["topic"];
    const char *event = doc["event"];
    JsonObject messagePayload = doc["payload"];

    if (_onMessageCallback && topic && event && strcmp(event, "phx_reply") != 0)
    {
      _onMessageCallback(String(topic), String(event), messagePayload);
    }
  }

  void sendJoinMessage(const String &topic, const String &payload)
  {
    char message[1024];
    snprintf(message, sizeof(message), "{\"topic\":\"%s\",\"event\":\"phx_join\",\"ref\":\"%u\",\"payload\":%s}", topic.c_str(), getNextRef(), payload.c_str());
    _webSocket.sendTXT(message);
  }

  void sendJsonMessage(const String &topic, const String &event, const String &payload)
  {
    char message[1024];
    snprintf(message, sizeof(message), "{\"topic\":\"%s\",\"event\":\"%s\",\"ref\":\"%u\",\"payload\":%s}", topic.c_str(), event.c_str(), getNextRef(), payload.c_str());

    _webSocket.sendTXT(message);
  }

  uint32_t getNextRef()
  {
    return ++_ref_counter;
  }
};