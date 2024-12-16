#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <xxh3.h>

#include <ESPMeshNow.h>

RTC_DATA_ATTR uint64_t messageCache[ESP_MESH_NOW_CACHE_ELEMENTS];
RTC_DATA_ATTR int      messageCachePointer = -1;

namespace espmeshnow {
  ESPMeshNow *ESPMeshNow::instance = nullptr;

  send_queue_t  sendQueue[ESP_MESH_NOW_SEND_QUEUE_LEN];
  volatile int  sendQueuePtr       = -1;
  volatile bool newMessageReceived = false;

  nvs_handle_t nvsHandle; // Handle para acceder a NVS

  uint64_t ESPMeshNow::getNodeId() { return nodeId; }
  uint32_t ESPMeshNow::getNodeTime() { return micros() + timeOffset; }
  void     ESPMeshNow::setDebugMsgTypes(uint16_t types) { Log.setLogLevel(types); }

  void ESPMeshNow::addESPNowPeer(esp_now_peer_info_t peer) {
    if (!esp_now_is_peer_exist(peer.peer_addr)) {
      esp_now_peer_num_t num;
      esp_now_get_peer_num(&num);
      if (num.total_num + 1 >= ESP_NOW_MAX_TOTAL_PEER_NUM) {
        uint64_t            leastUsedNodeId = getLeastSeenPeer();
        esp_now_peer_info_t leastUsedPeer;
        addressToMac(leastUsedNodeId, leastUsedPeer.peer_addr);
        if (esp_now_is_peer_exist(peer.peer_addr)) // Remueve el peer si existe, para no ocupar espacio de los limitados que hay
        {
          esp_now_del_peer(peer.peer_addr);
        }
      }
      esp_now_add_peer(&peer);
    }
  }

  void ESPMeshNow::addPeer(uint64_t nodeId) {
    uint16_t i, l = 255;
    int      n = -1;
    // Look for the peer
    for (i = 0; i < ESP_MESH_NOW_PEERLIST_ELEMENTS; i++) {
      if (peersList[i].nodeId == nodeId) {
        peersList[i].ttl = 255;
        n                = i;
      } else if (peersList[i].ttl > 0)
        peersList[i].ttl--;
    }
    if (n >= 0)
      return;
    // If not found, look for a place to put it, looking for an empty spot
    for (i = 0; i < ESP_MESH_NOW_PEERLIST_ELEMENTS; i++) {
      if (peersList[i].nodeId == 0) {
        peersList[i] = {nodeId, 255};
        savePeersToNVS();
        LOGLN("Guardando peer nuevo");
        return;
      }
    }
    // If no empty spot found, then look for the lowest TTL
    for (i = 0; i < ESP_MESH_NOW_PEERLIST_ELEMENTS; i++) {
      if (peersList[i].ttl < l) {
        l = peersList[i].ttl;
        n = i;
      }
    }
    esp_now_peer_info_t peer;
    addressToMac(peersList[n].nodeId, peer.peer_addr);
    if (esp_now_is_peer_exist(peer.peer_addr)) // Remueve el peer si existe, para no ocupar espacio de los limitados que hay
    {
      esp_now_del_peer(peer.peer_addr);
    }
    peersList[n] = {nodeId, 255};
    savePeersToNVS();
    LOGLN("Guardando peer nuevo");
  }

  uint64_t ESPMeshNow::getLeastSeenPeer() {
    uint16_t i, l = 255;
    int      n = -1;

    for (i = 0; i < ESP_MESH_NOW_PEERLIST_ELEMENTS; i++) {
      if (peersList[i].ttl < l) {
        l = peersList[i].ttl;
        n = i;
      }
    }
    return peersList[n].nodeId;
  }
  uint16_t ESPMeshNow::peersCount() {
    uint16_t i, c = 0;
    for (i = 0; i < ESP_MESH_NOW_PEERLIST_ELEMENTS; i++)
      if (peersList[i].nodeId != 0)
        c++;
    return c;
  }

  bool ESPMeshNow::isMessageInCache(esp_mesh_now_packet_t packet, bool addIfNotExists) {
    uint64_t messageHash = XXH64(packet.data, (size_t)(packet.dataLen), 1);
    for (int i = 0; i < ESP_MESH_NOW_CACHE_ELEMENTS; i++)
      if (messageCache[i] == messageHash)
        return true;
    if (addIfNotExists) {
      messageCache[messageCachePointer++] = messageHash;
      if (messageCachePointer >= ESP_MESH_NOW_CACHE_ELEMENTS)
        messageCachePointer = 0;
    }
    return false;
  }

