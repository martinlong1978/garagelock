#define USE_ESP_IDF_GPIO 1

#include <MqttClient.h>
#include <Arduino.h>
#include <WiFi.h>
#include <SPI.h>
#include <ArduinoOTA.h>
#include "RemoteLock.h"
#include <PubSubClient.h>
#include "MCP23S08.h"
#include "SECRETS.h"

WiFiClient client;
PubSubClient mqttClient(MQTT_HOST, 1883, client);

QueueHandle_t queue;
QueueHandle_t outQueue;

#define NO_LOCKS 1
#define SUBJECT "garagelock/feeds/onoff"
#define STATUSSUBJECT "garagelock/feeds/onoff/state"

SPIClass spi(HSPI);
MCP23S08 mcp(&spi, 16, 0);

// OTA Update task

void updater(void *parameters)
{
  for (;;)
  {
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    ArduinoOTA.handle();
  }
}

void setupOTA()
{
  // Port defaults to 3232
  ArduinoOTA.setPort(3232);

  // Hostname defaults to esp3232-[MAC]
  ArduinoOTA.setHostname("garagedoor");

  Serial.println("Configuring OTA");
  ArduinoOTA
      .onStart([]()
               {
      String type;
      if (ArduinoOTA.getCommand() == U_FLASH)
        type = "sketch";
      else // U_SPIFFS
        type = "filesystem";

      // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
      Serial.println("Start updating " + type); })
      .onEnd([]()
             { Serial.println("\nEnd"); })
      .onProgress([](unsigned int progress, unsigned int total)
                  { Serial.printf("Progress: %u%%\r", (progress / (total / 100))); })
      .onError([](ota_error_t error)
               {
      Serial.printf("Error[%u]: ", error);
      if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
      else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
      else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
      else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
      else if (error == OTA_END_ERROR) Serial.println("End Failed"); });

  ArduinoOTA.begin();
}

// Button / Relay controller task

void remote_loop(void *parameters)
{
  // RemoteLock locks[NO_LOCKS] = {
  //     RemoteLock(new LocalPin(13, true), new LocalPin(16, true), new LocalPin(11, false), new LocalPin(12, false))};

  RemoteLock locks[NO_LOCKS] = {
      RemoteLock(
          new SpiPin(&mcp, 7, true),
          new SpiPin(&mcp, 6, true),
          new SpiPin(&mcp, 5, false),
          new SpiPin(&mcp, 4, false))};

  bool prevstate = 99;
  for (;;)
  {
    for (int i = 0; i < NO_LOCKS; i++)
    {
      // Serial.println("Polling");
      locks[i].poll();
    }
    // Serial.println("Checking msg Queue");
    if (uxQueueMessagesWaiting(queue) > 0)
    {
      char msgc[10];
      xQueueReceive(queue, &msgc, portMAX_DELAY);
      String msg(msgc);
      Serial.printf("Has messages %s\n", msg);
      xQueueReset(queue);
      if (msg.equals("OFF"))
        for (int i = 0; i < NO_LOCKS; i++)
        {
          locks[i].unlock();
        }
      if (msg.equals("ON"))
        for (int i = 0; i < NO_LOCKS; i++)
        {
          locks[i].trylock();
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
  }
}

// MQTT Task

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
    if (!mqttClient.connect("GarageLock"))
    {
      Serial.print("MQTT: Could not connect \n");
    }
    else
    {
      // Use WiFiClient class to create TCP connections
      Serial.print("MQTT: Connected to HASS\n");
      mqttClient.setCallback(onMqttMessage);
      mqttClient.subscribe(SUBJECT);
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
    String msg(reinterpret_cast<const char *>(payload), messageSize);
    Serial.printf("Message: %s\n", msg);
    if (msg.equals("OFF") || msg.equals("ON"))
    {
      Serial.println("Placing message on queue");
      xQueueSend(queue, &msg, portMAX_DELAY);
      Serial.println("Placed message on queue");
    }
  }
  Serial.println("Done processing message with topic ");
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
      String msg(msgc);
      mqttClient.publish(STATUSSUBJECT, msgc);
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

// Entry points

void setup()
{
  Serial.begin(115200);
  delay(10);

  queue = xQueueCreate(10, sizeof(char) * 5);
  outQueue = xQueueCreate(10, sizeof(char) * 5);

  // We start by connecting to a WiFi network
  wifiConnect();
  mqttConnect();

  spi.begin(11, 12, 13, 16);
  mcp.begin();

  delay(500);
  setupOTA();

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
      remote_loop,
      "Remote",
      2000,
      NULL,
      1,
      NULL);

  xTaskCreate(
      updater,
      "Updater",
      2000,
      NULL,
      1,
      NULL);
}

void loop()
{
}