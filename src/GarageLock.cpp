#define USE_ESP_IDF_GPIO 1

#include <Arduino.h>
#include <WiFi.h>
#include <SPI.h>
#include <string>
//#include <ArduinoOTA.h>
#include "RemoteLock.h"
#include <PubSubClient.h>
#include "MCP23S08.h"
#include "SECRETS.h"
//#include <Update.h>
//#include <HTTPUpdate.h>
//#include <ESP32httpUpdate.h>
#include <FS.h>
#include <SPIFFS.h>
#include <HTTPClient.h>
#include <HttpsOTAUpdate.h>

using namespace std;

static const char *server_certificate = "";

#define NO_LOCKS 1

string base = "garagelock";
string controlsubject = base + "/onoff";
string statussubject = controlsubject + "/state";
string updatesubject = base + "/updates";
string commandsubject = base + "/command";
string configsubject = base + "/config";

string host;
string myStatus;

//#define CONFIGMODE

WiFiClient client;
PubSubClient mqttClient(MQTT_HOST, 1883, client);

QueueHandle_t inQueue;
QueueHandle_t outQueue;
QueueHandle_t cmdQueue;

// SPIClass spi(HSPI);
// MCP23S08 mcp(&spi, 11, 0);

// RemoteLock locks[NO_LOCKS] = {
//     RemoteLock(new SpiPin(&mcp, 5, false),
//                new SpiPin(&mcp, 6, false),
//                new SpiPin(&mcp, 7, false),
//                new SpiPin(&mcp, 3, false),
//                new SpiPin(&mcp, 2, false),
//                new SpiPin(&mcp, 0, false),
//                new SpiPin(&mcp, 1, false))};

RemoteLock locks[NO_LOCKS];

// OTA Update task

// void updater(void *parameters)
// {
//   for (;;)
//   {
//     vTaskDelay(1000 / portTICK_PERIOD_MS);
//     ArduinoOTA.handle();
//   }
// }

struct flipConfig
{
  bool closePin;
  bool lockLimitPin;
  bool unlockLimitPin;
};

void GetLockConfig()
{
  RemoteLock rl(new LocalPin(26, false),
                new LocalPin(27, false),
                new LocalPin(32, false),
                new LocalPin(22, false),
                new LocalPin(23, false),
                new LocalPin(19, false),
                new LocalPin(21, false));
};

// Console task

void console(void *parameters)
{
  for (;;)
  {
    Serial.print("Toggle: \n (c) close switch \n (l)ock limit switch\n (u)nlock limit switch\n (r)ead pins\n (M)QTT unlock (m).\n");
    char a[5];
    while (Serial.read(&a[0], 5) <= 0)
    {
      vTaskDelay(500 / portTICK_PERIOD_MS);
    }
    xQueueSend(cmdQueue, &a, portMAX_DELAY);
  }
}
// Button / Relay controller task

void checkDebugQueue()
{
  int debuglock = 0;

  if (uxQueueMessagesWaiting(cmdQueue) > 0)
  {
    char msgc[5];
    xQueueReceive(cmdQueue, &msgc, portMAX_DELAY);
    switch (msgc[0])
    {
    case 'c':
      locks[debuglock].closePin()->toggle();
      break;
    case 'l':
      locks[debuglock].lockLimitPin()->toggle();
      break;
    case 'u':
      locks[debuglock].unlockLimitPin()->toggle();
      break;
    case 'm':
      for (int i = 0; i < NO_LOCKS; i++)
      {
        locks[i].unlock();
      }
      break;
    case '1':
      locks[debuglock].relay1Pin()->toggle();
      break;
    case '2':
      locks[debuglock].relay2Pin()->toggle();
      break;
    case '3':
      locks[debuglock].act1Pin()->toggle();
      break;
    case '4':
      locks[debuglock].act2Pin()->toggle();
      break;
    case 'r':
      break;
    default:
      break;
    }
    for (int i = 0; i < NO_LOCKS; i++)
    {
      // Serial.println("Polling");
      // locks[i].poll();
    }
    Serial.printf("c - Close Pin: %i\n", locks[debuglock].closePin()->read());
    Serial.printf("l - LockLimit Pin: %i\n", locks[debuglock].lockLimitPin()->read());
    Serial.printf("u - UnlockLimit Pin: %i\n", locks[debuglock].unlockLimitPin()->read());
    Serial.printf("1 - Relay1 Pin: %i f %i\n", locks[debuglock].relay1Pin()->getWrittenState(), locks[debuglock].relay1Pin()->_flip);
    Serial.printf("2 - Relay2 Pin: %i f %i\n", locks[debuglock].relay2Pin()->getWrittenState(), locks[debuglock].relay2Pin()->_flip);
    Serial.printf("3 - Act1 Pin: %i f %i\n", locks[debuglock].act1Pin()->getWrittenState(), locks[debuglock].act1Pin()->_flip);
    Serial.printf("4 - Act2 Pin: %i f %i\n", locks[debuglock].act2Pin()->getWrittenState(), locks[debuglock].act2Pin()->_flip);
  }
}