  uint16_t ESPMeshNow::messagesCount() {
    int i, c = 0;
    for (i = 0; i < ESP_MESH_NOW_CACHE_ELEMENTS; i++)
      if (messageCache[i] != 0)
        c++;
    return c;
  }

  uint64_t ESPMeshNow::macToAddress(uint8_t *mac_addr) {
    uint64_t addr = 0;
    for (int i = 0; i < 6; i++) {
      uint64_t b = mac_addr[i];
      addr |= b << (40 - i * 8);
    }
    return addr;
  }
  void ESPMeshNow::addressToMac(uint64_t addr, uint8_t *mac_addr) {
    for (int i = 0; i < 6; i++) {
      mac_addr[i] = (addr >> (40 - i * 8)) & 0xff;
    }
  }

  bool ESPMeshNow::isMyPeer(uint64_t nodeId) {
    for (int i = 0; i < ESP_MESH_NOW_PEERLIST_ELEMENTS; i++) {
      if (peersList[i].nodeId != 0 && peersList[i].nodeId == nodeId)
        return true;
    }
    return false;
  }

  void ESPMeshNow::espNowSendCB(const uint8_t *mac_addr, esp_now_send_status_t status) {
    lastSendError = status;
    if (sentCallback) {
      uint64_t              to = 0;
      esp_mesh_now_packet_t packet;
      to = macToAddress((uint8_t *)mac_addr);
      sentCallback(to, status);
    }
  }

  void ESPMeshNow::espNowRecvCB(const uint8_t *mac_addr, const uint8_t *data, int data_len) {
    uint64_t from = 0;
    from          = macToAddress((uint8_t *)mac_addr);
    esp_mesh_now_packet_t packet;
    memcpy(&packet, data, data_len > sizeof(esp_mesh_now_packet_t) ? sizeof(esp_mesh_now_packet_t) : data_len);
    if (packet.protocolVersion == protocolVersion_e::ESP_MESH_NOW) {
      if (packet.dataLen > sizeof(packet.data)) {
        LOGLN("Paquete corrupto, largo demasiado grande");
      } else {
        newMessageReceived = true;
        if (receivedCallback) {

          addPeer(from);

          packet.data[packet.dataLen] = 0;

          LOGF("Mensaje de %llX (real %llX) para %llX y soy %llX\n", packet.src, from, packet.dst, getNodeId());
          if (isMessageInCache(packet, true)) {
            LOGLN("********** Ya lo tengo en el cache de recientes, no lo proceso");
          } else {
            // SI viene firmado, verifico la firma
            if ((packet.messageFlags & SIGNED) == SIGNED) {
              LOGLN("********** Viene firmado: " + String(packet.messageFlags));
            }

            // Es para mi
            if (packet.dst == getNodeId()) {
              LOGLN("********** Es para mi");
              receivedCallback(from, String((char *)packet.data));
            } else if (packet.dst == 0) // Es para todos
            {
              if (packet.messageFlags & FORWARD == FORWARD) // Es para todos y pide forward
              {
                LOGLN("********** Es para todos y pide forward");
                send(packet.src, packet.dst, String((char *)packet.data), packet.messageFlags);
              } else // Es para todos y no pide forward
              {
                LOGLN("********** Es para todos");
              }
              receivedCallback(from, String((char *)packet.data));
            } else if (isMyPeer(packet.dst)) // Es para alguien mas y es mi vecino
            {
              LOGLN("********** Es para alguien mas y es mi vecino");
              send(packet.src, packet.dst, String((char *)packet.data), packet.messageFlags);
            } else if ((packet.messageFlags & FORWARD) == FORWARD) // Es para alguien mas y pide forward
            {
              LOGLN("********** Es para alguien mas y pide forward");
              send(packet.src, packet.dst, String((char *)packet.data), packet.messageFlags);
            }
          }
        }
      }
    }
  }

  // Guardar la lista de peers en NVS
  void ESPMeshNow::savePeersToNVS() {
    esp_err_t err = nvs_open("espMesh", NVS_READWRITE, &nvsHandle);
    if (err == ESP_OK) {
      unsigned int expectedSize = sizeof(peers_list_t) * ESP_MESH_NOW_PEERLIST_ELEMENTS;
      err                       = nvs_set_blob(nvsHandle, "peer_list", peersList, expectedSize);
      if (err == ESP_OK) {
        nvs_commit(nvsHandle);
        LOGLN("Peers guardados en NVS.");
      } else {
        LOGF("Error guardando peers en NVS: %s\n", esp_err_to_name(err));
      }
      nvs_close(nvsHandle);
    } else {
      LOGF("Error abriendo NVS: %s\n", esp_err_to_name(err));
    }
  }

