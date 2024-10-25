#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <ESPPhoenixSocket.h>
#include <HTTPUpdate.h>
#include "Logger.h"
#include "Sensor.h"
#include <ArduinoJson.h>
// Debug logging macros
#ifdef DEBUG_LOGGER
#define DL_LOG(format, ...) Serial.printf(format "\n", ##__VA_ARGS__)
#define DL_SerialBegin(args...) Serial.begin(args)
#else
#define DL_LOG(...)
#define DL_SerialBegin(...)
#endif

// Constants
constexpr const char *CHANNEL = "Device";
constexpr const uint16_t PORT = 4000;

// LoggerClient class to manage communication with the server through PhoenixSocket

class LoggerClient
{
  using AfterJoinCallback = std::function<void(const int64_t, JsonDocument)>;

public:
  explicit LoggerClient(const String &api_key, const String &url = SERVER_URL_L, const uint16_t port = PORT)
  {
    // Pre-allocate string buffers
    _apiKey.reserve(40);
    setApiKey(api_key);

    _socket = new PhoenixSocket(url, port, API_SUFFIX_L);
    _socket->onClose([this](uint16_t code)
                     { _handleDisconnect(code); });
    _socket->onConnect([this]()
                       { _handleConnect(); });
    _socket->onMessage([this](const String &topic, const String &event, const JsonDocument payload)
                       { _handleMessage(topic, event, payload); });
    _socket->onError([this](const String &error)
                     { _handleError(error); });
    _socket->onReply([this](const String &topic, const String &event, const JsonDocument payload)
                     { _handleReply(topic, event, payload); });
    _socket->begin();
  }
  void setAfterJoinCallback(AfterJoinCallback callback);
  void setApiKey(const String &key);
  void joinChannel(const String &payload);
  void sendSensorData(float value, const String &sensorId);
  void sendSensorData(double value, const String &payload);
  void sendStatus(const String &payload);
  void tick();
  bool updateFirmware();
  uint32_t getUnix();

private:
  uint32_t _lastRef;
  AfterJoinCallback _afterJoinCallback;
  PhoenixSocket *_socket;
  String _apiKey;
  uint32_t _unix = 0;
  uint32_t _lastUnix = 0;
  bool _channelJoined = false;
  void _syncTime();
  void _handleReply(const String &topic, const String &event, const JsonDocument payload);
  void _handleConnect();
  void _handleDisconnect(uint16_t code);
  void _handleMessage(const String &topic, const String &event, const JsonDocument payload);
  void _handleError(const String &error);
};
void LoggerClient::setApiKey(const String &key)
{
  _apiKey = key;
}
void LoggerClient::setAfterJoinCallback(AfterJoinCallback callback)
{
  _afterJoinCallback = callback;
}
uint32_t LoggerClient::getUnix()
{
  if (_unix)
  {
    uint32_t diff = millis() - _lastUnix;
    if (diff > ONE_DAY)
    {
      _unix += diff / 1000UL;
      _lastUnix = millis() - (diff % 1000UL);
    }
    return (_unix + (millis() - _lastUnix) / 1000UL);
  }
  _syncTime();
  return 0;
}
void LoggerClient::joinChannel(const String &payload)
{
  if (!_channelJoined)
  {
    _lastRef = _socket->joinChannel(CHANNEL, payload);
    DL_LOG("Joining channel: %s", CHANNEL);
    DL_LOG("Payload: %s", payload.c_str());
  }
}
void LoggerClient::sendSensorData(float value, const String &sensorId)
{
  char buffer[50];
  snprintf(buffer, sizeof(buffer), "{\"value\":\"%f\"}", value);
  DL_LOG("Sending sensor data: %s", buffer);
  _socket->sendEvent(CHANNEL, "new_value_sensor:" + sensorId, buffer);
}
void LoggerClient::sendSensorData(double value, const String &sensorId)
{
  char buffer[50];
  snprintf(buffer, sizeof(buffer), "{\"value\":\"%f\"}", value);
  DL_LOG("Sending sensor data: %s", buffer);
  _socket->sendEvent(CHANNEL, sensorId, buffer);
}
void LoggerClient::sendStatus(const String &payload)
{
  _socket->sendEvent(CHANNEL, "status", payload);
}
void LoggerClient::_syncTime()
{
  _socket->sendEvent(CHANNEL, "time");
}
bool LoggerClient::updateFirmware()
{
  // Impl
  return true;
}
void LoggerClient::tick()
{
  _socket->loop();
}
void LoggerClient::_handleReply(const String &topic, const String &event, const JsonDocument payload)
{
#ifdef DEBUG_LOGGER
  String message;
  serializeJson(payload, message);
  DL_LOG("Reply from server: %s", topic.c_str());
  DL_LOG("Event: %s", event.c_str());
  DL_LOG("Payload: %s", message.c_str());
#endif
  if (topic != CHANNEL)
  {
    DL_LOG("Unknown topic: %s", topic.c_str());
    return;
  }

  if (!_channelJoined)
  {
    if (!(payload["group_id"].is<uint64_t>() && payload["sensor_ids"].is<JsonArray>()))
    {
      DL_LOG("Invalid reply from server");
      return;
    }
    if (!_afterJoinCallback)
    {
      DL_LOG("No after join callback set");
      return;
    }
    _afterJoinCallback(payload["group_id"], payload["sensor_ids"]);
    _channelJoined = true;
    _syncTime();
    return;
  }
  if (payload["timestamp"].is<uint32_t>())
  {
    _unix = payload["timestamp"];
    _lastUnix = millis();
  }
}
void LoggerClient::_handleConnect()
{
  DL_LOG("Connected to server");
}
void LoggerClient::_handleDisconnect(uint16_t code)
{
  DL_LOG("Disconnected from server: %d", code);
}
void LoggerClient::_handleMessage(const String &topic, const String &event, JsonDocument payload)
{
#ifdef DEBUG_LOGGER
  String message;
  serializeJson(payload, message);
  DL_LOG("Message from server: %s", topic.c_str());
  DL_LOG("Event: %s", event.c_str());
  DL_LOG("Payload: %s", message.c_str());
#endif
}
void LoggerClient::_handleError(const String &error)
{
  DL_LOG("Error from server: %s", error.c_str());
}