void remote_loop(void *parameters)
{
  // RemoteLock locks[NO_LOCKS] = {
  //     RemoteLock(new LocalPin(13, true), new LocalPin(16, true), new LocalPin(11, false), new LocalPin(12, false))};

  for (int i = 0; i < NO_LOCKS; i++)
  {
    // Serial.println("Polling");
    locks[i].init();
  }

  bool prevstate = 99;
  for (;;)
  {
    for (int i = 0; i < NO_LOCKS; i++)
    {
      // Serial.println("Polling");
#ifndef CONFIGMODE
      locks[i].poll();
#endif
    }
    // Serial.println("Checking msg Queue");
    if (uxQueueMessagesWaiting(inQueue) > 0)
    {
      char msgc[10];
      xQueueReceive(inQueue, &msgc, portMAX_DELAY);
      string msg(msgc);
      Serial.printf("Has messages %s\n", msg);
      xQueueReset(inQueue);
      if (msg == "OFF")
        for (int i = 0; i < NO_LOCKS; i++)
        {
#ifndef CONFIGMODE
          locks[i].unlock();
#endif
        }
      if (msg == "ON")
        for (int i = 0; i < NO_LOCKS; i++)
        {
#ifndef CONFIGMODE
          locks[i].trylock();
#endif
        }
    }
    bool locked = true;
    // Serial.println("Checking if locked");

    for (int i = 0; i < NO_LOCKS; i++)
    {
      // Serial.printf("Checking if locked %d\n", i);
      locked = locked && locks[i].isLocked();
    }

    // Serial.printf("Posting status to %s locked %d\n", STATUSSUBJECT, locked);
    // mqttClient.publish(STATUSSUBJECT, );
    if (locked != prevstate)
    {
      const char *buff = locked ? "ON" : "OFF";
      xQueueSend(outQueue, buff, portMAX_DELAY);
      prevstate = locked;
    }

    checkDebugQueue();
    HttpsOTAStatus_t otastatus = HttpsOTA.status();
    if (otastatus == HTTPS_OTA_SUCCESS)
    {
      Serial.println("Firmware written successfully. To reboot device, call API ESP.restart() or PUSH restart button on device");
      ESP.restart();
    }
    else if (otastatus == HTTPS_OTA_FAIL)
    {
      Serial.println("Firmware Upgrade Fail");
    }
  }
}

void HttpEvent(HttpEvent_t *event)
{
  switch (event->event_id)
  {
  case HTTP_EVENT_ERROR:
    Serial.println("Http Event Error");
    break;
  case HTTP_EVENT_ON_CONNECTED:
    Serial.println("Http Event On Connected");
    break;
  case HTTP_EVENT_HEADER_SENT:
    Serial.println("Http Event Header Sent");
    break;
  case HTTP_EVENT_ON_HEADER:
    Serial.printf("Http Event On Header, key=%s, value=%s\n", event->header_key, event->header_value);
    break;
  case HTTP_EVENT_ON_DATA:
    break;
  case HTTP_EVENT_ON_FINISH:
    Serial.println("Http Event On Finish");
    break;
  case HTTP_EVENT_DISCONNECTED:
    Serial.println("Http Event Disconnected");
    break;
  }
}

bool updating = false;

// MQTT Task

void onMqttMessage(const char topic[], byte *payload, unsigned int messageSize)
{
  // we received a message, print out the topic and contents
  Serial.print("Received a message with topic '");
  Serial.print(topic);
  Serial.print("', length ");
  Serial.print(messageSize);
  Serial.print(" bytes:\n");
  if (messageSize)
  {
    string msg;
    for (int i = 0; i < messageSize; i++)
    {
      msg += (char)payload[i];
    }
    if (updatesubject == topic)
    {
      if (!updating)
      {
        disableCore0WDT();
        disableCore1WDT();
        disableLoopWDT();
        updating = true;
        Serial.println("Updating......");
        Serial.println(msg.c_str());
        HttpsOTA.onHttpEvent(HttpEvent);
        HttpsOTA.begin(msg.c_str(), server_certificate);
        return;
      }
    }
    // string msg(reinterpret_cast<const char *>(payload), messageSize);
    Serial.printf("Message: %s\n", msg);
    if (msg == "OFF" || msg == "ON")
    {
      Serial.println("Placing message on inQueue");
      xQueueSend(inQueue, &msg, portMAX_DELAY);
      Serial.println("Placed message on inQueue");
    }
  }
  Serial.println("Done processing message with topic ");
}

