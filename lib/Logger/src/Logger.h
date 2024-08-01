#ifndef LOGGER_H
#define DEBUG_LOGGER 1 // SET TO 0 OUT TO REMOVE TRACES
#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "1.0.0"
#endif
#if DEBUG_LOGGER
#define DL_SerialBegin(...) Serial.begin(__VA_ARGS__);
#define DL_print(...) Serial.print(__VA_ARGS__)
#define DL_write(...) Serial.write(__VA_ARGS__)
#define DL_println(...) Serial.println(__VA_ARGS__)
#define DL_printf(...) Serial.printf(__VA_ARGS__)
#else
#define DL_SerialBegin(...)
#define DL_print(...)
#define DL_write(...)
#define DL_println(...)
#define DL_printf(...)
#endif
#define SERVER_URL F("esplogger.tech")
#define API_SUFFIX F("/api/v1")
#define LOG_PATH F("/log")
#define TIME_PATH F("/time")
#define FIRMWARE_PATH F("/firmwares/download/")
#define MAX_INTERVAL 3600
#define MIN_INTERVAL 60
#define MAX_SENSOR_INTERVAL 1800
#define MIN_SENSOR_INTERVAL 10
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

    DL_printf("Getting value for sensor: %s\n", _name.c_str());
    if (_callback)
    {
      _value = _callback();
      DL_printf("Value: %f\n", _value);
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
    // DL_printf("Sensor values: %s\n", output.c_str());
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
    _logUrl.reserve(40);
    _timeUrl.reserve(40);
    _downloadUrl.reserve(40);
    _statusUrl.reserve(60);
    _secure = secure;
    _http = new HTTPClient;
    _deviceId = ESP.getEfuseMac();
    _device["device_id"] = _deviceId;
    _setUrl(url);
    setFirmwareVersion(FIRMWARE_VERSION);
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
  bool init(const String &api_key, const String &deviceName = F("ESP32"), const String &group = F("Default"), const String &firmwareVersion = FIRMWARE_VERSION, u32_t sensorReadInterval = 30, u32_t logInterval = 60)
  {
    setFirmwareVersion(firmwareVersion);
    setDeviceName(deviceName);
    setGroup(group);
    _apiKey = "Bearer " + api_key;
    setLogInterval(logInterval);
    setSensorReadInterval(sensorReadInterval);
    start();
    String payload;
    serializeJson(_device, payload);
    return _sendStatus(payload);
  }
  // Call tick in your loop to log sensor values
  bool tick()
  {
    if (!_transmitting)
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
      DL_println("Logging data");
      for (int i = 0; i < NumSensors; i++)
      {
        if (_sensors[i])
        {
          _deviceSensors.add(_sensors[i]->getJson());
        }
      }
      String payload;
      serializeJson(_device, payload);
      if (_sendData(payload))
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
  u32_t getSensorReadInterval()
  {
    return _sensorReadInterval;
  }
  void setLogInterval(u32_t logInterval)
  {
    _logInterval = max(min((int)logInterval, MAX_INTERVAL), MIN_INTERVAL);
  }
  u32_t getLogInterval()
  {
    return _logInterval;
  }
  void setFirmwareVersion(const String &version)
  {
    _device[F("firmware_version")] = version;
    _firmwareVersion = version;
  }
  String getFirmwareVersion()
  {
    return _firmwareVersion;
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
    _lastLog = getUnix();
    _lastSensorRead = getUnix();
    _transmitting = true;
  }
  void setOnUpdate(void (*callback)())
  {
    _update_started = callback;
  }
  void setOnUpdateFinished(void (*callback)())
  {
    _update_finished = callback;
  }

private:
  Sensor *_sensors[NumSensors];
  void (*_update_started)() = nullptr;
  void (*_update_finished)() = nullptr;
  JsonDocument _device;
  JsonArray _deviceSensors;
  u64_t _deviceId;
  HTTPClient *_http = nullptr;
  String _deviceGroup;
  String _deviceName;
  String _logUrl;
  String _timeUrl;
  String _downloadUrl;
  String _statusUrl;
  String _apiKey;
  String _firmwareVersion;
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
    _device[F("firmware_version")] = _firmwareVersion;
    _deviceSensors = _device[F("sensors")].to<JsonArray>();
  }
  void _updateSensors()
  {
    if (getUnix() - _lastSensorRead > _sensorReadInterval - _sensorIntervalOffset)
    {
      DL_println("Reading sensor values");
      u32_t timestamp = getUnix();
      int diff = (((int)timestamp - (int)_lastSensorTimeStamp) - (int)_sensorReadInterval);
      if (_lastSensorTimeStamp && diff)
      {
        _sensorIntervalOffset = min(max((int)0, diff), 5);
      }
      DL_printf("Reading sensors on Timestamp: %u\n", timestamp);
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
  bool _sendData(const String &payload)
  {
    static char retries = 0;
    DL_printf("Sending data try: %i\n", retries);
    _http->begin(_logUrl);
    _http->addHeader(F("Content-Type"), F("application/json"));
    _http->addHeader(F("Authorization"), _apiKey);
    _resetJSON();
    int httpCode = _http->POST(payload);
    DL_printf("Send data HTTP Code: %d\n", httpCode);

    if (httpCode == 201)
    {
      retries = 0;
      if (millis() - _lastUnix > ONE_DAY)
        _syncTime();
      String response = _http->getString();
      DL_printf("Response: %s\n", response.c_str());
      JsonDocument doc;
      deserializeJson(doc, response);
      handleNotice(doc);
      _http->end();
      return true;
    }
    else
    {
      _http->end();
      if (retries < 3)
      {
        retries++;
        return _sendData(payload);
      }
      else
      {
        retries = 0;
        return false;
      }
    }
  }
  void handleNotice(JsonDocument &doc)
  {
    if (doc[F("notice")] == F("update required"))
    {
      DL_println("Update required");
      String firmware_number = doc[F("firmware_number")];
      _updateFirmware(_downloadUrl + firmware_number);
    }
  }
  bool _updateFirmware(const String &downloadUrl = "")
  {
    DL_printf("Updating firmware from: %s\n", downloadUrl.c_str());
    _http->begin(downloadUrl);
    _http->addHeader(F("Authorization"), _apiKey);

    DL_println("Updating firmware");
    if (_update_started)
      httpUpdate.onStart(_update_started);
    if (_update_finished)
      httpUpdate.onEnd(_update_finished);
    t_httpUpdate_return ret = httpUpdate.update(*_http);

    switch (ret)
    {
    case HTTP_UPDATE_FAILED:
      DL_printf("HTTP_UPDATE_FAILED Error (%d): %s\n", httpUpdate.getLastError(), httpUpdate.getLastErrorString().c_str());
      return false;
      break;

    case HTTP_UPDATE_NO_UPDATES:
      DL_println("HTTP_UPDATE_NO_UPDATES");
      return false;
      break;

    case HTTP_UPDATE_OK:
      DL_println("HTTP_UPDATE_OK");
      return true;
      break;
    default:
      return false;
      break;
    }
  }
  bool _sendStatus(const String &payload)
  {
    DL_println("Sending status");
    DL_printf("Connecting to: %s\n", _statusUrl.c_str());
    _http->begin(_statusUrl);
    _http->addHeader(F("Content-Type"), F("application/json"));
    _http->addHeader(F("Authorization"), _apiKey);
    int httpCode = _http->POST(payload);
    DL_printf("Send status HTTP Code: %d\n", httpCode);
    if (httpCode == 200)
    {
      String response = _http->getString();
      DL_printf("Response: %s\n", response.c_str());
      JsonDocument doc;
      deserializeJson(doc, response);
      _syncTime(doc[F("unix_time")]);
      handleNotice(doc);
      _http->end();
      return true;
    }
    else
    {
      _http->end();
      return false;
    }
  }
  bool _syncTime(const String &unix = "")
  {
    if (unix != "")
    {
      _unix = unix.toInt();
      _lastUnix = millis();
      _lastLog = _unix;
      _lastSensorRead = _unix;
      return true;
    }
    // If WiFi mode is set to AP return false
    if (WiFi.getMode() == WIFI_AP || WiFi.status() != WL_CONNECTED)
    {
      return false;
    }
    DL_printf("Syncing time\n");
    //  WiFiClient client;
    DL_printf("Connecting to: %s\n", _timeUrl.c_str());
    _http->begin(_timeUrl);
    DL_printf("Adding headers\n");
    _http->addHeader(F("Content-Type"), F("application/json"));
    _http->addHeader(F("Authorization"), _apiKey);
    // Json format -> {unix_time: 123456789}

    int httpCode = _http->GET();
    DL_printf("Sync time HTTP Code: %d\n", httpCode);
    if (httpCode == 200)
    {
      String payload = _http->getString();

      DL_printf("Payload: %s\n", payload.c_str());
      JsonDocument doc;
      DL_printf("Deserializing\n");
      deserializeJson(doc, payload);
      DL_printf("Deserialized\n");
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
    String _prefix = F("https://");
    if (!_secure)
      _prefix = F("http://");
    _logUrl = _prefix + url + API_SUFFIX + LOG_PATH;
    _timeUrl = _prefix + url + API_SUFFIX + TIME_PATH;
    _downloadUrl = _prefix + url + API_SUFFIX + FIRMWARE_PATH;
    _statusUrl = _prefix + url + API_SUFFIX + F("/devices/") + String(_deviceId) + F("/status");
  }
};
#endif