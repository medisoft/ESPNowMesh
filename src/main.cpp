#include <Arduino.h>
#include <ESPMeshNow.h>

ESPMeshNow_t espMeshNow;

void receivedCallback(uint64_t from, String msg)
{
  Serial.printf("startHere: Received from %llX msg=%s\n", from, msg.c_str());
  if (msg.equals("HOLA"))
  {
    // espMeshNow.sendSingle(from, "BIENVENIDO: " + String(esp_random() % 1000));
    espMeshNow.send(espMeshNow.getNodeId(), from, "BIENVENIDO: " + String(esp_random() % 1000));
  }
  // espmeshnow::peers_list_t *p = espMeshNow.getKnownPeers();
  // for (int i = 0; p[i].nodeId != 0; i++)
  // {
  //   Serial.printf("Nodo %llX TTL %d\n", p[i].nodeId, p[i].ttl);
  // }
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
  // espMeshNow.onNewConnection(&newConnectionCallback);
  // espMeshNow.onChangedConnections(&changedConnectionCallback);

  // espMeshNow.sendBroadcast("HOLA");
  espMeshNow.send(espMeshNow.getNodeId(), 0, "HOLA", espmeshnow::ESPMeshNowFlags_e::FORWARD);
}

void loop()
{
  Serial.println("Desperto de deepSleep");
  if (espMeshNow.getNodeId() == 0xCC7B5C36B65C)
  {
    delay(120e3);
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
