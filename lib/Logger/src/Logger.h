#ifndef LOGGER_H
#define SERVER_URL "esplogger.tech"
#define TIME_PATH "/api/v1/time"
#define LOGGER_H
#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <WiFiClientSecure.h>
class Sensor;
template <int NumSensors>
class ESPLogger;

class Sensor
{
public:
  Sensor(String name = "", String unit = "", String type = "", float (*callback)() = nullptr)
  {
    _name = name;
    _unit = unit;
    _type = type;
    _callback = callback;
    _sensor["name"] = _name;
    _sensor["unit"] = _unit;
    _sensor["sensor_type"] = _type;
  }

  float getValue()
  {
    Serial.printf("Getting value for sensor: %s\n", _name.c_str());
    if (_callback)
    {
      _value = _callback();
      Serial.printf("Value: %f\n", _value);
      return _value;
    }
    return 0;
  }
  String getName()
  {
    return _name;
  }
  String getUnit()
  {
    return _unit;
  }
  String getType()
  {
    return _type;
  }

protected:
  template <int NumSensors>
  friend class ESPLogger;
  void run(uint32_t timestamp = 0)
  {
    _value = getValue();
    JsonDocument sensorValue;
    sensorValue["value"] = _value;
    sensorValue["timestamp"] = timestamp;
    _sensorValues.add(sensorValue);
    String output;
    serializeJson(_sensorValues, output);
    Serial.printf("Sensor values: %s\n", output.c_str());
  }
  JsonDocument getJson()
  {
    JsonDocument doc = _sensor;
    doc["sensor_values"] = _sensorValues;
    // Display the JSON document
    _sensorValues.clear();
    return doc;
  }

private:
  float (*_callback)();
  JsonDocument _sensor;
  JsonDocument _sensorValues;
  String _name;
  String _unit;
  String _type;
  float _value;
};
template <int NumSensors>
class ESPLogger
{
public:
  // Constructor for the logger class with the following parameters:

