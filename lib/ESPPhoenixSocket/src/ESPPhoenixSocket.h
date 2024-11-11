#pragma once
#ifndef ESPPhoenixSocket_h
#define ESPPhoenixSocket_h
#include <Arduino.h>
#include <WiFi.h>
#include <WebSocketsClient.h>
#include <ArduinoJson.h>
// #define DEBUG_LOGGER
#ifdef DEBUG_LOGGER
#define DL_LOG(format, ...) Serial.printf(format "\n", ##__VA_ARGS__)
#define DL_SerialBegin(args...) Serial.begin(args)
#else
#define DL_LOG(...)
#define DL_SerialBegin(...)
#endif
#define RECONNECT_TIME 5000
constexpr const char *MESSAGE_STRING = "{\"topic\":\"%s\",\"event\":\"%s\",\"ref\":\"%u\",\"payload\":%s}";
class PhoenixSocket
{
public:
  // Modern callback types using std::function
  using ConnectCallback = std::function<void()>;
  using ErrorCallback = std::function<void(const String &)>;
  using DisconnectCallback = std::function<void(uint16_t)>;
  using MessageCallback = std::function<void(const String &, const String &, const JsonDocument)>;
  using ReplyCallback = std::function<void(const String &, const String &, const JsonDocument)>;

  // Constructor using const references for strings
  PhoenixSocket(const String &server, uint16_t port, const String &path)
  {
    _server = server;
    _port = port;
    _path = path;
    _ref_counter = 0;
    _instance = this;
  }

  // Rule of five (C++11 version)
  ~PhoenixSocket() = default;
  PhoenixSocket(const PhoenixSocket &) = delete;
  PhoenixSocket &operator=(const PhoenixSocket &) = delete;
  PhoenixSocket(PhoenixSocket &&) = delete;
  PhoenixSocket &operator=(PhoenixSocket &&) = delete;

  void begin()
  {
    DL_LOG("[ESPPhoenixSocket] Beginning websocket connection %s/%d/%s", _server.c_str(), _port, _path.c_str());
    _webSocket.begin(_server.c_str(), _port, _path.c_str(), "phoenix");
    _webSocket.onEvent(_webSocketEvent);
    _webSocket.setReconnectInterval(RECONNECT_TIME);
  }
  void beginAuthorized(const String &token)
  {
    DL_LOG("[ESPPhoenixSocket] Beginning authorized websocket connection %s/%d/%s", _server.c_str(), _port, _path.c_str());
    _webSocket.begin(_server.c_str(), _port, _path.c_str(), "phoenix");
    DL_LOG("[ESPPhoenixSocket] Setting extra headers token: %s", token.c_str());
    const char *token_c = String("x-token: " + token).c_str();
    _webSocket.setExtraHeaders(token_c);
    _webSocket.onEvent(_webSocketEvent);
    _webSocket.setReconnectInterval(RECONNECT_TIME);
  }
  bool isConnected()
  {
    return _webSocket.isConnected();
  }
  void loop()
  {
    _webSocket.loop();
    if (millis() % 30000 == 0)
    {
      DL_LOG("[ESPPhoenixSocket] Sending heartbeat");
      _sendHeartbeat();
    }
  }

  // Method signatures using const references
  uint32_t joinChannel(const String &topic, const String &payload)
  {
    DL_LOG("[ESPPhoenixSocket] Sending join channel request on:\n\tTopic: %s\n\tPayload %s", topic.c_str(), payload.c_str());
    return _sendJoinMessage(topic, payload);
  }

  void sendEvent(const String &topic, const String &event, const String &payload = "")
  {
    DL_LOG("[ESPPhoenixSocket] Sending event on:\n\tTopic: %s\n\tEvent: %s\n\tPayload %s", topic, event, payload);
    _sendJsonMessage(topic, event, payload);
  }

  // Callback setters
  void onConnect(ConnectCallback callback) { _onConnectCallback = std::move(callback); }
  void onError(ErrorCallback callback) { _onErrorCallback = std::move(callback); }
  void onClose(DisconnectCallback callback) { _onDisconnectCallback = std::move(callback); }
  void onMessage(MessageCallback callback) { _onMessageCallback = std::move(callback); }
  void onReply(ReplyCallback callback) { _onReplyCallback = std::move(callback); }

private:
  WebSocketsClient _webSocket;
  String _server;
  uint16_t _port;
  String _path;
  uint32_t _ref_counter;

  ConnectCallback _onConnectCallback;
  ErrorCallback _onErrorCallback;
  DisconnectCallback _onDisconnectCallback;
  MessageCallback _onMessageCallback;
  ReplyCallback _onReplyCallback;

  static PhoenixSocket *_instance;

  static void _webSocketEvent(WStype_t type, uint8_t *payload, size_t length)
  {
    if (!_instance)
      return;
    _instance->_handleWebSocketEvent(type, payload, length);
  }

  void _handleWebSocketEvent(WStype_t type, uint8_t *payload, size_t length)
  {
    switch (type)
    {
    case WStype_DISCONNECTED:
      if (_onDisconnectCallback)
        _onDisconnectCallback(1000);
      break;
    case WStype_CONNECTED:
      if (_onConnectCallback)
        _onConnectCallback();
      break;
    case WStype_TEXT:
      _handleIncomingMessage(reinterpret_cast<char *>(payload));
      break;
    case WStype_ERROR:
      if (_onErrorCallback)
        _onErrorCallback("Websocket error");
      break;
    default:
      break;
    }
  }

  void _handleIncomingMessage(const char *payload)
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

    if (_onMessageCallback && topic && event)
    {
      if (strcmp(event, "phx_reply") == 0)
      {
        _onReplyCallback(String(topic), String(event), messagePayload);
      }
      else
      {
        _onMessageCallback(String(topic), String(event), messagePayload);
      }
    }
  }

  uint32_t _sendJoinMessage(const String &topic, const String &payload)
  {
    char message[1024];
    uint32_t ref = encodeMessageString(message, sizeof(message), topic.c_str(), "phx_join", payload.c_str());
    DL_LOG("[ESPPhoenixSocket] Sending raw message as \n\t\t%s\n", message);
    bool could = _webSocket.sendTXT(message);
    DL_LOG("[ESPPhoenixSocket] %d after join message", could);
    return ref;
  }
  // Returns message ref
  uint32_t _sendJsonMessage(const String &topic, const String &event, const String &payload)
  {
    char message[1024];
    uint32_t ref = encodeMessageString(message, sizeof(message), topic.c_str(), event.c_str(), payload.c_str());
    DL_LOG("[ESPPhoenixSocket] Sending raw message as \n\t\t%s\n", message);
    _webSocket.sendTXT(message);
    return ref;
  }

  void _sendHeartbeat()
  {
    char message[128];
    encodeMessageString(message, sizeof(message), "phoenix", "heartbeat", "{}");
    _webSocket.sendTXT(message);
  }
  uint32_t encodeMessageString(char *buffer, const uint16_t length, const char *topic, const char *event, const char *payload)
  {
    uint32_t ref = _getNextRef();
    if (strlen(payload) == 0)
      snprintf(buffer, length, MESSAGE_STRING, topic, event, ref, "{}");
    else
      snprintf(buffer, length, MESSAGE_STRING, topic, event, ref, payload);
    return ref;
  }
  uint32_t _getNextRef()
  {
    return ++_ref_counter;
  }
};
PhoenixSocket *PhoenixSocket::_instance = nullptr;
#endif