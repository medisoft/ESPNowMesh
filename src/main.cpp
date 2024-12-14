#include <Arduino.h>
#include <ArduinoJson.h>
#define ESP_MESH_NOW_DEBUG_LOGGING 0
#include <ESPMeshNow.h>

ESPMeshNow_t espMeshNow;

void receivedCallback(uint64_t from, String msg)
{
  Serial.printf(">>> Received from %llX msg=%s\n", from, msg.c_str());
}

void sentCallback(uint64_t to, esp_now_send_status_t status)
{
  Serial.printf("<<< Sent to %llX msg=(%d) %s\n", to, status, status == ESP_NOW_SEND_SUCCESS ? "SUCCESS" : "FAILURE");
}

void newConnectionCallback(uint32_t nodeId)
{
  Serial.printf("--> startHere: New Connection, nodeId = %u\n", nodeId);
}

void changedConnectionCallback()
{
  Serial.printf("Changed connections\n");
}

void setup()
{
  Serial.begin(115200);
  espMeshNow.init(2);
  espMeshNow.onReceive(&receivedCallback);
  espMeshNow.onSend(&sentCallback);
  // espMeshNow.onNewConnection(&newConnectionCallback);
  // espMeshNow.onChangedConnections(&changedConnectionCallback);

  // espMeshNow.sendBroadcast("HOLA");
  // espMeshNow.send(espMeshNow.getNodeId(), 0, "HOLA", espmeshnow::ESPMeshNowFlags_e::FORWARD);

  // JsonDocument doc;
  // doc["cmd"] = "OTA";
  // doc["sq"] = esp_random();
  // espMeshNow.send(espMeshNow.getNodeId(), 0, doc, espmeshnow::ESPMeshNowFlags_e::FORWARD);
}

void loop()
{
  Serial.println("Desperto de deepSleep");
  if (espMeshNow.getNodeId() == 0xCC7B5C36B65C)
  {
    // delay(120e3);
    while (true)
    {
      if (Serial.available())
      {
        String cmd = Serial.readStringUntil('\n');
        JsonDocument jsonDoc;
        cmd.toUpperCase();
        cmd.trim();
        jsonDoc["cmd"] = cmd;
        jsonDoc["sq"] = esp_random();
        espMeshNow.send(espMeshNow.getNodeId(), 0x5443B2ABF2C0, jsonDoc);
      }
      espMeshNow.handle();
      delay(50);
    }
  }
  else
  {
    for (int i = 0; i < 120; i++)
    {
      espMeshNow.send(espMeshNow.getNodeId(), 0xCC7B5C36B65C, "PAQUETE: " + String(esp_random()));
      delay(10e3);
      espMeshNow.send(espMeshNow.getNodeId(), 0xCC7B5C36B65C, "SIGNED: " + String(esp_random()), espmeshnow::ESPMeshNowFlags_e::SIGNED);
      delay(10e3);
      espMeshNow.send(espMeshNow.getNodeId(), 0xCC7B5C36B65C, "FORWARD TO NODE: " + String(esp_random()), espmeshnow::ESPMeshNowFlags_e::FORWARD);
      delay(10e3);
      espMeshNow.send(espMeshNow.getNodeId(), 0x001122334455, "FORWARD TO OTHER: " + String(esp_random()), espmeshnow::ESPMeshNowFlags_e::FORWARD);
      delay(10e3);
      espMeshNow.send(espMeshNow.getNodeId(), 0x001122334455, "BROADCAST: " + String(esp_random()));
      delay(10e3);
      espMeshNow.send(espMeshNow.getNodeId(), 0x001122334455, "BROADCAST FORWARD: " + String(esp_random()), espmeshnow::ESPMeshNowFlags_e::FORWARD);
      delay(10e3);
    }
  }
  Serial.println("Entrando a deepSleep");
  delay(50);
  esp_deep_sleep(10e6);
}
