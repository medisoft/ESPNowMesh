#include <Arduino.h>
#include <painlessMesh.h>
#include <esp_mesh_now.h>

#define MESH_PREFIX "whateverYouLike"
#define MESH_PASSWORD "somethingSneaky"
#define MESH_PORT 5555

Scheduler userScheduler; // to control your personal task
painlessMesh mesh;
espMeshNow_t espMeshNow;
// User stub
void sendMessage(); // Prototype so PlatformIO doesn't complain

Task taskSendMessage(TASK_SECOND * 1, TASK_FOREVER, &sendMessage);

void sendMessage()
{
  String msg = "Hi from node1";
  msg += mesh.getNodeId();
  mesh.sendBroadcast(msg);
  taskSendMessage.setInterval(random(TASK_SECOND * 1, TASK_SECOND * 5));
}

// Needed for painless library
void receivedCallback(uint64_t from, String msg)
{
  Serial.printf("startHere: Received from %llX msg=%s\n", from, msg.c_str());
  if (msg.equals("HOLA"))
  {
    espMeshNow.sendSingle(from, "BIENVENIDO: " + String(millis() % 1000));
  }
  espmeshnow::peers_list_t *p = espMeshNow.getKnownPeers();
  for (int i = 0; p[i].nodeId != 0; i++)
  {
    Serial.printf("Nodo %llX TTL %d\n", p[i].nodeId, p[i].ttl);
  }
}

void newConnectionCallback(uint32_t nodeId)
{
  Serial.printf("--> startHere: New Connection, nodeId = %u\n", nodeId);
}

void changedConnectionCallback()
{
  Serial.printf("Changed connections\n");
}

void nodeTimeAdjustedCallback(int32_t offset)
{
  Serial.printf("Adjusted time %u. Offset = %d\n", mesh.getNodeTime(), offset);
}

void taskMeshUpdate(void *params)
{
  // mesh.setDebugMsgTypes( ERROR | MESH_STATUS | CONNECTION | SYNC | COMMUNICATION | GENERAL | MSG_TYPES | REMOTE ); // all types on
  mesh.setDebugMsgTypes(ERROR | STARTUP); // set before init() so that you can see startup messages

  mesh.init(MESH_PREFIX, MESH_PASSWORD, &userScheduler, MESH_PORT, WIFI_STA);
  // mesh.onReceive(&receivedCallback);
  mesh.onNewConnection(&newConnectionCallback);
  mesh.onChangedConnections(&changedConnectionCallback);
  mesh.onNodeTimeAdjusted(&nodeTimeAdjustedCallback);

  userScheduler.addTask(taskSendMessage);
  taskSendMessage.enable();
  while (true)
  {
    // it will run the user scheduler as well
    mesh.update();
    taskYIELD();
  }
}

void setup()
{
  Serial.begin(115200);
  espMeshNow.init(2);
  espMeshNow.onReceive(&receivedCallback);
  // espMeshNow.onNewConnection(&newConnectionCallback);
  // espMeshNow.onChangedConnections(&changedConnectionCallback);
  // espMeshNow.onNodeTimeAdjusted(&nodeTimeAdjustedCallback);

  // xTaskCreate(taskMeshUpdate, "taskMeshUpdate", 1 << 13, NULL, 0, NULL);
  espMeshNow.sendBroadcast("HOLA");
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
      // espMeshNow.sendBroadcast(String("Hola-") + espMeshNow.getNodeId() + "-" + millis(), false);
      espMeshNow.sendSingle(0xCC7B5C36B65C, "PAQUETE: " + String(millis()));
      delay(1e3);
    }
  }
  Serial.println("Entrando a deepSleep");
  delay(50);
  esp_deep_sleep(10e6);
}