  // - secure: Whether to use HTTPS or HTTP (default is true)
  // - url: The URL of the server (default is SERVER_URL)
  ESPLogger(bool secure = true, String url = SERVER_URL)
  {
    _secure = secure;
    _setUrl(url);
    _http = new HTTPClient;
    _deviceId = ESP.getEfuseMac();
    _device["device_id"] = _deviceId;
    setGroup("Default");
    setDeviceName("ESP32");
    _deviceSensors = _device["sensors"].to<JsonArray>();
  }
  // Call init to start the logger after connecting to wifi
  // - api_key: The API key for the logger
  // - deviceName: The name of the device
  // - group: The group name for the device (default is "Default")
  // - sensorReadInterval: The interval in seconds to read sensor values (minimum is 5 seconds)
  // - logInterval: The interval in seconds to log sensor values (minimum is 60 seconds)
  bool init(String api_key, String deviceName, String group = "Default", uint32_t sensorReadInterval = 5, uint32_t logInterval = 60)
  {
    setDeviceName(deviceName);
    setGroup(group);
    _apiKey = "Bearer " + api_key;
    setLogInterval(logInterval);
    setSensorReadInterval(sensorReadInterval);
    start();
    return _syncTime();
  }
  // Call tick in your loop to log sensor values
  bool tick()
  {
    if (!_transmitting or !getUnix())
    {
      return false;
    }
    _updateSensors();
    if (getUnix() - _lastLog > _logInterval)
    {
      _lastLog = getUnix();
      Serial.println("Logging data");
      for (int i = 0; i < NumSensors; i++)
      {
        if (_sensors[i])
        {
          _deviceSensors.add(_sensors[i]->getJson());
        }
      }
      // Return if no sensors were added
      if (_deviceSensors.size() == 0)
      {
        Serial.println("No sensors added");
        return false;
      }
      if (_sendData())
      {
        _lastLog = getUnix();
        _deviceSensors.clear();
        return true;
      }
    }
    return false;
  }
  bool addSensor(Sensor &sensor)
  {
    for (int i = 0; i < NumSensors; i++)
    {
      if (!_sensors[i])
      {
        _sensors[i] = &sensor;
        return true;
      }
    }
    return false;
  }
  void setTransmitting(bool transmitting)
  {
    _transmitting = transmitting;
  }
  bool addSensor(String name, String unit, String type, float (*callback)())
  {
    return addSensor(Sensor(name, unit, type, callback));
  }
  void setSensorReadInterval(uint32_t sensorReadInterval)
  {
    _sensorReadInterval = min((int)sensorReadInterval, 60000);
  }
  void setLogInterval(uint32_t logInterval)
  {
    _logInterval = min((int)logInterval, 60000);
  }
  void setDeviceName(String deviceName)
  {
    _device["device_name"] = deviceName;
    _deviceName = deviceName;
  }
  void setGroup(String group)
  {
    _device["group_name"] = group;
    _deviceGroup = group;
  }
  u_int32_t getUnix()
  {
    if (_unix)
    {
      uint32_t diff = millis() - _lastUnix;
      if (_unix && diff > 86400000ul)
      {
        _unix += diff / 1000ul;
        _lastUnix = millis() - diff % 1000ul;
      }
      return _unix + (millis() - _lastUnix) / 1000ul;
    }
    return 0;
  }
  void stop()
  {
    _transmitting = false;
  }
  void start()
  {
    _transmitting = true;
  }

private:
  Sensor *_sensors[NumSensors];
  JsonDocument _device;
  JsonArray _deviceSensors;
  u_int64_t _deviceId;
  HTTPClient *_http = nullptr;
  String _deviceGroup;
  String _deviceName;
  String _url;
  String _timeUrl;
  String _apiKey;
  bool _secure;
  bool _transmitting = false;
  uint32_t _unix = 0;
  uint32_t _lastUnix = 0;
  uint32_t _logInterval;
  uint32_t _sensorReadInterval;
  u16_t _sensorIntervalOffset = 0;
  uint32_t _lastSensorTimeStamp = 0;
  uint32_t _lastSensorRead = 0;
  uint32_t _lastLog = 0;
  void _updateSensors()
  {
    if (getUnix() - _lastSensorRead > _sensorReadInterval - _sensorIntervalOffset)
    {
      Serial.println("Reading sensor values");
      u32_t timestamp = getUnix();
      int diff = (((int)timestamp - (int)_lastSensorTimeStamp) - (int)_sensorReadInterval);
      if (_lastSensorTimeStamp && diff)
      {
        _sensorIntervalOffset = max((int)0, diff);
      }
      Serial.printf("Reading sensors on Timestamp: %u\n", timestamp);
      for (int i = 0; i < NumSensors; i++)
      {
        if (_sensors[i])
        {
          _sensors[i]->run(timestamp);
        }
      }
      _lastSensorTimeStamp = timestamp;
      _lastSensorRead = getUnix();
    }
  }
  bool _sendData()
  {
    WiFiClientSecure client;
    client.setInsecure();
    _http->begin(client, _url);
    _http->addHeader("Content-Type", "application/json");
    _http->addHeader("Authorization", _apiKey);
    String payload;
    serializeJson(_device, payload);
    Serial.printf("Payload: %s\n", payload.c_str());
    int httpCode = _http->POST(payload);
    Serial.printf("HTTP Code: %d\n", httpCode);
    if (httpCode == 201)
    {
      payload = _http->getString(); // {unix_time: 123456789}
      Serial.printf("Payload: %s\n", payload.c_str());
      if (millis() - _lastUnix > 86400000ul)
      {
        JsonDocument doc;
        deserializeJson(doc, payload);
        _unix = doc["unix_time"];
        _lastUnix = millis();
      }
      _http->end();
      return true;
    }
    else
    {
      String payload = _http->getString();
      JsonDocument errorMsg;
      deserializeJson(errorMsg, payload);
      String error = errorMsg["errors"];
      Serial.printf("Error: %s\n", error.c_str());
      _http->end();
      return false;
    }
  }
  bool _syncTime()
  {
    Serial.printf("Syncing time\n");
    //  WiFiClient client;
    WiFiClientSecure client;
    client.setInsecure();
    Serial.printf("Connecting to: %s\n", _timeUrl.c_str());
    _http->begin(client, _timeUrl);
    Serial.printf("Adding headers\n");
    _http->addHeader("Content-Type", "application/json");
    _http->addHeader("Authorization", _apiKey);
    // Json format -> {unix_time: 123456789}
    int httpCode = _http->GET();
    Serial.printf("HTTP Code: %d\n", httpCode);
    if (httpCode == 200)
    {
      String payload = _http->getString();
      Serial.printf("Payload: %s\n", payload.c_str());
      JsonDocument doc;
      Serial.printf("Deserializing\n");
      deserializeJson(doc, payload);
      Serial.printf("Deserialized\n");
      _unix = doc["unix_time"];
      _lastUnix = millis();
      _lastLog = _unix;
      _lastSensorRead = _unix;
      _http->end();
      return true;
    }
    else
    {
      _http->end();
      return false;
    }
  }
  void _setUrl(String url)
  {
    if (_secure)
    {
      _url = "https://" + url + "/api/v1/log";
      _timeUrl = "https://" + url + TIME_PATH;
    }
    else
    {
      _url = "http://" + url + "/api/v1/log";
      _timeUrl = "http://" + url + TIME_PATH;
    }
  }
};
#endif