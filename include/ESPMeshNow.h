#ifndef _ESP_MESH_NOW_H_
#define _ESP_MESH_NOW_H_
#include <esp_now.h>
#include <ArduinoJson.h>

#ifndef ESP_MESH_NOW_DEBUG_LOGGING
#define ESP_MESH_NOW_DEBUG_LOGGING 1
#endif

#if ESP_MESH_NOW_DEBUG_LOGGING == 1
#define LOG(x) Serial.print(x)
#define LOGLN(x) Serial.println(x)
#define LOGF(...) Serial.printf(__VA_ARGS__)
#else
#define LOG(x)
#define LOGLN(x)
#define LOGF(...)
#endif

#define ESP_MESH_NOW_CACHE_ELEMENTS 500   // 512*4=2kb de RTC
#define ESP_MESH_NOW_PEERLIST_ELEMENTS 50 // 450 bytes de NVS
#define ESP_MESH_NOW_SEND_QUEUE_LEN 10
#define ESP_MESH_NOW_SEND_TIMEOUT_MS 50
#define ESP_MESH_NOW_SEND_RETRIES 50

namespace espmeshnow
{
    const uint8_t ESP_MESH_NOW_BROADCAST_ADDR[] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

    enum protocolVersion_e : uint8_t
    {
        ESP_MESH_NOW = 232
    };
    enum ESPMeshNowFlags_e : uint8_t
    {
        FORWARD = 0b00000001,
        SIGNED = 0b00000010,
        JSONDOC = 0b00000100,
    };
    typedef struct __attribute__((packed)) peers_list_t
    {
        uint64_t nodeId;
        uint8_t ttl;
    } peers_list_t;
    typedef std::function<void(uint64_t nodeId)> newConnectionCallback_t;
    typedef std::function<void(uint64_t nodeId)> droppedConnectionCallback_t;
    typedef std::function<void(uint64_t from, String msg)> receivedCallback_t;
    typedef std::function<void(uint64_t to, esp_now_send_status_t status)> sentCallback_t;
    typedef std::function<void()> changedConnectionsCallback_t;
    typedef std::function<void(int32_t offset)> nodeTimeAdjustedCallback_t;
    typedef std::function<void(uint64_t nodeId, int32_t delay)> nodeDelayCallback_t;
    typedef struct __attribute__((packed)) esp_mesh_now_packet_t
    {
        uint8_t protocolVersion;
        uint8_t messageFlags;
        uint64_t src;
        uint64_t dst;
        byte signature[16];
        uint8_t dataLen;
        byte data[215];
    } esp_mesh_now_packet_t;
    typedef struct send_queue_t
    {
        esp_mesh_now_packet_t packet;
        int retries;
    } send_queue_t;

    class LogClass
    {
    public:
        void Log(String);
        void setLogLevel(uint16_t newTypes);
    };

    class ESPMeshNow
    {
    public:
        bool init(uint8_t channel = 1);
        void onReceive(receivedCallback_t onReceive);
        void onSend(sentCallback_t onSend); // TODO
        void onNewConnection(newConnectionCallback_t onNewConnection);
        void onChangedConnections(changedConnectionsCallback_t onChangedConnections);
        void onNodeTimeAdjusted(nodeTimeAdjustedCallback_t onTimeAdjusted);
        esp_err_t send(uint64_t srcId, uint64_t dstId, String msg, uint8_t messageFlags = 0);
        esp_err_t send(uint64_t srcId, uint64_t dstId, JsonDocument jsonDoc, uint8_t messageFlags = 0);
        esp_err_t send(uint64_t srcId, uint64_t dstId, uint8_t *data, uint8_t messageFlags = 0);
        uint64_t getNodeId();
        uint32_t getNodeTime();
        void setDebugMsgTypes(uint16_t types);
        peers_list_t *getKnownPeers();
        bool isMyPeer(uint64_t nodeId);
        uint64_t macToAddress(uint8_t *mac_addr);
        void addressToMac(uint64_t addr, uint8_t *mac_addr);
        bool isRunning();
        void handle();
        void enableSendQueue(bool enabled = true);
        LogClass Log;

    protected:
        uint8_t _channel = 1;
        uint64_t nodeId;
        uint32_t timeOffset = 0;
        volatile esp_err_t lastSendError = ESP_ERR_NOT_FINISHED;

        receivedCallback_t receivedCallback;
        sentCallback_t sentCallback;
        newConnectionCallback_t newConnectionCallback;
        changedConnectionsCallback_t changedConnectionsCallback;
        nodeTimeAdjustedCallback_t nodeTimeAdjustedCallback;

        void espNowRecvCB(const uint8_t *mac_addr, const uint8_t *data, int data_len);
        void espNowSendCB(const uint8_t *mac_addr, esp_now_send_status_t status);

        void addPeer(uint64_t nodeId);
        uint64_t getLeastSeenPeer();
        uint16_t peersCount();
        bool isMessageInCache(esp_mesh_now_packet_t packet, bool addIfNotExists);
        uint16_t messagesCount();

        void packetSendResponse(esp_err_t result, const esp_now_peer_info_t *peer);

        void loadPeersFromNVS();
        void savePeersToNVS();

    private:
        static ESPMeshNow *instance; // Pointer to the active instance
        static void espNowRecvCBStatic(const uint8_t *mac_addr, const uint8_t *data, int data_len)
        {
            if (instance)
                instance->espNowRecvCB(mac_addr, data, data_len);
        };
        static void espNowSendCBStatic(const uint8_t *mac_addr, esp_now_send_status_t status)
        {
            if (instance)
                instance->espNowSendCB(mac_addr, status);
        };
        peers_list_t *peersList;
        bool _initialized = false;
        bool _sendQueueEnabled = true;
    };

};

using ESPMeshNow_t = espmeshnow::ESPMeshNow;
#endif