  // Cargar la lista de peers desde NVS
  void ESPMeshNow::loadPeersFromNVS() {
    esp_err_t    err          = nvs_open("espMesh", NVS_READONLY, &nvsHandle);
    unsigned int expectedSize = sizeof(peers_list_t) * ESP_MESH_NOW_PEERLIST_ELEMENTS;
    if (err == ESP_OK) {
      size_t requiredSize = expectedSize;
      err                 = nvs_get_blob(nvsHandle, "peer_list", peersList, &requiredSize);
      if (err == ESP_OK && requiredSize == expectedSize) {
        LOGLN("Peers cargados desde NVS.");
      } else {
        LOGF("Error cargando peers desde NVS: %s\n", esp_err_to_name(err));
        memset(peersList, 0, expectedSize);
        savePeersToNVS(); // Guarda los valores inicializados
      }
      nvs_close(nvsHandle);
    } else {
      LOGF("Error abriendo NVS: %s\n", esp_err_to_name(err));
      memset(peersList, 0, expectedSize);
    }
  }

  // Guardar la lista de elementos por enviar en NVS
  void ESPMeshNow::saveSendQueueToNVS() {
    if (sendQueuePtr < 0)
      return;
    esp_err_t err = nvs_open("espMesh", NVS_READWRITE, &nvsHandle);
    if (err == ESP_OK) {
      unsigned int expectedSize = sizeof(send_queue_t) * (sendQueuePtr + 1);
      err                       = nvs_set_blob(nvsHandle, "send_queue", sendQueue, expectedSize);
      if (err == ESP_OK) {
        nvs_commit(nvsHandle);
        LOGLN("Send Queue guardado en NVS.");
      } else {
        LOGF("Error guardando Send Queue  en NVS: %s\n", esp_err_to_name(err));
      }
      nvs_close(nvsHandle);
    } else {
      LOGF("Error abriendo NVS: %s\n", esp_err_to_name(err));
    }
  }

  // Cargar la lista de elementos por enviar desde NVS
  void ESPMeshNow::loadSendQueueFromNVS() {
    esp_err_t    err          = nvs_open("espMesh", NVS_READONLY, &nvsHandle);
    unsigned int expectedSize = sizeof(send_queue_t) * ESP_MESH_NOW_SEND_QUEUE_LEN;
    if (err == ESP_OK) {
      size_t requiredSize = expectedSize;
      err                 = nvs_get_blob(nvsHandle, "send_queue", sendQueue, &requiredSize);
      if (err == ESP_OK && requiredSize % sizeof(send_queue_t) == 0) {
        sendQueuePtr = (requiredSize / sizeof(send_queue_t)) - 1;
        LOGLN("Send Queue cargado desde NVS.");
      } else {
        LOGF("Error cargando Send Qeueu desde NVS: %s\n", esp_err_to_name(err));
        saveSendQueueToNVS(); // Guarda los valores inicializados
      }
      nvs_close(nvsHandle);
    } else {
      LOGF("Error abriendo NVS: %s\n", esp_err_to_name(err));
      memset(sendQueue, 0, expectedSize);
    }
  }

  bool ESPMeshNow::init(uint8_t channel, bool cleanNVS) {
    this->instance = this;

    _channel = channel;
    nodeId   = ESP.getEfuseMac();
    nodeId   = __builtin_bswap64(nodeId) >> 16;

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND || cleanNVS) {
      // Borra y vuelve a inicializar NVS si es necesario
      nvs_flash_erase();
      nvs_flash_init();
      if (!cleanNVS)
        return false;
    }

    peersList = (peers_list_t *)malloc(sizeof(peers_list_t) * ESP_MESH_NOW_PEERLIST_ELEMENTS + 1);
    if (peersList == NULL) {
      return false;
    }

    loadPeersFromNVS();
    loadSendQueueFromNVS();

    if (messageCachePointer == -1) {
      LOGLN("Inicializando messageCache");
      memset(messageCache, 0, sizeof(messageCache));
      messageCachePointer = 0;
    }

