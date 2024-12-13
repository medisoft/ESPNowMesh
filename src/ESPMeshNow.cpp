#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <xxh3.h>
#include <esp_now.h>
#include <nvs.h>
#include <nvs_flash.h>

#include <ESPMeshNow.h>

RTC_DATA_ATTR uint64_t messageCache[ESP_MESH_NOW_CACHE_ELEMENTS];
RTC_DATA_ATTR int messageCachePointer = -1;

namespace espmeshnow
{
    ESPMeshNow *ESPMeshNow::instance = nullptr;

    nvs_handle_t nvsHandle; // Handle para acceder a NVS

    uint64_t ESPMeshNow::getNodeId() { return nodeId; }
    uint32_t ESPMeshNow::getNodeTime() { return micros() + timeOffset; }
    void ESPMeshNow::setDebugMsgTypes(uint16_t types) { Log.setLogLevel(types); }

    void ESPMeshNow::addPeer(uint64_t nodeId)
    {
        uint16_t i, l = 255;
        int n = -1;
        // Look for the peer
        for (i = 0; i < ESP_MESH_NOW_PEERLIST_ELEMENTS; i++)
        {
            if (peersList[i].nodeId == nodeId)
            {
                peersList[i].ttl = 255;
                n = i;
            }
            else if (peersList[i].ttl > 0)
                peersList[i].ttl--;
        }
        if (n >= 0)
            return;
        // If not found, look for a place to put it, looking for an empty spot
        for (i = 0; i < ESP_MESH_NOW_PEERLIST_ELEMENTS; i++)
        {
            if (peersList[i].nodeId == 0)
            {
                peersList[i] = {nodeId, 255};
                savePeersToNVS();
                Serial.println("Guardando peer nuevo");
                return;
            }
        }
        // If no empty spot found, then look for the lowest TTL
        for (i = 0; i < ESP_MESH_NOW_PEERLIST_ELEMENTS; i++)
        {
            if (peersList[i].ttl < l)
            {
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
        Serial.println("Guardando peer nuevo");
    }

    uint64_t ESPMeshNow::getLeastSeenPeer()
    {
        uint16_t i, l = 255;
        int n = -1;

        for (i = 0; i < ESP_MESH_NOW_PEERLIST_ELEMENTS; i++)
        {
            if (peersList[i].ttl < l)
            {
                l = peersList[i].ttl;
                n = i;
            }
        }
        return peersList[n].nodeId;
    }
    uint16_t ESPMeshNow::peersCount()
    {
        uint16_t i, c = 0;
        for (i = 0; i < ESP_MESH_NOW_PEERLIST_ELEMENTS; i++)
            if (peersList[i].nodeId != 0)
                c++;
        return c;
    }

    bool ESPMeshNow::isMessageInCache(esp_mesh_now_packet_t packet, bool addIfNotExists)
    {
        uint64_t messageHash = XXH64(packet.data, (size_t)(packet.dataLen), 1);
        for (int i = 0; i < ESP_MESH_NOW_CACHE_ELEMENTS; i++)
            if (messageCache[i] == messageHash)
                return true;
        if (addIfNotExists)
        {
            messageCache[messageCachePointer++] = messageHash;
            if (messageCachePointer >= ESP_MESH_NOW_CACHE_ELEMENTS)
                messageCachePointer = 0;
        }
        return false;
    }

    uint16_t ESPMeshNow::messagesCount()
    {
        int i, c = 0;
        for (i = 0; i < ESP_MESH_NOW_CACHE_ELEMENTS; i++)
            if (messageCache[i] != 0)
                c++;
        return c;
    }

    uint64_t ESPMeshNow::macToAddress(uint8_t *mac_addr)
    {
        uint64_t addr = 0;
        for (int i = 0; i < 6; i++)
        {
            uint64_t b = mac_addr[i];
            addr |= b << (40 - i * 8);
        }
        return addr;
    }
    void ESPMeshNow::addressToMac(uint64_t addr, uint8_t *mac_addr)
    {
        for (int i = 0; i < 6; i++)
        {
            mac_addr[i] = (addr >> (40 - i * 8)) & 0xff;
        }
    }

    bool ESPMeshNow::isMyPeer(uint64_t nodeId)
    {
        for (int i = 0; i < ESP_MESH_NOW_PEERLIST_ELEMENTS; i++)
        {
            if (peersList[i].nodeId != 0 && peersList[i].nodeId == nodeId)
                return true;
        }
        return false;
    }

    void ESPMeshNow::espNowRecvCB(const uint8_t *mac_addr, const uint8_t *data, int data_len)
    {
        if (receivedCallback)
        {
            uint64_t from = 0;
            esp_mesh_now_packet_t packet;
            from = macToAddress((uint8_t *)mac_addr);
            memcpy(&packet, data, data_len > sizeof(esp_mesh_now_packet_t) ? sizeof(esp_mesh_now_packet_t) : data_len);
            if (packet.protocolVersion == protocolVersion_e::ESP_MESH_NOW)
            {
                if (packet.dataLen > sizeof(packet.data))
                {
                    Serial.println("Paquete corrupto, largo demasiado grande");
                }
                else
                {
                    addPeer(from);

                    packet.data[packet.dataLen] = 0;

                    Serial.printf("Mensaje de %llX (real %llX) para %llX y soy %llX\n", packet.src, from, packet.dst, getNodeId());
                    if (isMessageInCache(packet, true))
                    {
                        Serial.println("********** Ya lo tengo en el cache de recientes, no lo proceso");
                    }
                    else
                    {
                        // SI viene firmado, verifico la firma
                        if ((packet.messageFlags & SIGNED) == SIGNED)
                        {
                            Serial.println("********** Viene firmado: " + String(packet.messageFlags));
                        }

                        // Es para mi
                        if (packet.dst == getNodeId())
                        {
                            Serial.println("********** Es para mi");
                            receivedCallback(from, String((char *)packet.data));
                        }
                        else if (packet.dst == 0) // Es para todos
                        {
                            if (packet.messageFlags & FORWARD == FORWARD) // Es para todos y pide forward
                            {
                                Serial.println("********** Es para todos y pide forward");
                                send(packet.src, packet.dst, String((char *)packet.data), packet.messageFlags);
                            }
                            else // Es para todos y no pide forward
                            {
                                Serial.println("********** Es para todos");
                            }
                            receivedCallback(from, String((char *)packet.data));
                        }
                        else if (isMyPeer(packet.dst)) // Es para alguien mas y es mi vecino
                        {
                            Serial.println("********** Es para alguien mas y es mi vecino");
                            send(packet.src, packet.dst, String((char *)packet.data), packet.messageFlags);
                        }
                        else if ((packet.messageFlags & FORWARD) == FORWARD) // Es para alguien mas y pide forward
                        {
                            Serial.println("********** Es para alguien mas y pide forward");
                            send(packet.src, packet.dst, String((char *)packet.data), packet.messageFlags);
                        }
                    }
                }
            }
        }
    }

    // Guardar la lista de peers en NVS
    void ESPMeshNow::savePeersToNVS()
    {
        esp_err_t err = nvs_open("espMesh", NVS_READWRITE, &nvsHandle);
        if (err == ESP_OK)
        {
            unsigned int expectedSize = sizeof(peers_list_t) * ESP_MESH_NOW_PEERLIST_ELEMENTS;
            err = nvs_set_blob(nvsHandle, "peer_list", peersList, expectedSize);
            if (err == ESP_OK)
            {
                nvs_commit(nvsHandle);
                Serial.println("Peers guardados en NVS.");
            }
            else
            {
                Serial.printf("Error guardando peers en NVS: %s\n", esp_err_to_name(err));
            }
            nvs_close(nvsHandle);
        }
        else
        {
            Serial.printf("Error abriendo NVS: %s\n", esp_err_to_name(err));
        }
    }

    // Cargar la lista de peers desde NVS
    void ESPMeshNow::loadPeersFromNVS()
    {
        esp_err_t err = nvs_open("espMesh", NVS_READONLY, &nvsHandle);
        unsigned int expectedSize = sizeof(peers_list_t) * ESP_MESH_NOW_PEERLIST_ELEMENTS;
        if (err == ESP_OK)
        {
            size_t requiredSize = expectedSize;
            err = nvs_get_blob(nvsHandle, "peer_list", peersList, &requiredSize);
            if (err == ESP_OK && requiredSize == expectedSize)
            {
                Serial.println("Peers cargados desde NVS.");
            }
            else
            {
                Serial.printf("Error cargando peers desde NVS: %s\n", esp_err_to_name(err));
                memset(peersList, 0, expectedSize);
                savePeersToNVS(); // Guarda los valores inicializados
            }
            nvs_close(nvsHandle);
        }
        else
        {
            Serial.printf("Error abriendo NVS: %s\n", esp_err_to_name(err));
            memset(peersList, 0, expectedSize);
        }
    }

    bool ESPMeshNow::init(uint8_t channel)
    {
        this->instance = this;

        _channel = channel;
        nodeId = ESP.getEfuseMac();
        nodeId = __builtin_bswap64(nodeId) >> 16;

        esp_err_t err = nvs_flash_init();
        if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
        {
            // Borra y vuelve a inicializar NVS si es necesario
            nvs_flash_erase();
            nvs_flash_init();
            return false;
        }

        peersList = (peers_list_t *)malloc(sizeof(peers_list_t) * ESP_MESH_NOW_PEERLIST_ELEMENTS + 1);
        if (peersList == NULL)
        {
            return false;
        }

        loadPeersFromNVS();

        if (messageCachePointer == -1)
        {
            Serial.println("Inicializando messageCache");
            memset(messageCache, 0, sizeof(messageCache));
            messageCachePointer = 0;
        }

        if (WiFi.isConnected())
            WiFi.disconnect();
        WiFi.mode(WIFI_STA);

        if (esp_now_init() == ESP_OK)
        {
            Serial.print("My mac address is: ");
            Serial.printf("%llX", getNodeId());
            Serial.println(": ESPNow Init Success");
        }
        else
        {
            Serial.println("ESPNow Init Failed");
            free(peersList);
            return false;
        }

        WiFi.channel(_channel);
        esp_wifi_set_channel(_channel, WIFI_SECOND_CHAN_NONE);

        esp_now_register_recv_cb(espNowRecvCBStatic);

        _initialized = true;
        return true;
    }

    void ESPMeshNow::onReceive(receivedCallback_t onReceive)
    {
        ESPMeshNow::receivedCallback = onReceive;
    }

    void ESPMeshNow::onNewConnection(newConnectionCallback_t onNewConnection)
    {
        ESPMeshNow::newConnectionCallback = onNewConnection;
    }

    void ESPMeshNow::onChangedConnections(changedConnectionsCallback_t onChangedConnection)
    {
        ESPMeshNow::changedConnectionsCallback = onChangedConnection;
    }

    void ESPMeshNow::onNodeTimeAdjusted(nodeTimeAdjustedCallback_t onNodeTimeAdjusted)
    {
        ESPMeshNow::nodeTimeAdjustedCallback = onNodeTimeAdjusted;
    }

    void ESPMeshNow::send(uint64_t srcId, uint64_t dstId, String msg, uint8_t messageFlags)
    {
        esp_now_peer_info_t peer;
        esp_mesh_now_packet_t packet;
        memset(&peer, 0, sizeof(peer));
        // Si hay peer en mi peersList, mando directo, si no, mando broadcast
        if (dstId != 0 && isMyPeer(dstId))
        {
            addressToMac(dstId, peer.peer_addr);
        }
        else
        {
            memcpy(peer.peer_addr, ESP_MESH_NOW_BROADCAST_ADDR, sizeof(peer.peer_addr));
        }
        peer.channel = _channel;
        peer.encrypt = 0; // no encryption

        packet.dataLen = msg.length() > sizeof(packet.data) ? sizeof(packet.data) : msg.length();
        memcpy(packet.data, msg.c_str(), packet.dataLen);
        packet.protocolVersion = ESP_MESH_NOW;
        packet.dst = dstId;
        packet.src = getNodeId();
        packet.messageFlags = messageFlags;
        memset(packet.signature, 0, sizeof(packet.signature));
        uint64_t to = macToAddress((uint8_t *)peer.peer_addr);
        Serial.printf("Enviando mensaje de %llX para %llX (real %llX): %s\n", srcId, dstId, to, msg.c_str());
        if (!esp_now_is_peer_exist(peer.peer_addr))
        {
            esp_now_peer_num_t num;
            esp_now_get_peer_num(&num);
            if (num.total_num + 1 >= ESP_NOW_MAX_TOTAL_PEER_NUM)
            {
                uint64_t leastUsedNodeId = getLeastSeenPeer();
                esp_now_peer_info_t leastUsedPeer;
                addressToMac(leastUsedNodeId, leastUsedPeer.peer_addr);
                if (esp_now_is_peer_exist(peer.peer_addr)) // Remueve el peer si existe, para no ocupar espacio de los limitados que hay
                {
                    esp_now_del_peer(peer.peer_addr);
                }
            }
            Serial.println("Adding broadcast address as a peer");
            esp_now_add_peer(&peer);
        }
        esp_wifi_set_channel(peer.channel, WIFI_SECOND_CHAN_NONE);
        esp_err_t result = esp_now_send(peer.peer_addr, (uint8_t *)&packet, sizeof(packet));
        packetSendResponse(result, &peer);
    }
    void ESPMeshNow::packetSendResponse(esp_err_t result, const esp_now_peer_info_t *peer)
    {
        const uint8_t *peer_addr = peer->peer_addr;

        Serial.print("Broadcast Status: ");
        if (result == ESP_OK)
        {
            Serial.println("Success");
        }
        else if (result == ESP_ERR_ESPNOW_NOT_INIT)
        {
            // How did we get so far!!
            Serial.println("ESPNOW not Init.");
        }
        else if (result == ESP_ERR_ESPNOW_ARG)
        {
            Serial.println("Invalid Argument");
        }
        else if (result == ESP_ERR_ESPNOW_INTERNAL)
        {
            Serial.println("Internal Error");
        }
        else if (result == ESP_ERR_ESPNOW_NO_MEM)
        {
            Serial.println("ESP_ERR_ESPNOW_NO_MEM");
        }
        else if (result == ESP_ERR_ESPNOW_NOT_FOUND)
        {
            Serial.println("Peer not found.");
            if (!esp_now_is_peer_exist(peer_addr))
            {
                Serial.println("Adding peer");
                esp_now_add_peer(peer);
            }
        }
        else
        {
            Serial.println("Not sure what happened");
        }
    }
    peers_list_t *ESPMeshNow::getKnownPeers()
    {
        for (int i = 0; i < ESP_MESH_NOW_PEERLIST_ELEMENTS; i++)
        {
            if (peersList[i].nodeId == 0)
            {
                peersList[i] = peersList[i + 1];
                peersList[i].nodeId = 0;
            }
        }
        return peersList;
    }

    bool ESPMeshNow::isRunning() { return _initialized; }
};