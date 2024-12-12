#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <xxh3.h>
#include <esp_now.h>
#include <Preferences.h>

#include <esp_mesh_now.h>

RTC_DATA_ATTR uint64_t messageCache[ESP_MESH_NOW_CACHE_ELEMENTS];
RTC_DATA_ATTR int messageCachePointer = -1;

namespace espmeshnow
{
    ESPMeshNow *ESPMeshNow::instance = nullptr;

    Preferences espMeshNowPrefs;

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
                espMeshNowPrefs.putBytes("peer_list", peersList, sizeof(peers_list_t) * ESP_MESH_NOW_PEERLIST_ELEMENTS);
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
        peersList[n] = {nodeId, 255};
        espMeshNowPrefs.putBytes("peer_list", peersList, sizeof(peers_list_t) * ESP_MESH_NOW_PEERLIST_ELEMENTS);
        Serial.println("Guardando peer nuevo");
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

    uint64_t macToAddress(uint8_t *mac_addr)
    {
        uint64_t addr = 0;
        for (int i = 0; i < 6; i++)
        {
            uint64_t b = mac_addr[i];
            addr |= b << (40 - i * 8);
        }
        return addr;
    }
    void addressToMac(uint64_t addr, uint8_t *mac_addr)
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
            // for (int i = 0; i < 6; i++)
            // {
            //     uint64_t b = mac_addr[i];
            //     from |= b << (40 - i * 8);
            // }
            // Serial.printf("Original %02X:%02X:%02X:%02X:%02X:%02X\n", mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
            // Serial.printf("For %llX\n", from);
            from = macToAddress((uint8_t *)mac_addr);
            // Serial.printf("Funcion %llX\n", from);
            // uint8_t mc[6];
            // addressToMac(from, (uint8_t *)&mc);
            // Serial.printf("Inversa %02X:%02X:%02X:%02X:%02X:%02X\n", mc[0], mc[1], mc[2], mc[3], mc[4], mc[5]);
            memcpy(&packet, data, data_len > sizeof(esp_mesh_now_packet_t) ? sizeof(esp_mesh_now_packet_t) : data_len);
            if (packet.protocolVersion == protocolVersion_e::ESP_MESH_NOW)
            {
                Serial.println("Protocolo reconocido");
                if (packet.dataLen > sizeof(packet.data))
                {
                    Serial.println("Paquete corrupto, largo demasiado grande");
                }
                else
                {
                    packet.data[packet.dataLen] = 0;

                    Serial.printf("Mensaje de %llX (real %llX) para %llX y soy %llX\n", packet.src, from, packet.dst, getNodeId());
                    // SI viene firmado, verifico la firma
                    if (packet.messageFlags & SIGNED == SIGNED)
                    {
                        Serial.println("Viene firmado");
                    }

                    // Es para mi
                    if (packet.dst == getNodeId())
                    {
                        Serial.println("Es para mi");
                        receivedCallback(from, String((char *)packet.data));
                    }
                    else if (packet.dst == 0) // Es para todos
                    {
                        if (packet.messageFlags & FORWARD == FORWARD) // Es para todos y pide forward
                        {
                            Serial.println("Es para todos y pide forward");
                        }
                        else // Es para todos y no pide forward
                        {
                            Serial.println("Es para todos");
                            receivedCallback(from, String((char *)packet.data));
                        }
                    }
                    else if (isMyPeer(packet.dst)) // Es para alguien mas y es mi vecino
                    {
                        Serial.println("Es para alguien mas y es mi vecino");
                    }
                    else if (packet.messageFlags & FORWARD == FORWARD) // Es para alguien mas y pide forward
                    {
                        Serial.println("Es para alguien mas y pide forward");
                    }

                    // if (packet.dst == getNodeId()) // Si es para mi, llamo al callback
                    // {
                    //     Serial.println("Es para mi, llamo al CB");
                    //     receivedCallback(from, String((char *)packet.data));
                    // }
                    // else if (true || !isMessageInCache(packet, true)) // Si es nuevo paquete lo reviso y añado al cache
                    // {
                    //     Serial.println("Agregando al cache de mensajes");
                    //     esp_now_peer_info_t peer;

                    //     // Tiene que estar invertidos los bits!
                    //     memcpy(peer.peer_addr, ((uint8_t *)&packet.dst) + 2, sizeof(peer.peer_addr));

                    //     if (packet.dst == 0) // Si ademas es un broadcast, tambien lo recibo yo
                    //         receivedCallback(from, String((char *)packet.data));

                    //     if (packet.messageFlags & FORWARD == FORWARD) // Si trae el flag de distribuir
                    //     {
                    //         // habria que hacer algo para reenviar un paquete completo, sin cambiar el src
                    //         Serial.println("Reenviar por broadcast");
                    //         sendBroadcast(String((char *)packet.data));
                    //     }
                    //     else if (esp_now_is_peer_exist(peer.peer_addr)) // Si el peer esta en mi red
                    //     {
                    //         Serial.println("Reenviar por single");
                    //         sendSingle(packet.dst, String((char *)packet.data));
                    //     }
                    // }
                    // else
                    // {
                    //     Serial.println("Ya esta en cache");
                    // }
                }
            }
            else
            {
                packet.dataLen = data_len > sizeof(packet.data) ? sizeof(packet.data) : data_len;
                memcpy(packet.data, data, packet.dataLen);

                Serial.printf("Protocolo desconocido %u %u %u\n", packet.protocolVersion, sizeof(packet.protocolVersion), sizeof(esp_mesh_now_packet_t));

                Serial.println(isMessageInCache(packet, true) ? "Existe" : "NO existe");
            }
            addPeer(from);
            Serial.printf("Hay %u peers en la lista de detectados\n", peersCount());
            Serial.printf("Hay %u mensajes en cache\n", messagesCount());
        }
    }

    bool ESPMeshNow::init(uint8_t channel)
    {
        this->instance = this;

        _channel = channel;
        nodeId = ESP.getEfuseMac();
        nodeId = __builtin_bswap64(nodeId) >> 16;

        if (!espMeshNowPrefs.begin("espmeshnowprefs", false))
        {
            Serial.println("Error inicializando NVS");
            return false;
        }

        peersList = (peers_list_t *)malloc(sizeof(peers_list_t) * ESP_MESH_NOW_PEERLIST_ELEMENTS + 1);
        if (peersList == NULL)
        {
            return false;
        }

        if (espMeshNowPrefs.isKey("peer_list"))
        {
            espMeshNowPrefs.getBytes("peer_list", peersList, sizeof(peers_list_t) * ESP_MESH_NOW_PEERLIST_ELEMENTS);
        }
        else
        {
            memset(peersList, 0, sizeof(peers_list_t) * ESP_MESH_NOW_PEERLIST_ELEMENTS);
            espMeshNowPrefs.putBytes("peer_list", peersList, sizeof(peers_list_t) * ESP_MESH_NOW_PEERLIST_ELEMENTS);
        }

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

    void ESPMeshNow::sendBroadcast(String msg, uint8_t messageFlags)
    {
        esp_now_peer_info_t broadcastPeer;
        esp_mesh_now_packet_t packet;
        memset(&broadcastPeer, 0, sizeof(broadcastPeer));
        memcpy(broadcastPeer.peer_addr, ESP_MESH_NOW_BROADCAST_ADDR, sizeof(broadcastPeer.peer_addr));
        broadcastPeer.channel = _channel;
        broadcastPeer.encrypt = 0; // no encryption

        packet.dataLen = msg.length() > sizeof(packet.data) ? sizeof(packet.data) : msg.length();
        memcpy(packet.data, msg.c_str(), packet.dataLen);
        packet.protocolVersion = ESP_MESH_NOW;
        packet.dst = 0;
        packet.src = getNodeId();
        packet.messageFlags = messageFlags;
        memset(packet.signature, 0, sizeof(packet.signature));
        Serial.printf("Enviando broadcast: %s\n", msg.c_str());
        if (!esp_now_is_peer_exist(broadcastPeer.peer_addr))
        {
            Serial.println("Adding broadcast address as a peer");
            esp_now_add_peer(&broadcastPeer);
        }
        esp_wifi_set_channel(broadcastPeer.channel, WIFI_SECOND_CHAN_NONE);
        esp_err_t result = esp_now_send(broadcastPeer.peer_addr, (uint8_t *)&packet, sizeof(packet));
        packetSendResponse(result, &broadcastPeer);
    }
    void ESPMeshNow::sendSingle(uint64_t destId, String msg, uint8_t messageFlags)
    {
        esp_now_peer_info_t peer;
        esp_mesh_now_packet_t packet;
        memset(&peer, 0, sizeof(peer));
        // Si hay peer en mi peersList, mando directo, si no, mando broadcast
        if (isMyPeer(destId))
        {
            addressToMac(destId, peer.peer_addr);
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
        packet.dst = destId;
        packet.src = getNodeId();
        packet.messageFlags = messageFlags;
        memset(packet.signature, 0, sizeof(packet.signature));
        Serial.printf("Enviando single: %s\n", msg.c_str());
        if (!esp_now_is_peer_exist(peer.peer_addr))
        {
            Serial.println("Adding broadcast address as a peer");
            esp_now_add_peer(&peer);
        }
        esp_wifi_set_channel(peer.channel, WIFI_SECOND_CHAN_NONE);
        esp_err_t result = esp_now_send(peer.peer_addr, (uint8_t *)&packet, sizeof(packet));
        packetSendResponse(result, &peer);
    }

    void ESPMeshNow::send(uint64_t srcId, uint64_t dstId, String msg, uint8_t messageFlags)
    {
        esp_now_peer_info_t peer;
        esp_mesh_now_packet_t packet;
        memset(&peer, 0, sizeof(peer));
        // Si hay peer en mi peersList, mando directo, si no, mando broadcast
        if (isMyPeer(dstId))
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
        Serial.printf("Enviando mensaje de %llX para %llX: %s\n", srcId, dstId, msg.c_str());
        if (!esp_now_is_peer_exist(peer.peer_addr))
        {
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
};