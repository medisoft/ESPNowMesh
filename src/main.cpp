
#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESPMeshNow.h>

ESPMeshNow_t espMeshNow;

void receivedCallback(uint64_t from, uint8_t *data, size_t len) {
  Serial.printf(">>> Received from %llX msg=%s\n", from, data);
}
void receivedCallbackJson(uint64_t from, JsonDocument jsonDoc) {
  String msg;
  serializeJson(jsonDoc, msg);
  Serial.printf(">>> Received from %llX msg=%s\n", from, msg.c_str());
}
void receivedCallbackString(uint64_t from, String msg) {
  Serial.printf(">>> Received from %llX msg=%s\n", from, msg.c_str());
}

void sentCallback(uint64_t to, esp_now_send_status_t status) {
  Serial.printf("<<< Sent to %llX status=%s\n", to, status == ESP_NOW_SEND_SUCCESS ? "SUCCESS" : "FAILURE");
}

void newConnectionCallback(uint32_t nodeId) {
  Serial.printf("--> startHere: New Connection, nodeId = %u\n", nodeId);
}

void changedConnectionCallback() {
  Serial.printf("Changed connections\n");
}

void setup() {
  Serial.begin(115200);
  espMeshNow.init(2);
  espMeshNow.onReceive(&receivedCallbackString);
  espMeshNow.onReceive(&receivedCallbackJson);
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

void loop() {
  static int s = 1;
  Serial.println("Desperto de deepSleep");
  if (espMeshNow.getNodeId() == 0xCC7B5C36B65C) {
    // delay(120e3);
    while (true) {
      espMeshNow.handle();
      if (Serial.available() > 0) {
        String       cmd = Serial.readStringUntil('\n');
        JsonDocument jsonDoc;
        cmd.toUpperCase();
        cmd.trim();
        if (cmd.length() > 0) {
          if (cmd.toInt() > 0)
            s = cmd.toInt();
          else {
            jsonDoc["cmd"] = cmd;
            jsonDoc["sq"]  = esp_random();
            if (s == 3) {
              espMeshNow.send(espMeshNow.getNodeId(), 0x4022D8EDC1F0, jsonDoc, espmeshnow::ESPMeshNowFlags_e::RETRY | espmeshnow::ESPMeshNowFlags_e::FORWARD);
              espMeshNow.send(espMeshNow.getNodeId(), 0x5443B2ABF2C0, jsonDoc, espmeshnow::ESPMeshNowFlags_e::RETRY | espmeshnow::ESPMeshNowFlags_e::FORWARD);
            }
            if (s == 4) {
              espMeshNow.send(espMeshNow.getNodeId(), 0x4022D8EDC1F0, jsonDoc, espmeshnow::ESPMeshNowFlags_e::RETRY | espmeshnow::ESPMeshNowFlags_e::FORWARD | espmeshnow::ESPMeshNowFlags_e::ENCODING_PACK);
              espMeshNow.send(espMeshNow.getNodeId(), 0x5443B2ABF2C0, jsonDoc, espmeshnow::ESPMeshNowFlags_e::RETRY | espmeshnow::ESPMeshNowFlags_e::FORWARD | espmeshnow::ESPMeshNowFlags_e::ENCODING_PACK);
            }
            if (s == 1)
              espMeshNow.send(espMeshNow.getNodeId(), 0x5443B2ABF2C0, jsonDoc, espmeshnow::ESPMeshNowFlags_e::RETRY | espmeshnow::ESPMeshNowFlags_e::FORWARD);
            if (s == 2)
              espMeshNow.send(espMeshNow.getNodeId(), 0x5443B2ABF2C0, jsonDoc, espmeshnow::ESPMeshNowFlags_e::RETRY | espmeshnow::ESPMeshNowFlags_e::FORWARD | espmeshnow::ESPMeshNowFlags_e::ENCODING_PACK);
          }
        }
      }
      delay(50);
    }
  } else {
    for (int i = 0; i < 120; i++) {
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
