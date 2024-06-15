#ifndef LOGGER_H
#define SERVER_URL F("esplogger.tech")
#define TIME_PATH F("/api/v1/time")
#define MAX_INTERVAL 3600
#define MIN_INTERVAL 60
#define MAX_SENSOR_INTERVAL 1800
#define MIN_SENSOR_INTERVAL 5
#define ONE_DAY 86400000UL
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
  Sensor(const String &name = "", const String &unit = "", const String &type = "", float (*callback)() = nullptr)
  {
    _name = name;
    _unit = unit;
    _type = type;
    _callback = callback;
    _sensor[F("name")] = _name;
    _sensor[F("unit")] = _unit;
    _sensor[F("sensor_type")] = _type;
  }

  float getValue()
  {
    esp_task_wdt_reset();
    // Serial.printf("Getting value for sensor: %s\n", _name.c_str());
    if (_callback)
    {
      _value = _callback();
      // Serial.printf("Value: %f\n", _value);
      return _value;
    }
    return 0;
  }
  String diagnostic()
  {
    String output;
    output += _name + F(":  ");
    output += String(getValue());
    output += F(" ");
    output += _unit;
    return output;
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
  void run(u32_t timestamp = 0)
  {
    _value = getValue();
    JsonDocument sensorValue;
    sensorValue[F("value")] = _value;
    sensorValue[F("timestamp")] = timestamp;
    _sensorValues.add(sensorValue);
    // String output;
    // serializeJson(_sensorValues, output);
    // Serial.printf("Sensor values: %s\n", output.c_str());
  }
  JsonDocument getJson()
  {
    JsonDocument doc = _sensor;
    doc[F("sensor_values")] = _sensorValues;
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
  ESPLogger(bool secure = true, const String &url = SERVER_URL)
  {
    _apiKey.reserve(40);
    _url.reserve(40);
    _timeUrl.reserve(40);
    _secure = secure;
    _setUrl(url);
    _http = new HTTPClient;
    _deviceId = ESP.getEfuseMac();
    _device["device_id"] = _deviceId;
    setGroup(F("Default"));
    setDeviceName(F("ESP32"));
    _deviceSensors = _device[F("sensors")].to<JsonArray>();
  }

  // Call init to start the logger after connecting to wifi
  // - api_key: The API key for the logger
  // - deviceName: The name of the device
  // - group: The group name for the device (default is "Default")
  // - sensorReadInterval: The interval in seconds to read sensor values (minimum is 5 seconds)
  // - logInterval: The interval in seconds to log sensor values (minimum is 60 seconds)
  bool init(const String &api_key, const String &deviceName = F("ESP32"), const String &group = F("Default"), u32_t sensorReadInterval = 5, u32_t logInterval = 60)
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
    if (!getUnix())
    {
      _syncTime();
      return false;
    }
    _updateSensors();
    if (getUnix() - _lastLog > _logInterval)
    {
      _lastLog = getUnix();
      // Serial.println("Logging data");
      for (int i = 0; i < NumSensors; i++)
      {
        if (_sensors[i])
        {
          _deviceSensors.add(_sensors[i]->getJson());
        }
      }
      if (_sendData())
      {
        _lastLog = getUnix();
        return true;
      }
    }
    return false;
  }
  String sensorsDiagnostic()
  {
    String output = F("");
    for (int i = 0; i < NumSensors; i++)
    {
      if (_sensors[i])
      {
        output += _sensors[i]->diagnostic() + F("\n");
      }
    }
    return output;
  }
  bool addSensor(const String &name, const String &unit, const String &type, float (*callback)())
  {
    return addSensor(Sensor(name, unit, type, callback));
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
  void setSensorReadInterval(u32_t sensorReadInterval)
  {
    _sensorReadInterval = max(min((int)sensorReadInterval, MAX_SENSOR_INTERVAL), MIN_SENSOR_INTERVAL);
  }
  void setLogInterval(u32_t logInterval)
  {
    _logInterval = max(min((int)logInterval, MAX_INTERVAL), MIN_INTERVAL);
  }
  void setDeviceName(const String &deviceName)
  {
    _device[F("device_name")] = deviceName;
    _deviceName = deviceName;
  }
  void setGroup(const String &group)
  {
    _device[F("group_name")] = group;
    _deviceGroup = group;
  }
  u_int32_t getUnix()
  {
    if (_unix)
    {
      u32_t diff = millis() - _lastUnix;
      if (_unix && diff > 86400000ul)
      {
        _unix += diff / 1000ul;
        _lastUnix = millis() - diff % 1000ul;
      }
      return (_unix + (millis() - _lastUnix) / 1000ul);
    }
    return 0;
  }
  void setTransmitting(bool transmitting)
  {
    _transmitting = transmitting;
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
  u64_t _deviceId;
  HTTPClient *_http = nullptr;
  String _deviceGroup;
  String _deviceName;
  String _url;
  String _timeUrl;
  String _apiKey;
  bool _secure;
  bool _transmitting = false;
  u32_t _unix = 0;
  u32_t _lastUnix = 0;
  u32_t _logInterval;
  u32_t _sensorReadInterval;
  u16_t _sensorIntervalOffset = 0;
  u32_t _lastSensorTimeStamp = 0;
  u32_t _lastSensorRead = 0;
  u32_t _lastLog = 0;
  void _resetJSON()
  {
    _deviceSensors.clear();
    _device.clear();
    _device[F("device_id")] = _deviceId;
    _device[F("device_name")] = _deviceName;
    _device[F("group_name")] = _deviceGroup;
    _deviceSensors = _device[F("sensors")].to<JsonArray>();
  }
  void _updateSensors()
  {
    if (getUnix() - _lastSensorRead > _sensorReadInterval - _sensorIntervalOffset)
    {
      // Serial.println("Reading sensor values");
      u32_t timestamp = getUnix();
      int diff = (((int)timestamp - (int)_lastSensorTimeStamp) - (int)_sensorReadInterval);
      if (_lastSensorTimeStamp && diff)
      {
        _sensorIntervalOffset = max((int)0, diff);
      }
      // Serial.printf("Reading sensors on Timestamp: %u\n", timestamp);
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
    // Serial.printf("Sending data\n");
    _http->begin( _url);
    _http->addHeader(F("Content-Type"), F("application/json"));
    _http->addHeader(F("Authorization"), _apiKey);
    String payload;
    serializeJson(_device, payload);
    esp_task_wdt_reset();
    _resetJSON();
    int httpCode = _http->POST(payload);
    // Serial.printf("Send data HTTP Code: %d\n", httpCode);
    if (httpCode == 201)
    {
      if (millis() - _lastUnix > ONE_DAY)
        _syncTime();
      _http->end();
      return true;
    }
    else
    {
      _http->end();
      return false;
    }
  }
  bool _syncTime()
  {
    // Serial.printf("Syncing time\n");
    //  WiFiClient client;
    // Serial.printf("Connecting to: %s\n", _timeUrl.c_str());
    _http->begin(_timeUrl);
    // Serial.printf("Adding headers\n");
    _http->addHeader(F("Content-Type"), F("application/json"));
    _http->addHeader(F("Authorization"), _apiKey);
    // Json format -> {unix_time: 123456789}
    esp_task_wdt_reset();
    int httpCode = _http->GET();
    // Serial.printf("Sync time HTTP Code: %d\n", httpCode);
    if (httpCode == 200)
    {
      String payload = _http->getString();

      // Serial.printf("Payload: %s\n", payload.c_str());
      JsonDocument doc;
      // Serial.printf("Deserializing\n");
      deserializeJson(doc, payload);
      // Serial.printf("Deserialized\n");
      _unix = doc[F("unix_time")];
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
  void _setUrl(const String &url)
  {
    if (_secure)
    {
      _url = F("https://");
      _url += url + F("/api/v1/log");
      _timeUrl = F("https://");
      _timeUrl += url + TIME_PATH;
    }
    else
    {
      _url = F("http://");
      _url += url + F("/api/v1/log");
      _timeUrl = F("http://");
      _timeUrl += url + TIME_PATH;
    }
  }
};
#endif