void mqttConnect()
{
  // Use WiFiClient class to create TCP connections
  while (!mqttClient.connected())
  {
    Serial.println("MQTT: Trying to connect MQTT");
    while (WiFi.status() != WL_CONNECTED)
    {
      Serial.println("MQTT: waiting for wifi connection");
      vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    // if (!mqttClient.connect(&id[0]))
    if (!mqttClient.connect(host.c_str()))
    {
      Serial.print("MQTT: Could not connect \n");
    }
    else
    {
      // Use WiFiClient class to create TCP connections
      Serial.print("MQTT: Connected to HASS\n");
      mqttClient.setCallback(onMqttMessage);
      mqttClient.subscribe(controlsubject.c_str());
      mqttClient.subscribe(myStatus.c_str());
      mqttClient.subscribe((updatesubject + host).c_str());
      vTaskDelay(1000 / portTICK_PERIOD_MS);
      return;
    }
  }
}

void mqttReconnect()
{
  // Make sure we stay connected to the mqtt broker
  if (!mqttClient.connected())
  {
    mqttConnect();
  }
}

void mqtt_loop(void *parameters)
{
  for (;;)
  {
    vTaskDelay(500 / portTICK_PERIOD_MS);
    // Serial.println("Polling MQTT");
    mqttReconnect();
    mqttClient.loop();
    if (uxQueueMessagesWaiting(outQueue) > 0)
    {
      char msgc[10];
      xQueueReceive(outQueue, &msgc, portMAX_DELAY);
      xQueueReset(outQueue);
      string msg(msgc);
      mqttClient.publish(statussubject.c_str(), msgc);
    }
  }
}

// Wifi Task

void wifiConnect()
{
  WiFi.begin(WIFI_AP_NAME, WIFI_PASSWORD);

  Serial.print("WIFI: Waiting for WiFi... ");
  while (WiFi.status() != WL_CONNECTED)
  {
    Serial.print(".");
    vTaskDelay(500 / portTICK_PERIOD_MS);
  }

  Serial.println("\nWIFI: WiFi connected");
  Serial.print("WIFI: IP address: ");
  Serial.println(WiFi.localIP());
}

void wifi_loop(void *parameters)
{
  for (;;)
  {
    if (WiFi.status() == WL_CONNECTED)
    {
      vTaskDelay(500 / portTICK_PERIOD_MS);
      continue;
    }
    Serial.println("WIFI: Wifi is not connecting... Try to connect");
    mqttClient.disconnect();
    wifiConnect();
  }
}

void init()
{
  xTaskCreate(
      remote_loop,
      "Remote",
      2000,
      NULL,
      1,
      NULL);
}

void sendStatus()
{
  char buffer[11];

  string topic = "garagelock/" + host;
  sprintf(buffer, "c %d l %d u %d", locks[0].closePin()->read(), locks[0].lockLimitPin()->read(), locks[0].unlockLimitPin()->read());
  mqttClient.publish(topic.c_str(), buffer);
}

// Entry points
void setup()
{
  Serial.begin(300000);
  delay(10);

  host = base + WiFi.macAddress().c_str();

  inQueue = xQueueCreate(10, sizeof(char) * 5);
  outQueue = xQueueCreate(10, sizeof(char) * 5);
  cmdQueue = xQueueCreate(10, sizeof(char) * 5);

  // We start by connecting to a WiFi network
  wifiConnect();
  mqttConnect();

  // spi.begin(16, 18, 17, 11);

  // spi.begin(12, 16, 13, 11);
  // mcp.begin();

  delay(500);
  // setupOTA();

  xTaskCreate(
      mqtt_loop,
      "MQTT",
      2000,
      NULL,
      1,
      NULL);

  xTaskCreate(
      wifi_loop,
      "Wifi",
      2000,
      NULL,
      1,
      NULL);

  xTaskCreate(
      console,
      "Console",
      2000,
      NULL,
      1,
      NULL);
}

void loop()
{
}
