#pragma once
#ifndef LOGGERCLIENT_H
#define LOGGERCLIENT_H
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
#define BASECHANNEL "device:"
constexpr const uint16_t REJOIN_INTERVAL = 5000;

// LoggerClient class to manage communication with the server through PhoenixSocket

class LoggerClient
{
  using AfterJoinCallback = std::function<void(const int64_t, JsonDocument)>;

public:
  explicit LoggerClient(const uint64_t &device_id, const String &api_key, const String &url, const uint16_t port)
  {
    // Pre-allocate string buffers
    _apiKey.reserve(40);
    setApiKey(api_key);
    CHANNEL = BASECHANNEL + String(device_id);
    _socket = new PhoenixSocket(url, port, API_SUFFIX_L);
    _socket->onClose([this](uint16_t code)
                     { _channelJoined = false; 
                      _handleDisconnect(code); });
    _socket->onConnect([this]()
                       { _handleConnect(); });
    _socket->onMessage([this](const String &topic, const String &event, const JsonDocument payload)
                       { _handleMessage(topic, event, payload); });
    _socket->onError([this](const String &error)
                     { _handleError(error); });
    _socket->onReply([this](const String &topic, const String &event, const JsonDocument payload)
                     { _handleReply(topic, event, payload); });
    _socket->begin();
    _socket->loop();
  }
  void setAfterJoinCallback(AfterJoinCallback callback);
  void setApiKey(const String &key);
  void joinChannel(const String &payload);
  void sendSensorData(float value, const String &sensorId);
  void sendSensorData(double value, const String &payload);
  void sendStatus(const String &payload);
  void tick();
  void setJoinString(const String &joinString);
  bool isChannelJoined();
  bool updateFirmware();
  uint32_t getUnix();

private:
  String CHANNEL;
  String _joinString;
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
void LoggerClient::setJoinString(const String &joinString)
{
  _joinString = joinString;
}

void LoggerClient::setApiKey(const String &key)
{
  _apiKey = key;
}
bool LoggerClient::isChannelJoined()
{
  return _channelJoined;
}
void LoggerClient::setAfterJoinCallback(AfterJoinCallback callback)
{
  _afterJoinCallback = callback;
}
uint32_t LoggerClient::getUnix()
{
  static uint64_t lastSyncAttempt = 0;
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
  else if (_channelJoined && (lastSyncAttempt == 0 || (millis() - lastSyncAttempt) > 1000))
  {
    lastSyncAttempt = millis();
    _syncTime();
  }
  return 0;
}
void LoggerClient::joinChannel(const String &payload)
{
  static uint32_t lastJoin = 0;
  if (!_channelJoined && ((millis() - lastJoin) > REJOIN_INTERVAL || lastJoin == 0))
  {
    _lastRef = _socket->joinChannel(CHANNEL, payload);
    DL_LOG("[LoggerClient] Joining channel: %s", CHANNEL);
    DL_LOG("[LoggerClient] Payload: %s", payload.c_str());
    lastJoin = millis();
  }
}
void LoggerClient::sendSensorData(float value, const String &sensorId)
{
  char buffer[50];
  snprintf(buffer, sizeof(buffer), "{\"value\":\"%f\"}", value);
  DL_LOG("[LoggerClient] Sending sensor data: %s", buffer);
  _socket->sendEvent(CHANNEL, "new_value_sensor:" + sensorId, buffer);
}
void LoggerClient::sendSensorData(double value, const String &sensorId)
{
  char buffer[50];
  snprintf(buffer, sizeof(buffer), "{\"value\":\"%f\"}", value);
  DL_LOG("[LoggerClient] Sending sensor data: %s", buffer);
  _socket->sendEvent(CHANNEL, sensorId, buffer);
}
void LoggerClient::sendStatus(const String &payload)
{
  DL_LOG("[LoggerClient] Sending status data: %s", payload);
  _socket->sendEvent(CHANNEL, "status", payload);
}
void LoggerClient::_syncTime()
{
  DL_LOG("[LoggerClient] Sending time sync request");
  _socket->sendEvent(CHANNEL, "time");
  return;
}
bool LoggerClient::updateFirmware()

{
  // Impl
  return true;
}
void LoggerClient::tick()
{
  _socket->loop();
  if (!_channelJoined && _socket->isConnected())
  {
    joinChannel(_joinString);
  }
}
void LoggerClient::_handleReply(const String &topic, const String &event, const JsonDocument payload)
{
#ifdef DEBUG_LOGGER
  String message;
  serializeJson(payload, message);
  DL_LOG("[LoggerClient] Reply from server: %s", topic.c_str());
  DL_LOG("[LoggerClient] Event: %s", event.c_str());
  DL_LOG("[LoggerClient] Payload: %s", message.c_str());
#endif
  if (topic != CHANNEL)
  {
    DL_LOG("[LoggerClient] Unknown topic: %s", topic.c_str());
    return;
  }
  JsonDocument response = payload["response"];
  if (!_channelJoined)
  {
    if (response["reason"].as<String>() == "invalid token")
    {
      DL_LOG("[LoggerClient] Invalid API key");
      return;
    }
    JsonDocument array_doc = response["sensors_ids"];
    DL_LOG("[LoggerClient] sensor_ids: %s", response["sensors_ids"].as<String>().c_str());
    JsonArray array = array_doc.as<JsonArray>();
    if (!response["group_id"].is<uint64_t>() || array.isNull())
    {
      DL_LOG("[LoggerClient] Group_id is integer: %d", response["group_id"].is<uint64_t>());
      DL_LOG("[LoggerClient] Sensor_ids is array: %d", !array.isNull());
      DL_LOG("[LoggerClient] Invalid reply from server");
      return;
    }
    if (!_afterJoinCallback)
    {
      DL_LOG("[LoggerClient] No after join callback set");
      return;
    }
    _afterJoinCallback(response["group_id"], array_doc);
    _channelJoined = true;
    DL_LOG("[LoggerClient] Channel succesfully joined");
    return;
  }
  if (response["timestamp"].is<uint32_t>())
  {
    DL_LOG("[LoggerClient] Time succesfully synced");
    _unix = response["timestamp"];
    _lastUnix = millis();
    return;
  }
}
void LoggerClient::_handleConnect()
{
  DL_LOG("[LoggerClient] Connected to server");
}
void LoggerClient::_handleDisconnect(uint16_t code)
{
  DL_LOG("[LoggerClient] Disconnected from server: %d", code);
}
void LoggerClient::_handleMessage(const String &topic, const String &event, JsonDocument payload)
{
#ifdef DEBUG_LOGGER
  String message;
  serializeJson(payload, message);
  DL_LOG("[LoggerClient] Message from server: %s", topic.c_str());
  DL_LOG("[LoggerClient] Event: %s", event.c_str());
  DL_LOG("[LoggerClient] Payload: %s", message.c_str());
#endif
}
void LoggerClient::_handleError(const String &error)
{
  DL_LOG("[LoggerClient] Error from server: %s", error.c_str());
}
#endif