    if (WiFi.isConnected())
      WiFi.disconnect();
    WiFi.mode(WIFI_STA);

    if (esp_now_init() == ESP_OK) {
      LOG("My mac address is: ");
      LOGF("%llX", getNodeId());
      LOGLN(": ESPNow Init Success");
    } else {
      LOGLN("ESPNow Init Failed");
      free(peersList);
      return false;
    }

    WiFi.channel(_channel);
    esp_wifi_set_channel(_channel, WIFI_SECOND_CHAN_NONE);

    esp_now_register_recv_cb(espNowRecvCBStatic);
    esp_now_register_send_cb(espNowSendCBStatic);

    _initialized = true;
    return true;
  }

  void ESPMeshNow::onReceive(receivedCallback_t onReceive) {
    ESPMeshNow::receivedCallback = onReceive;
  }

  void ESPMeshNow::onSend(sentCallback_t onSend) {
    ESPMeshNow::sentCallback = onSend;
  }

  void ESPMeshNow::onNewConnection(newConnectionCallback_t onNewConnection) {
    ESPMeshNow::newConnectionCallback = onNewConnection;
  }

  void ESPMeshNow::onChangedConnections(changedConnectionsCallback_t onChangedConnection) {
    ESPMeshNow::changedConnectionsCallback = onChangedConnection;
  }

  void ESPMeshNow::onNodeTimeAdjusted(nodeTimeAdjustedCallback_t onNodeTimeAdjusted) {
    ESPMeshNow::nodeTimeAdjustedCallback = onNodeTimeAdjusted;
  }

  esp_err_t ESPMeshNow::send(uint64_t srcId, uint64_t dstId, String msg, uint8_t messageFlags) {
    esp_now_peer_info_t   peer;
    esp_mesh_now_packet_t packet;
    memset(&peer, 0, sizeof(peer));
    // Si hay peer en mi peersList, mando directo, si no, mando broadcast
    if (dstId != 0 && isMyPeer(dstId)) {
      addressToMac(dstId, peer.peer_addr);
    } else {
      memcpy(peer.peer_addr, ESP_MESH_NOW_BROADCAST_ADDR, sizeof(peer.peer_addr));
    }
    peer.channel = _channel;
    peer.encrypt = 0; // no encryption

    if (msg.length() > sizeof(packet.data)) {
      return ESP_ERR_INVALID_SIZE;
    }
    packet.dataLen = msg.length();
    memcpy(packet.data, msg.c_str(), packet.dataLen);
    packet.protocolVersion = ESP_MESH_NOW;
    packet.dst             = dstId;
    packet.src             = getNodeId();
    packet.messageFlags    = messageFlags;
    memset(packet.signature, 0, sizeof(packet.signature));
    uint64_t to = macToAddress((uint8_t *)peer.peer_addr);
    LOGF("Enviando mensaje de %llX para %llX (real %llX): %s\n", srcId, dstId, to, msg.c_str());
    addESPNowPeer(peer);
    esp_wifi_set_channel(peer.channel, WIFI_SECOND_CHAN_NONE);
    lastSendError    = ESP_ERR_NOT_FINISHED;
    esp_err_t result = esp_now_send(peer.peer_addr, (uint8_t *)&packet, sizeof(packet));
    for (int ii = 0; ii < ESP_MESH_NOW_SEND_TIMEOUT_MS && lastSendError == ESP_ERR_NOT_FINISHED; ii++)
      delay(1);
    if (lastSendError != ESP_NOW_SEND_SUCCESS) {
      if (_sendQueueEnabled && (messageFlags & RETRY) == RETRY) {
        if (sendQueuePtr + 1 < ESP_MESH_NOW_SEND_QUEUE_LEN) {
          memcpy(&sendQueue[++sendQueuePtr], &packet, sizeof(packet));
          saveSendQueueToNVS();
        }
      }
      result = ESP_NOW_SEND_FAIL;
    }
    if (result != ESP_OK)
      packetSendResponse(result, &peer);
    return result;
  }

  esp_err_t ESPMeshNow::send(uint64_t srcId, uint64_t dstId, JsonDocument jsonDoc, uint8_t messageFlags) {
    String msg;
    if (measureJson(jsonDoc) > sizeof(esp_mesh_now_packet_t::data)) {
      return ESP_ERR_INVALID_SIZE;
    }
    serializeJson(jsonDoc, msg);
    return send(srcId, dstId, msg, messageFlags | ENCODING_JSON);
  }

  esp_err_t ESPMeshNow::send(uint64_t srcId, uint64_t dstId, uint8_t *data, uint8_t messageFlags) {
    // String msg;
    // serializeJson(jsonDoc, msg);
    // send(srcId, dstId, msg, messageFlags | ENCODING_JSON);
    LOGLN("Not implemented");
    return ESP_ERR_INVALID_STATE;
  }

  void ESPMeshNow::packetSendResponse(esp_err_t result, const esp_now_peer_info_t *peer) {
    const uint8_t *peer_addr = peer->peer_addr;

    LOG("Broadcast Status: ");
    if (result == ESP_OK) {
      LOGLN("Success");
    } else if (result == ESP_ERR_ESPNOW_NOT_INIT) {
      // How did we get so far!!
      LOGLN("ESPNOW not Init.");
    } else if (result == ESP_ERR_ESPNOW_ARG) {
      LOGLN("Invalid Argument");
    } else if (result == ESP_ERR_ESPNOW_INTERNAL) {
      LOGLN("Internal Error");
    } else if (result == ESP_ERR_ESPNOW_NO_MEM) {
      LOGLN("ESP_ERR_ESPNOW_NO_MEM");
    } else if (result == ESP_ERR_ESPNOW_NOT_FOUND) {
      LOGLN("Peer not found.");
      if (!esp_now_is_peer_exist(peer_addr)) {
        LOGLN("Adding peer");
        addESPNowPeer(*peer);
      }
    }
    if (result == ESP_NOW_SEND_FAIL) {
      LOGLN("Failed to send, maybe offline");
    } else {
      LOGLN("Not sure what happened");
    }
  }

  peers_list_t *ESPMeshNow::getKnownPeers() {
    for (int i = 0; i < ESP_MESH_NOW_PEERLIST_ELEMENTS; i++) {
      if (peersList[i].nodeId == 0) {
        peersList[i]        = peersList[i + 1];
        peersList[i].nodeId = 0;
      }
    }
    return peersList;
  }

  bool ESPMeshNow::isRunning() { return _initialized; }
  void ESPMeshNow::enableSendQueue(bool enabled) { _sendQueueEnabled = enabled; }

  void ESPMeshNow::handle() {
    if (!newMessageReceived)
      return;
    newMessageReceived = false;
    if (_sendQueueEnabled && sendQueuePtr >= 0) {
      for (int i = 0; i <= sendQueuePtr; i++) {
        esp_now_peer_info_t peer;
        memset(&peer, 0, sizeof(esp_now_peer_info_t));
        peer.channel = _channel;
        peer.encrypt = 0; // no encryption
        if (sendQueue[i].packet.dst != 0 && isMyPeer(sendQueue[i].packet.dst)) {
          addressToMac(sendQueue[i].packet.dst, peer.peer_addr);
        } else {
          memcpy(peer.peer_addr, ESP_MESH_NOW_BROADCAST_ADDR, sizeof(peer.peer_addr));
        }
        addESPNowPeer(peer);
        lastSendError    = ESP_ERR_NOT_FINISHED;
        esp_err_t result = esp_now_send(peer.peer_addr, (uint8_t *)&sendQueue[i].packet, sizeof(esp_mesh_now_packet_t));
        for (int ii = 0; ii < ESP_MESH_NOW_SEND_TIMEOUT_MS && lastSendError == ESP_ERR_NOT_FINISHED; ii++)
          delay(1);
        if (lastSendError != ESP_NOW_SEND_SUCCESS) {
          LOGF("****** Fallo al reenviar (%d) %s\n", lastSendError, esp_err_to_name(lastSendError));
          sendQueue[i].retries++;
          if (sendQueue[i].retries > ESP_MESH_NOW_SEND_RETRIES) {
            for (int j = i + 1; j <= sendQueuePtr; j++)
              sendQueue[i] = sendQueue[j];
            i--;
            sendQueuePtr--;
            saveSendQueueToNVS();
          }
        } else {
          LOGLN("****** Reenviado OK");
          for (int j = i + 1; j <= sendQueuePtr; j++)
            sendQueue[i] = sendQueue[j];
          i--;
          sendQueuePtr--;
          saveSendQueueToNVS();
        }
      }
      LOGF("SendQueue: %d / %d\n", sendQueuePtr + 1, ESP_MESH_NOW_SEND_QUEUE_LEN);
    }
  }
}; // namespace espmeshnow