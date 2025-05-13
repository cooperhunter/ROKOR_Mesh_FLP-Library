/* ROKOR_Mesh_FLP
 * Copyright 2024 Roman Korotkykh (ROKOR) <romankorotkykh@gmail.com>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef ROKOR_MESH_FLP_H
#define ROKOR_MESH_FLP_H

#include <PJON.h>
#include <strategies/ESPNOW/ESPNOW.h>
#include "esp_now.h"
#include "esp_wifi.h"

// Константы из спецификации
#define ROKOR_MESH_DEFAULT_GATEWAY_ID 1
#define ROKOR_MESH_MAX_NETWORK_NAME_LEN 32
#define ROKOR_MESH_ESPNOW_PMK_LEN 16
#define ROKOR_MESH_MAX_PAYLOAD_SIZE 200

class ROKOR_Mesh;

extern ROKOR_Mesh *global_ROKOR_Mesh_instance;

typedef void (*ROKOR_Mesh_ReceiveCallback)(uint8_t senderId, const uint8_t *payload, uint16_t length, void *custom_ptr);
typedef void (*ROKOR_Mesh_GatewayStatusCallback)(bool connected, void *custom_ptr);
typedef void (*ROKOR_Mesh_NodeStatusCallback)(uint8_t nodeId, bool isConnected, void *custom_ptr);

enum ROKOR_Mesh_Role
{
    ROLE_UNINITIALIZED,
    ROLE_DISCOVERING,
    ROLE_NODE,
    ROLE_GATEWAY,
    ROLE_ERROR
};

class ROKOR_Mesh
{
public:
    ROKOR_Mesh();
    ~ROKOR_Mesh();

    bool begin(const char *networkName, uint8_t espNowChannel = 1, uint8_t pjonIdForGatewayRole = ROKOR_MESH_DEFAULT_GATEWAY_ID);
    void end();

    void setEspNowPmk(const char *pmk);

    void forceRoleNode(uint8_t pjonId, uint8_t gatewayToConnectPjonId = 0);
    void forceRoleGateway(uint8_t pjonId = ROKOR_MESH_DEFAULT_GATEWAY_ID);

    void update();

    bool sendMessage(uint8_t destinationId, const uint8_t *payload, uint16_t length);
    bool sendMessage(const uint8_t *payload, uint16_t length);

    void setReceiveCallback(ROKOR_Mesh_ReceiveCallback callback, void *custom_ptr = nullptr);
    void setGatewayStatusCallback(ROKOR_Mesh_GatewayStatusCallback callback, void *custom_ptr = nullptr);
    void setNodeStatusCallback(ROKOR_Mesh_NodeStatusCallback callback, void *custom_ptr = nullptr);

    ROKOR_Mesh_Role getRole() const;
    uint8_t getPjonId() const;
    const uint8_t *getBusId() const;
    const char *getNetworkName() const;
    bool isNetworkActive() const;

    bool isGatewayConnected() const;

    void setDiscoveryTimeout(uint32_t timeout_ms);
    void setGatewayContentionWindow(uint32_t window_ms);
    void setGatewayAnnounceInterval(uint32_t interval_ms);
    void setNodePingGatewayInterval(uint32_t interval_ms);
    void setNodeMaxGatewayPingAttempts(uint8_t attempts);

private:
    PJON<ESPNOW> _pjon_bus;
    uint8_t _pjon_bus_id[4];
    char _network_name_stored[ROKOR_MESH_MAX_NETWORK_NAME_LEN + 1];
    char _esp_now_pmk[ROKOR_MESH_ESPNOW_PMK_LEN + 1];
    bool _is_custom_pmk_set;

    ROKOR_Mesh_Role _current_role;
    uint8_t _myPjonId;
    uint8_t _gatewayPjonId;
    uint8_t _espNowChannel;
    uint8_t _pjonIdForGatewayUse;
    bool _forced_role_active;

    ROKOR_Mesh_ReceiveCallback _user_receive_cb;
    void *_user_receive_cb_custom_ptr;
    ROKOR_Mesh_GatewayStatusCallback _user_gateway_status_cb;
    void *_user_gateway_status_cb_custom_ptr;
    ROKOR_Mesh_NodeStatusCallback _user_node_status_cb;
    void *_user_node_status_cb_custom_ptr;

    bool _is_begun;

    enum class DiscoveryFSM
    {
        INIT_STATE,
        LOAD_NVS_CONFIG,
        CHECK_FORCED_ROLE,
        LISTEN_FOR_GATEWAY,
        GATEWAY_ELECTION_DELAY,
        ANNOUNCE_AS_GATEWAY,
        REQUEST_NODE_ID,
        OPERATIONAL_NODE,
        OPERATIONAL_GATEWAY,
        ERROR_STATE
    };
    DiscoveryFSM _fsm_state;
    uint32_t _fsm_timer_start;
    uint8_t _my_mac_addr[6];

    uint32_t _discovery_timeout_ms;
    uint32_t _gateway_contention_window_ms;
    uint32_t _gateway_announce_interval_ms;
    uint32_t _node_ping_gateway_interval_ms;
    uint8_t _node_max_gateway_ping_attempts;
    uint32_t _last_gateway_announce_time;

    void initializePjonStack(uint8_t pjon_id, const uint8_t bus_id[4]);
    void hashStringToBytes(const char *str, uint8_t *output_bytes, uint8_t num_bytes);
    void preparePmk(const char *input_pmk_or_network_name, char *output_pmk_buffer);

    void runDiscoveryFSM();

    void loadConfigFromNVS();
    void saveConfigToNVS();
    void clearConfigNVS();

    static void _staticPjonReceiver(uint8_t *payload, uint16_t length, const PJON_Packet_Info &packet_info);
    static void _staticPjonError(uint8_t code, uint16_t data, void *custom_pointer);

    void actualPjonReceiver(uint8_t *payload, uint16_t length, const PJON_Packet_Info &packet_info);
    void actualPjonError(uint8_t code, uint16_t data);

    bool _current_gateway_connected_status;
    uint32_t _last_ack_from_gateway_time;
    uint32_t _next_gateway_ping_time;
    uint8_t _failed_gateway_pings_count;

    static const uint8_t MAX_NODES_PER_GATEWAY = 30;
    struct NodeInfo
    {
        uint8_t pjon_id;
        uint8_t mac_addr[6];
        uint32_t last_seen;
        bool id_assigned_this_session;
    };
    NodeInfo _known_nodes[MAX_NODES_PER_GATEWAY];
    uint8_t _known_nodes_count;
    uint8_t _next_available_node_id_candidate;

    void initNodeManagement();
    void handleNodeIdRequest(const PJON_Packet_Info &request_info, const uint8_t *mac_from_payload);
    void sendPjonIdAssignment(uint8_t assigned_id, const uint8_t target_mac[6]);
    void cleanupInactiveNodes();
    uint8_t findNodeByMac(const uint8_t mac[6]);
    uint8_t findNodeById(uint8_t id);
    void updateNodeStatus(uint8_t nodeId, bool isConnected);

    void espNowInit();
    void espNowDeinit();
    static void _esp_now_on_data_sent(const uint8_t *mac_addr, esp_now_send_status_t status);
    static void _esp_now_on_data_recv(const esp_now_recv_info_t *recv_info, const uint8_t *incoming_data, int len); // Обновленный esp_now_recv_cb
    void addEspNowPeer(const uint8_t *mac_address, uint8_t channel, bool encrypt);

    enum class MeshDiscoveryMessage : uint8_t
    {
        GATEWAY_ANNOUNCE = 0xD1,
        NODE_ID_REQUEST = 0xD2,
        NODE_ID_ASSIGN = 0xD3,
        NODE_ID_ACK = 0xD4,
        NODE_PING_GATEWAY = 0xD5,
        GATEWAY_PONG_NODE = 0xD6
    };
};

#endif // ROKOR_MESH_FLP_H
