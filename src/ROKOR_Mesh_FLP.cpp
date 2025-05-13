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

// Раскомментируйте следующую строку, чтобы включить отладочный вывод в Serial порт
#define ROKOR_MESH_DEBUG_SERIAL

#include "ROKOR_Mesh_FLP.h"
#include <Arduino.h>
#include <WiFi.h>     // Используется для WiFi.mode, esp_wifi_get_mac, esp_wifi_set_channel
#include "esp_wifi.h" // Для esp_wifi_get_mac, esp_wifi_set_channel (более низкоуровневые функции)
#include "nvs_flash.h"
#include "nvs.h"
#include <string.h>
#include <algorithm>      // Для std::min
#include "esp_random.h"   // Для esp_random()
#include "mbedtls/sha1.h" // Для хэширования SHA1

// Определение глобального указателя
ROKOR_Mesh *global_ROKOR_Mesh_instance = nullptr;

// Константы для NVS
const char *NVS_NAMESPACE = "rokor_mesh";
const char *NVS_KEY_ROLE = "role";
const char *NVS_KEY_PJON_ID = "pjon_id";
const char *NVS_KEY_BUS_ID = "bus_id";
const char *NVS_KEY_CHANNEL = "channel";
const char *NVS_KEY_NET_NAME = "net_name";
const char *NVS_KEY_PMK_STORE = "pmk_val";
const char *NVS_KEY_GW_ID = "gw_pjonid";
const char *NVS_KEY_GW_MAC = "gw_mac"; // MAC шлюза, к которому подключен узел

// Таймауты и интервалы по умолчанию (могут быть изменены сеттерами)
const uint32_t DEFAULT_DISCOVERY_TIMEOUT_MS = 5000;
const uint32_t DEFAULT_CONTENTION_WINDOW_MS = 1500;
const uint32_t DEFAULT_GATEWAY_ANNOUNCE_INTERVAL_MS = 10000;
const uint32_t DEFAULT_NODE_PING_INTERVAL_MS = 30000;
const uint8_t DEFAULT_NODE_MAX_PING_ATTEMPTS = 3;
const uint32_t GATEWAY_MIN_ANNOUNCE_INTERVAL_MS = 2000;
const uint32_t NODE_ID_REQUEST_TIMEOUT_MS = 5000;
const uint32_t NODE_CLEANUP_INTERVAL_MS = (DEFAULT_NODE_PING_INTERVAL_MS * (DEFAULT_NODE_MAX_PING_ATTEMPTS + 2)) + 10000;
const uint32_t NODE_INACTIVITY_THRESHOLD_MS = DEFAULT_NODE_PING_INTERVAL_MS * (DEFAULT_NODE_MAX_PING_ATTEMPTS + 1);

const uint8_t PJON_RX_WAIT_TIME = 10; // ms, время ожидания для PJON receive

// --- Конструктор и Деструктор ---
ROKOR_Mesh::ROKOR_Mesh() : _is_custom_pmk_set(false),
                           _current_role(ROLE_UNINITIALIZED),
                           _myPjonId(PJON_NOT_ASSIGNED),
                           _gatewayPjonId(PJON_NOT_ASSIGNED),
                           _espNowChannel(1),
                           _pjonIdForGatewayUse(ROKOR_MESH_DEFAULT_GATEWAY_ID),
                           _forced_role_active(false),
                           _user_receive_cb(nullptr),
                           _user_receive_cb_custom_ptr(nullptr),
                           _user_gateway_status_cb(nullptr),
                           _user_gateway_status_cb_custom_ptr(nullptr),
                           _user_node_status_cb(nullptr),
                           _user_node_status_cb_custom_ptr(nullptr),
                           _is_begun(false),
                           _fsm_state(DiscoveryFSM::INIT_STATE),
                           _fsm_timer_start(0),
                           _discovery_timeout_ms(DEFAULT_DISCOVERY_TIMEOUT_MS),
                           _gateway_contention_window_ms(DEFAULT_CONTENTION_WINDOW_MS),
                           _gateway_announce_interval_ms(DEFAULT_GATEWAY_ANNOUNCE_INTERVAL_MS),
                           _node_ping_gateway_interval_ms(DEFAULT_NODE_PING_INTERVAL_MS),
                           _node_max_gateway_ping_attempts(DEFAULT_NODE_MAX_PING_ATTEMPTS),
                           _last_gateway_announce_time(0),
                           _current_gateway_connected_status(false),
                           _last_ack_from_gateway_time(0),
                           _next_gateway_ping_time(0),
                           _failed_gateway_pings_count(0),
                           _known_nodes_count(0),
                           _next_available_node_id_candidate(2),
                           _last_node_cleanup_time(0),
                           _contention_delay_value(0) // Инициализация новой переменной
{
    global_ROKOR_Mesh_instance = this;
    memset(_pjon_bus_id, 0, sizeof(_pjon_bus_id));
    memset(_network_name_stored, 0, sizeof(_network_name_stored));
    memset(_esp_now_pmk, 0, sizeof(_esp_now_pmk));
    memset(_my_mac_addr, 0, sizeof(_my_mac_addr));
    memset(_gateway_mac_addr, 0, sizeof(_gateway_mac_addr));
    initNodeManagement();
}

ROKOR_Mesh::~ROKOR_Mesh()
{
    end();
    global_ROKOR_Mesh_instance = nullptr;
}

// --- Публичные методы ---
bool ROKOR_Mesh::begin(const char *networkName, uint8_t espNowChannel, uint8_t pjonIdForGatewayRole)
{
    if (_is_begun)
    {
#ifdef ROKOR_MESH_DEBUG_SERIAL
        Serial.println(F("[ROKOR_Mesh] Error: Already begun. Call end() first."));
#endif
        return false;
    }

    if (!networkName || strlen(networkName) == 0 || strlen(networkName) > ROKOR_MESH_MAX_NETWORK_NAME_LEN)
    {
#ifdef ROKOR_MESH_DEBUG_SERIAL
        Serial.println(F("[ROKOR_Mesh] Error: Invalid network name."));
#endif
        return false;
    }
    strncpy(_network_name_stored, networkName, ROKOR_MESH_MAX_NETWORK_NAME_LEN);
    _network_name_stored[ROKOR_MESH_MAX_NETWORK_NAME_LEN] = '\0';

    if (espNowChannel < 1 || espNowChannel > 13)
    {
#ifdef ROKOR_MESH_DEBUG_SERIAL
        Serial.printf(F("[ROKOR_Mesh] Warning: Invalid ESP-NOW channel %d. Using default 1.\n"), espNowChannel);
#endif
        _espNowChannel = 1;
    }
    else
    {
        _espNowChannel = espNowChannel;
    }

    _pjonIdForGatewayUse = (pjonIdForGatewayRole == 0 || pjonIdForGatewayRole == PJON_NOT_ASSIGNED) ? ROKOR_MESH_DEFAULT_GATEWAY_ID : pjonIdForGatewayRole;
    if (_pjonIdForGatewayUse > 254)
        _pjonIdForGatewayUse = ROKOR_MESH_DEFAULT_GATEWAY_ID;

    esp_err_t mac_ret = esp_wifi_get_mac(WIFI_IF_STA, _my_mac_addr);
    if (mac_ret != ESP_OK)
    {
#ifdef ROKOR_MESH_DEBUG_SERIAL
        Serial.printf(F("[ROKOR_Mesh] Error: Failed to get MAC address: %s\n"), esp_err_to_name(mac_ret));
#endif
        return false;
    }
#ifdef ROKOR_MESH_DEBUG_SERIAL
    Serial.printf(F("[ROKOR_Mesh] My MAC: %02X:%02X:%02X:%02X:%02X:%02X\n"),
                  _my_mac_addr[0], _my_mac_addr[1], _my_mac_addr[2],
                  _my_mac_addr[3], _my_mac_addr[4], _my_mac_addr[5]);
#endif

    if (!_is_custom_pmk_set)
    {
        preparePmk(_network_name_stored, _esp_now_pmk);
    }

    esp_err_t nvs_err = nvs_flash_init();
    if (nvs_err == ESP_ERR_NVS_NO_FREE_PAGES || nvs_err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
#ifdef ROKOR_MESH_DEBUG_SERIAL
        Serial.println(F("[ROKOR_Mesh] NVS: Erasing and re-initializing."));
#endif
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs_err = nvs_flash_init();
    }
    if (nvs_err != ESP_OK)
    {
#ifdef ROKOR_MESH_DEBUG_SERIAL
        Serial.printf(F("[ROKOR_Mesh] Error: NVS Flash init failed: %s\n"), esp_err_to_name(nvs_err));
#endif
        return false;
    }

    hashStringToBytes(_network_name_stored, _pjon_bus_id, 4);
#ifdef ROKOR_MESH_DEBUG_SERIAL
    Serial.printf(F("[ROKOR_Mesh] PJON Bus ID for network '%s': %d.%d.%d.%d\n"), _network_name_stored, _pjon_bus_id[0], _pjon_bus_id[1], _pjon_bus_id[2], _pjon_bus_id[3]);
#endif

    _is_begun = true;
    _fsm_state = DiscoveryFSM::INIT_STATE;
    _fsm_timer_start = millis();

#ifdef ROKOR_MESH_DEBUG_SERIAL
    Serial.printf(F("[ROKOR_Mesh] Initializing for network: '%s' on channel %d\n"), _network_name_stored, _espNowChannel);
#endif

    WiFi.disconnect(true);
    if (!WiFi.mode(WIFI_STA))
    {
#ifdef ROKOR_MESH_DEBUG_SERIAL
        Serial.println(F("[ROKOR_Mesh] Error: Failed to set WiFi STA mode."));
#endif
        _is_begun = false;
        return false;
    }

    if (!espNowInit())
    {
#ifdef ROKOR_MESH_DEBUG_SERIAL
        Serial.println(F("[ROKOR_Mesh] Error: ESP-NOW initialization failed."));
#endif
        _is_begun = false;
        return false;
    }

    return true;
}

void ROKOR_Mesh::end()
{
    if (!_is_begun)
        return;

#ifdef ROKOR_MESH_DEBUG_SERIAL
    Serial.println(F("[ROKOR_Mesh] Ending network activity..."));
#endif

    _pjon_bus.end();
    espNowDeinit();

    _is_begun = false;
    _current_role = ROLE_UNINITIALIZED;
    _fsm_state = DiscoveryFSM::INIT_STATE;
    _myPjonId = PJON_NOT_ASSIGNED;
    _gatewayPjonId = PJON_NOT_ASSIGNED;
    _current_gateway_connected_status = false;
    _is_custom_pmk_set = false;
    memset(_esp_now_pmk, 0, sizeof(_esp_now_pmk));
    initNodeManagement();
#ifdef ROKOR_MESH_DEBUG_SERIAL
    Serial.println(F("[ROKOR_Mesh] Network activity ended."));
#endif
}

void ROKOR_Mesh::setEspNowPmk(const char *pmk)
{
    if (!pmk || strlen(pmk) == 0)
    {
#ifdef ROKOR_MESH_DEBUG_SERIAL
        Serial.println(F("[ROKOR_Mesh] Warning: Attempted to set an empty PMK. Ignoring."));
#endif
        return;
    }
    if (strlen(pmk) != ROKOR_MESH_ESPNOW_PMK_LEN)
    {
#ifdef ROKOR_MESH_DEBUG_SERIAL
        Serial.printf(F("[ROKOR_Mesh] Warning: PMK length is not %d. It will be truncated/padded.\n"), ROKOR_MESH_ESPNOW_PMK_LEN);
#endif
    }
    preparePmk(pmk, _esp_now_pmk);
    _is_custom_pmk_set = true;
#ifdef ROKOR_MESH_DEBUG_SERIAL
    Serial.println(F("[ROKOR_Mesh] Custom ESP-NOW PMK has been set."));
#endif
}

void ROKOR_Mesh::forceRoleNode(uint8_t pjonId, uint8_t gatewayToConnectPjonId)
{
    if (_is_begun)
    {
#ifdef ROKOR_MESH_DEBUG_SERIAL
        Serial.println(F("[ROKOR_Mesh] Error: Cannot force role after begin(). Call end() first."));
#endif
        return;
    }
    if (pjonId > 254 && pjonId != 0 && pjonId != PJON_NOT_ASSIGNED)
    {
#ifdef ROKOR_MESH_DEBUG_SERIAL
        Serial.printf(F("[ROKOR_Mesh] Warning: Invalid forced PJON ID %d for Node. Using PJON_NOT_ASSIGNED.\n"), pjonId);
#endif
        _myPjonId = PJON_NOT_ASSIGNED;
    }
    else
    {
        _myPjonId = (pjonId == 0) ? PJON_NOT_ASSIGNED : pjonId;
    }

    if (gatewayToConnectPjonId > 254 && gatewayToConnectPjonId != 0 && gatewayToConnectPjonId != PJON_NOT_ASSIGNED)
    {
#ifdef ROKOR_MESH_DEBUG_SERIAL
        Serial.printf(F("[ROKOR_Mesh] Warning: Invalid forced Gateway PJON ID %d. Will try to auto-discover.\n"), gatewayToConnectPjonId);
#endif
        _gatewayPjonId = PJON_NOT_ASSIGNED;
    }
    else
    {
        _gatewayPjonId = (gatewayToConnectPjonId == 0) ? PJON_NOT_ASSIGNED : gatewayToConnectPjonId;
    }

    _current_role = ROLE_NODE;
    _forced_role_active = true;
#ifdef ROKOR_MESH_DEBUG_SERIAL
    Serial.printf(F("[ROKOR_Mesh] Role forced to NODE. PJON ID: %d, Target Gateway ID: %d\n"), _myPjonId, _gatewayPjonId);
#endif
}

void ROKOR_Mesh::forceRoleGateway(uint8_t pjonId)
{
    if (_is_begun)
    {
#ifdef ROKOR_MESH_DEBUG_SERIAL
        Serial.println(F("[ROKOR_Mesh] Error: Cannot force role after begin(). Call end() first."));
#endif
        return;
    }
    if (pjonId > 254 && pjonId != 0 && pjonId != PJON_NOT_ASSIGNED)
    {
#ifdef ROKOR_MESH_DEBUG_SERIAL
        Serial.printf(F("[ROKOR_Mesh] Warning: Invalid forced PJON ID %d for Gateway. Using default %d.\n"), pjonId, ROKOR_MESH_DEFAULT_GATEWAY_ID);
#endif
        _myPjonId = ROKOR_MESH_DEFAULT_GATEWAY_ID;
    }
    else
    {
        _myPjonId = (pjonId == 0 || pjonId == PJON_NOT_ASSIGNED) ? ROKOR_MESH_DEFAULT_GATEWAY_ID : pjonId;
    }
    _pjonIdForGatewayUse = _myPjonId;
    _current_role = ROLE_GATEWAY;
    _forced_role_active = true;
#ifdef ROKOR_MESH_DEBUG_SERIAL
    Serial.printf(F("[ROKOR_Mesh] Role forced to GATEWAY. PJON ID: %d\n"), _myPjonId);
#endif
}

void ROKOR_Mesh::update()
{
    if (!_is_begun)
        return;

    runDiscoveryFSM();

    if (_current_role == ROLE_NODE)
    {
        operateAsNode();
    }
    else if (_current_role == ROLE_GATEWAY)
    {
        operateAsGateway();
    }

    if (_pjon_bus.is_listening())
    {
        _pjon_bus.update();
        _pjon_bus.receive(PJON_RX_WAIT_TIME);
    }
}

bool ROKOR_Mesh::sendMessage(uint8_t destinationId, const uint8_t *payload, uint16_t length)
{
    if (!_is_begun || (_current_role != ROLE_NODE && _current_role != ROLE_GATEWAY))
    {
#ifdef ROKOR_MESH_DEBUG_SERIAL
        Serial.println(F("[ROKOR_Mesh] sendMessage: Network not active or role not operational."));
#endif
        return false;
    }
    if (destinationId == PJON_NOT_ASSIGNED || destinationId > 254)
    {
#ifdef ROKOR_MESH_DEBUG_SERIAL
        Serial.println(F("[ROKOR_Mesh] sendMessage: Invalid destination ID."));
#endif
        return false;
    }
    if (!payload || length == 0)
    {
#ifdef ROKOR_MESH_DEBUG_SERIAL
        Serial.println(F("[ROKOR_Mesh] sendMessage: Empty payload."));
#endif
        return false;
    }
    if (length > ROKOR_MESH_MAX_PAYLOAD_SIZE)
    {
#ifdef ROKOR_MESH_DEBUG_SERIAL
        Serial.printf(F("[ROKOR_Mesh] sendMessage: Payload too long (%d > %d).\n"), length, ROKOR_MESH_MAX_PAYLOAD_SIZE);
#endif
        return false;
    }

    uint8_t target_mac[ESP_NOW_ETH_ALEN];
    bool mac_found = false;

    if (_current_role == ROLE_GATEWAY)
    {
        if (destinationId == PJON_BROADCAST_ADDRESS)
        {
            memcpy(target_mac, _esp_now_broadcast_mac, ESP_NOW_ETH_ALEN);
            mac_found = true;
        }
        else
        {
            int node_idx = findNodeById(destinationId);
            if (node_idx != -1)
            {
                memcpy(target_mac, _known_nodes[node_idx].mac_addr, ESP_NOW_ETH_ALEN);
                mac_found = true;
            }
            else
            {
#ifdef ROKOR_MESH_DEBUG_SERIAL
                Serial.printf(F("[ROKOR_Mesh] sendMessage (GW): Destination node ID %d not found or MAC unknown.\n"), destinationId);
#endif
                return false;
            }
        }
    }
    else if (_current_role == ROLE_NODE)
    {
        if (destinationId == _gatewayPjonId)
        {
            if (memcmp(_gateway_mac_addr, _esp_now_null_mac, ESP_NOW_ETH_ALEN) != 0)
            {
                memcpy(target_mac, _gateway_mac_addr, ESP_NOW_ETH_ALEN);
                mac_found = true;
            }
            else
            {
#ifdef ROKOR_MESH_DEBUG_SERIAL
                Serial.println(F("[ROKOR_Mesh] sendMessage (Node): Gateway MAC unknown."));
#endif
                return false;
            }
        }
        else
        {
#ifdef ROKOR_MESH_DEBUG_SERIAL
            Serial.printf(F("[ROKOR_Mesh] sendMessage (Node): Cannot send to ID %d. Nodes can only send to gateway.\n"), destinationId);
#endif
            return false;
        }
    }

    if (!mac_found)
    {
        return false;
    }

    _pjon_bus.strategy.set_receiver_mac(target_mac);
    _pjon_bus.set_receiver_id(destinationId);

    uint16_t response = _pjon_bus.send(payload, length);

    if (response == PJON_ACK)
    {
#ifdef ROKOR_MESH_DEBUG_SERIAL
        Serial.printf(F("[ROKOR_Mesh] Message to ID %d sent successfully (ACK).\n"), destinationId);
#endif
        return true;
    }
    else if (response == PJON_BUSY)
    {
#ifdef ROKOR_MESH_DEBUG_SERIAL
        Serial.printf(F("[ROKOR_Mesh] Message to ID %d failed: PJON_BUSY.\n"), destinationId);
#endif
    }
    else if (response == PJON_FAIL)
    {
#ifdef ROKOR_MESH_DEBUG_SERIAL
        Serial.printf(F("[ROKOR_Mesh] Message to ID %d failed: PJON_FAIL.\n"), destinationId);
#endif
    }
    else
    {
#ifdef ROKOR_MESH_DEBUG_SERIAL
        Serial.printf(F("[ROKOR_Mesh] Message to ID %d queued (code: %d).\n"), destinationId, response);
#endif
        return true;
    }
    return false;
}

bool ROKOR_Mesh::sendMessage(const uint8_t *payload, uint16_t length)
{
    if (_current_role == ROLE_NODE)
    {
        if (_gatewayPjonId == PJON_NOT_ASSIGNED || !_current_gateway_connected_status)
        {
#ifdef ROKOR_MESH_DEBUG_SERIAL
            Serial.println(F("[ROKOR_Mesh] sendMessage: Node not connected to gateway."));
#endif
            return false;
        }
        return sendMessage(_gatewayPjonId, payload, length);
    }
    else if (_current_role == ROLE_GATEWAY)
    {
#ifdef ROKOR_MESH_DEBUG_SERIAL
        Serial.println(F("[ROKOR_Mesh] sendMessage: Gateway should specify destination ID. Use sendMessage(destId, ...)."));
#endif
        return false;
    }
#ifdef ROKOR_MESH_DEBUG_SERIAL
    Serial.println(F("[ROKOR_Mesh] sendMessage: Role not Node."));
#endif
    return false;
}

void ROKOR_Mesh::setReceiveCallback(ROKOR_Mesh_ReceiveCallback callback, void *custom_ptr)
{
    _user_receive_cb = callback;
    _user_receive_cb_custom_ptr = custom_ptr;
}
void ROKOR_Mesh::setGatewayStatusCallback(ROKOR_Mesh_GatewayStatusCallback callback, void *custom_ptr)
{
    _user_gateway_status_cb = callback;
    _user_gateway_status_cb_custom_ptr = custom_ptr;
}
void ROKOR_Mesh::setNodeStatusCallback(ROKOR_Mesh_NodeStatusCallback callback, void *custom_ptr)
{
    _user_node_status_cb = callback;
    _user_node_status_cb_custom_ptr = custom_ptr;
}

ROKOR_Mesh_Role ROKOR_Mesh::getRole() const { return _current_role; }
uint8_t ROKOR_Mesh::getPjonId() const { return _myPjonId; }
const uint8_t *ROKOR_Mesh::getBusId() const { return _pjon_bus_id; }
const char *ROKOR_Mesh::getNetworkName() const { return _network_name_stored; }
bool ROKOR_Mesh::isNetworkActive() const
{
    return _is_begun && (_current_role == ROLE_NODE || _current_role == ROLE_GATEWAY);
}
bool ROKOR_Mesh::isGatewayConnected() const
{
    return (_current_role == ROLE_NODE) && _current_gateway_connected_status;
}

void ROKOR_Mesh::setDiscoveryTimeout(uint32_t timeout_ms) { _discovery_timeout_ms = timeout_ms; }
void ROKOR_Mesh::setGatewayContentionWindow(uint32_t window_ms) { _gateway_contention_window_ms = std::max(100U, window_ms); }
void ROKOR_Mesh::setGatewayAnnounceInterval(uint32_t interval_ms) { _gateway_announce_interval_ms = std::max(GATEWAY_MIN_ANNOUNCE_INTERVAL_MS, interval_ms); }
void ROKOR_Mesh::setNodePingGatewayInterval(uint32_t interval_ms) { _node_ping_gateway_interval_ms = std::max(1000U, interval_ms); }
void ROKOR_Mesh::setNodeMaxGatewayPingAttempts(uint8_t attempts) { _node_max_gateway_ping_attempts = std::max((uint8_t)1, attempts); }

// --- Приватные методы ---
void ROKOR_Mesh::initializePjonStack(uint8_t pjon_id, const uint8_t bus_id[4], bool is_gateway)
{
    if (_pjon_bus.is_listening())
    {
        _pjon_bus.end();
    }
    _pjon_bus.set_id(pjon_id);
    _pjon_bus.set_bus_id(bus_id[0], bus_id[1], bus_id[2], bus_id[3]);
    _pjon_bus.set_receiver(_staticPjonReceiver);
    _pjon_bus.set_error(_staticPjonError);

    _pjon_bus.strategy.set_channel(_espNowChannel);

    if (is_gateway)
    {
        addEspNowPeer(_esp_now_broadcast_mac, _espNowChannel, strlen(_esp_now_pmk) > 0);
    }
    else
    {
        if (memcmp(_gateway_mac_addr, _esp_now_null_mac, ESP_NOW_ETH_ALEN) != 0)
        {
            addEspNowPeer(_gateway_mac_addr, _espNowChannel, strlen(_esp_now_pmk) > 0);
        }
    }

    _pjon_bus.begin();
    if (_pjon_bus.is_listening())
    {
#ifdef ROKOR_MESH_DEBUG_SERIAL
        Serial.printf(F("[ROKOR_Mesh] PJON stack initialized. ID: %d, Bus: %d.%d.%d.%d, Listening.\n"),
                      _pjon_bus.device_id(), _pjon_bus.bus_id()[0], _pjon_bus.bus_id()[1], _pjon_bus.bus_id()[2], _pjon_bus.bus_id()[3]);
#endif
    }
    else
    {
#ifdef ROKOR_MESH_DEBUG_SERIAL
        Serial.println(F("[ROKOR_Mesh] Error: PJON stack failed to initialize."));
#endif
        _fsm_state = DiscoveryFSM::ERROR_STATE;
    }
}

void ROKOR_Mesh::hashStringToBytes(const char *str, uint8_t *output_bytes, uint8_t num_bytes)
{
    if (!str || !output_bytes || num_bytes == 0)
        return;

    mbedtls_sha1_context ctx;
    unsigned char sha1_result[20];

    mbedtls_sha1_init(&ctx);
    mbedtls_sha1_starts_ret(&ctx);
    mbedtls_sha1_update_ret(&ctx, (const unsigned char *)str, strlen(str));
    mbedtls_sha1_finish_ret(&ctx, sha1_result);
    mbedtls_sha1_free(&ctx);

    memcpy(output_bytes, sha1_result, std::min((uint8_t)20, num_bytes));

#ifdef ROKOR_MESH_DEBUG_SERIAL
    Serial.printf("[ROKOR_Mesh] Hashed '%s' to %d bytes: ", str, std::min((uint8_t)20, num_bytes));
    for (int i = 0; i < std::min((uint8_t)20, num_bytes); i++)
        Serial.printf("%02X", output_bytes[i]);
    Serial.println();
#endif
}

void ROKOR_Mesh::preparePmk(const char *input_string, char *output_pmk_buffer)
{
    if (!input_string || !output_pmk_buffer)
        return;

    memset(output_pmk_buffer, 0, ROKOR_MESH_ESPNOW_PMK_LEN + 1);

    if (strlen(input_string) == ROKOR_MESH_ESPNOW_PMK_LEN)
    {
        strncpy(output_pmk_buffer, input_string, ROKOR_MESH_ESPNOW_PMK_LEN);
    }
    else
    {
        strncpy(output_pmk_buffer, input_string, ROKOR_MESH_ESPNOW_PMK_LEN);
        size_t input_len = strlen(output_pmk_buffer);
        if (input_len < ROKOR_MESH_ESPNOW_PMK_LEN)
        {
            for (size_t i = input_len; i < ROKOR_MESH_ESPNOW_PMK_LEN; ++i)
            {
                output_pmk_buffer[i] = ((i % 4) == 0) ? 'R' : (((i % 4) == 1) ? 'o' : (((i % 4) == 2) ? 'K' : 'r'));
            }
        }
    }
    output_pmk_buffer[ROKOR_MESH_ESPNOW_PMK_LEN] = '\0';
#ifdef ROKOR_MESH_DEBUG_SERIAL
    Serial.printf("[ROKOR_Mesh] Prepared PMK: '");
    for (int i = 0; i < ROKOR_MESH_ESPNOW_PMK_LEN; ++i)
        Serial.print(output_pmk_buffer[i]);
    Serial.println("'");
#endif
}

void ROKOR_Mesh::runDiscoveryFSM()
{
    uint32_t current_time = millis();

    switch (_fsm_state)
    {
    case DiscoveryFSM::INIT_STATE:
#ifdef ROKOR_MESH_DEBUG_SERIAL
        Serial.println(F("[FSM] State: INIT_STATE -> LOAD_NVS_CONFIG"));
#endif
        _fsm_state = DiscoveryFSM::LOAD_NVS_CONFIG;
        _fsm_timer_start = current_time;
        break;

    case DiscoveryFSM::LOAD_NVS_CONFIG:
#ifdef ROKOR_MESH_DEBUG_SERIAL
        Serial.println(F("[FSM] State: LOAD_NVS_CONFIG"));
#endif
        if (loadConfigFromNVS())
        {
#ifdef ROKOR_MESH_DEBUG_SERIAL
            Serial.printf(F("[FSM] Loaded config from NVS. Role: %d, PJON ID: %d, GW ID: %d\n"), _current_role, _myPjonId, _gatewayPjonId);
#endif
            initializePjonStack(_myPjonId, _pjon_bus_id, (_current_role == ROLE_GATEWAY));
            if (!_pjon_bus.is_listening())
            {
                _fsm_state = DiscoveryFSM::ERROR_STATE;
                break;
            }

            if (_current_role == ROLE_NODE)
            {
                if (_gatewayPjonId != PJON_NOT_ASSIGNED && memcmp(_gateway_mac_addr, _esp_now_null_mac, ESP_NOW_ETH_ALEN) != 0)
                {
                    addEspNowPeer(_gateway_mac_addr, _espNowChannel, strlen(_esp_now_pmk) > 0);
                    _current_gateway_connected_status = false;
                    _next_gateway_ping_time = current_time;
                    _failed_gateway_pings_count = 0;
                }
                else
                {
#ifdef ROKOR_MESH_DEBUG_SERIAL
                    Serial.println(F("[FSM] NVS Node: Gateway info missing. Re-discovering."));
#endif
                    _current_role = ROLE_DISCOVERING;
                    _myPjonId = PJON_NOT_ASSIGNED;
                    _fsm_state = DiscoveryFSM::LISTEN_FOR_GATEWAY;
                    _fsm_timer_start = current_time;
                    break;
                }
            }
            else if (_current_role == ROLE_GATEWAY)
            {
                initNodeManagement();
                _last_gateway_announce_time = 0;
            }
            _fsm_state = (_current_role == ROLE_NODE) ? DiscoveryFSM::OPERATIONAL_NODE : DiscoveryFSM::OPERATIONAL_GATEWAY;
#ifdef ROKOR_MESH_DEBUG_SERIAL
            Serial.printf(F("[FSM] LOAD_NVS_CONFIG -> %s\n"), (_fsm_state == DiscoveryFSM::OPERATIONAL_NODE) ? "OPERATIONAL_NODE" : "OPERATIONAL_GATEWAY");
#endif
        }
        else
        {
#ifdef ROKOR_MESH_DEBUG_SERIAL
            Serial.println(F("[FSM] No valid NVS config or network mismatch. -> CHECK_FORCED_ROLE"));
#endif
            clearConfigNVS();
            _current_role = ROLE_UNINITIALIZED;
            _myPjonId = PJON_NOT_ASSIGNED;
            _gatewayPjonId = PJON_NOT_ASSIGNED;
            memset(_gateway_mac_addr, 0, ESP_NOW_ETH_ALEN);
            _fsm_state = DiscoveryFSM::CHECK_FORCED_ROLE;
        }
        _fsm_timer_start = current_time;
        break;

    case DiscoveryFSM::CHECK_FORCED_ROLE:
#ifdef ROKOR_MESH_DEBUG_SERIAL
        Serial.println(F("[FSM] State: CHECK_FORCED_ROLE"));
#endif
        if (_forced_role_active)
        {
#ifdef ROKOR_MESH_DEBUG_SERIAL
            Serial.printf(F("[FSM] Role is forced. Current forced role: %d\n"), _current_role);
#endif
            if (_current_role == ROLE_GATEWAY)
            {
                _myPjonId = _pjonIdForGatewayUse;
                initializePjonStack(_myPjonId, _pjon_bus_id, true);
                if (!_pjon_bus.is_listening())
                {
                    _fsm_state = DiscoveryFSM::ERROR_STATE;
                    break;
                }
                initNodeManagement();
                _last_gateway_announce_time = 0;
                saveConfigToNVS();
                _fsm_state = DiscoveryFSM::OPERATIONAL_GATEWAY;
#ifdef ROKOR_MESH_DEBUG_SERIAL
                Serial.println(F("[FSM] CHECK_FORCED_ROLE (GW) -> OPERATIONAL_GATEWAY"));
#endif
            }
            else if (_current_role == ROLE_NODE)
            {
                initializePjonStack((_myPjonId == 0 || _myPjonId == PJON_NOT_ASSIGNED) ? PJON_NOT_ASSIGNED : _myPjonId, _pjon_bus_id, false);
                if (!_pjon_bus.is_listening())
                {
                    _fsm_state = DiscoveryFSM::ERROR_STATE;
                    break;
                }

                if (_myPjonId == PJON_NOT_ASSIGNED)
                {
                    _fsm_state = DiscoveryFSM::LISTEN_FOR_GATEWAY;
#ifdef ROKOR_MESH_DEBUG_SERIAL
                    Serial.println(F("[FSM] CHECK_FORCED_ROLE (Node, ID needed) -> LISTEN_FOR_GATEWAY"));
#endif
                }
                else
                {
                    if (_gatewayPjonId != PJON_NOT_ASSIGNED)
                    {
                        _fsm_state = DiscoveryFSM::LISTEN_FOR_GATEWAY;
#ifdef ROKOR_MESH_DEBUG_SERIAL
                        Serial.printf(F("[FSM] CHECK_FORCED_ROLE (Node, ID %d, GW ID %d) -> LISTEN_FOR_GATEWAY (to find GW MAC)\n"), _myPjonId, _gatewayPjonId);
#endif
                    }
                    else
                    {
                        _fsm_state = DiscoveryFSM::LISTEN_FOR_GATEWAY;
#ifdef ROKOR_MESH_DEBUG_SERIAL
                        Serial.printf(F("[FSM] CHECK_FORCED_ROLE (Node, ID %d, GW ID unknown) -> LISTEN_FOR_GATEWAY\n"), _myPjonId);
#endif
                    }
                }
            }
            else
            {
                _fsm_state = DiscoveryFSM::LISTEN_FOR_GATEWAY;
#ifdef ROKOR_MESH_DEBUG_SERIAL
                Serial.println(F("[FSM] CHECK_FORCED_ROLE (Unknown forced) -> LISTEN_FOR_GATEWAY"));
#endif
            }
        }
        else
        {
            _current_role = ROLE_DISCOVERING;
            initializePjonStack(PJON_NOT_ASSIGNED, _pjon_bus_id, false);
            if (!_pjon_bus.is_listening())
            {
                _fsm_state = DiscoveryFSM::ERROR_STATE;
                break;
            }
            _fsm_state = DiscoveryFSM::LISTEN_FOR_GATEWAY;
#ifdef ROKOR_MESH_DEBUG_SERIAL
            Serial.println(F("[FSM] CHECK_FORCED_ROLE (Not forced) -> LISTEN_FOR_GATEWAY"));
#endif
        }
        _fsm_timer_start = current_time;
        break;

    case DiscoveryFSM::LISTEN_FOR_GATEWAY:
#ifdef ROKOR_MESH_DEBUG_SERIAL
        Serial.println(F("[FSM] State: LISTEN_FOR_GATEWAY"));
#endif
        if (current_time - _fsm_timer_start > _discovery_timeout_ms)
        {
#ifdef ROKOR_MESH_DEBUG_SERIAL
            Serial.println(F("[FSM] LISTEN_FOR_GATEWAY: Timeout. No gateway found. -> GATEWAY_ELECTION_DELAY"));
#endif
            _fsm_state = DiscoveryFSM::GATEWAY_ELECTION_DELAY;
            _fsm_timer_start = current_time;
            _pjon_bus.end();
        }
        break;

    case DiscoveryFSM::GATEWAY_ELECTION_DELAY:
#ifdef ROKOR_MESH_DEBUG_SERIAL
        Serial.println(F("[FSM] State: GATEWAY_ELECTION_DELAY"));
#endif
        if (_contention_delay_value == 0)
        {
            _contention_delay_value = esp_random() % _gateway_contention_window_ms;
#ifdef ROKOR_MESH_DEBUG_SERIAL
            Serial.printf(F("[FSM] Gateway contention delay: %d ms\n"), _contention_delay_value);
#endif
        }
        if (current_time - _fsm_timer_start > _contention_delay_value)
        {
#ifdef ROKOR_MESH_DEBUG_SERIAL
            Serial.println(F("[FSM] GATEWAY_ELECTION_DELAY: Contention delay passed. -> ANNOUNCE_AS_GATEWAY"));
#endif
            _fsm_state = DiscoveryFSM::ANNOUNCE_AS_GATEWAY;
            _fsm_timer_start = current_time;
            _contention_delay_value = 0;
        }
        break;

    case DiscoveryFSM::ANNOUNCE_AS_GATEWAY:
#ifdef ROKOR_MESH_DEBUG_SERIAL
        Serial.println(F("[FSM] State: ANNOUNCE_AS_GATEWAY -> OPERATIONAL_GATEWAY"));
#endif
        _current_role = ROLE_GATEWAY;
        _myPjonId = _pjonIdForGatewayUse;

        initializePjonStack(_myPjonId, _pjon_bus_id, true);
        if (!_pjon_bus.is_listening())
        {
            _fsm_state = DiscoveryFSM::ERROR_STATE;
            break;
        }

        initNodeManagement();
        sendGatewayAnnounce();
        _last_gateway_announce_time = current_time;

        saveConfigToNVS();
        _fsm_state = DiscoveryFSM::OPERATIONAL_GATEWAY;
        _fsm_timer_start = current_time;
        break;

    case DiscoveryFSM::REQUEST_NODE_ID:
#ifdef ROKOR_MESH_DEBUG_SERIAL
        Serial.println(F("[FSM] State: REQUEST_NODE_ID"));
#endif
        if (current_time - _fsm_timer_start > NODE_ID_REQUEST_TIMEOUT_MS)
        {
#ifdef ROKOR_MESH_DEBUG_SERIAL
            Serial.println(F("[FSM] REQUEST_NODE_ID: Timeout. -> LISTEN_FOR_GATEWAY (to re-evaluate)"));
#endif
            _gatewayPjonId = PJON_NOT_ASSIGNED;
            memset(_gateway_mac_addr, 0, ESP_NOW_ETH_ALEN);
            _fsm_state = DiscoveryFSM::LISTEN_FOR_GATEWAY;
            _fsm_timer_start = current_time;
        }
        break;

    case DiscoveryFSM::OPERATIONAL_NODE:
#ifdef ROKOR_MESH_DEBUG_SERIAL
// Serial.println(F("[FSM] State: OPERATIONAL_NODE - Running")); // Спамит в лог, если часто вызывается
#endif
        break;

    case DiscoveryFSM::OPERATIONAL_GATEWAY:
#ifdef ROKOR_MESH_DEBUG_SERIAL
// Serial.println(F("[FSM] State: OPERATIONAL_GATEWAY - Running")); // Спамит в лог, если часто вызывается
#endif
        break;

    case DiscoveryFSM::ERROR_STATE:
#ifdef ROKOR_MESH_DEBUG_SERIAL
        Serial.println(F("[FSM] State: ERROR_STATE. Halting FSM."));
#endif
        break;
    }
}

bool ROKOR_Mesh::loadConfigFromNVS()
{ // Изменено на bool
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    bool success = false;

    if (err == ESP_OK)
    {
        char stored_net_name[ROKOR_MESH_MAX_NETWORK_NAME_LEN + 1];
        size_t len = sizeof(stored_net_name);
        err = nvs_get_str(nvs_handle, NVS_KEY_NET_NAME, stored_net_name, &len);
        if (err == ESP_OK && strcmp(stored_net_name, _network_name_stored) == 0)
        {
            uint8_t role_val;
            if (nvs_get_u8(nvs_handle, NVS_KEY_ROLE, &role_val) == ESP_OK)
            {
                _current_role = (ROKOR_Mesh_Role)role_val;
            }
            else
            {
                _current_role = ROLE_UNINITIALIZED;
            } // Если не удалось, сбрасываем

            if (nvs_get_u8(nvs_handle, NVS_KEY_PJON_ID, &_myPjonId) != ESP_OK)
            {
                _myPjonId = PJON_NOT_ASSIGNED;
            }

            uint8_t stored_channel;
            if (nvs_get_u8(nvs_handle, NVS_KEY_CHANNEL, &stored_channel) != ESP_OK || stored_channel != _espNowChannel)
            {
#ifdef ROKOR_MESH_DEBUG_SERIAL
                Serial.println(F("[NVS] Channel mismatch or not found. Invalidating NVS config."));
#endif
                nvs_close(nvs_handle);
                return false;
            }

            if (_current_role == ROLE_NODE)
            {
                if (nvs_get_u8(nvs_handle, NVS_KEY_GW_ID, &_gatewayPjonId) != ESP_OK)
                {
                    _gatewayPjonId = PJON_NOT_ASSIGNED;
                }
                len = ESP_NOW_ETH_ALEN;
                if (nvs_get_blob(nvs_handle, NVS_KEY_GW_MAC, _gateway_mac_addr, &len) != ESP_OK)
                {
                    memset(_gateway_mac_addr, 0, ESP_NOW_ETH_ALEN);
                }
                // Если роль узел, но нет информации о шлюзе, конфигурация неполная
                if (_gatewayPjonId == PJON_NOT_ASSIGNED || memcmp(_gateway_mac_addr, _esp_now_null_mac, ESP_NOW_ETH_ALEN) == 0)
                {
#ifdef ROKOR_MESH_DEBUG_SERIAL
                    Serial.println(F("[NVS] Node role loaded, but gateway info is missing/invalid."));
#endif
                    // Не считаем это полным успехом, FSM должен будет переопределить
                }
            }
            success = (_current_role == ROLE_NODE || _current_role == ROLE_GATEWAY); // Успех, если роль определена
#ifdef ROKOR_MESH_DEBUG_SERIAL
            if (success)
                Serial.println(F("[NVS] Configuration loaded successfully."));
#endif
        }
        else
        {
#ifdef ROKOR_MESH_DEBUG_SERIAL
            Serial.println(F("[NVS] Network name mismatch or not found. Config not loaded."));
#endif
        }
        nvs_close(nvs_handle);
    }
    else
    {
#ifdef ROKOR_MESH_DEBUG_SERIAL
        Serial.printf(F("[NVS] Failed to open NVS: %s. No config loaded.\n"), esp_err_to_name(err));
#endif
    }
    return success;
}

void ROKOR_Mesh::saveConfigToNVS()
{
    if (_current_role == ROLE_UNINITIALIZED || _current_role == ROLE_DISCOVERING || _current_role == ROLE_ERROR)
    {
        return;
    }
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err == ESP_OK)
    {
        nvs_set_str(nvs_handle, NVS_KEY_NET_NAME, _network_name_stored);
        nvs_set_u8(nvs_handle, NVS_KEY_ROLE, (uint8_t)_current_role);
        nvs_set_u8(nvs_handle, NVS_KEY_PJON_ID, _myPjonId);
        nvs_set_blob(nvs_handle, NVS_KEY_BUS_ID, _pjon_bus_id, 4);
        nvs_set_u8(nvs_handle, NVS_KEY_CHANNEL, _espNowChannel);

        if (_current_role == ROLE_NODE)
        {
            nvs_set_u8(nvs_handle, NVS_KEY_GW_ID, _gatewayPjonId);
            if (memcmp(_gateway_mac_addr, _esp_now_null_mac, ESP_NOW_ETH_ALEN) != 0)
            {
                nvs_set_blob(nvs_handle, NVS_KEY_GW_MAC, _gateway_mac_addr, ESP_NOW_ETH_ALEN);
            }
        }

        err = nvs_commit(nvs_handle);
#ifdef ROKOR_MESH_DEBUG_SERIAL
        if (err == ESP_OK)
        {
            Serial.println(F("[NVS] Configuration saved."));
        }
        else
        {
            Serial.printf(F("[NVS] Failed to commit NVS: %s\n"), esp_err_to_name(err));
        }
#endif
        nvs_close(nvs_handle);
    }
    else
    {
#ifdef ROKOR_MESH_DEBUG_SERIAL
        Serial.printf(F("[NVS] Failed to open NVS for writing: %s\n"), esp_err_to_name(err));
#endif
    }
}

void ROKOR_Mesh::clearConfigNVS()
{
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err == ESP_OK)
    {
        nvs_erase_key(nvs_handle, NVS_KEY_NET_NAME);
        nvs_erase_key(nvs_handle, NVS_KEY_ROLE);
        nvs_erase_key(nvs_handle, NVS_KEY_PJON_ID);
        nvs_erase_key(nvs_handle, NVS_KEY_BUS_ID);
        nvs_erase_key(nvs_handle, NVS_KEY_CHANNEL);
        nvs_erase_key(nvs_handle, NVS_KEY_GW_ID);
        nvs_erase_key(nvs_handle, NVS_KEY_GW_MAC);
        err = nvs_commit(nvs_handle);
#ifdef ROKOR_MESH_DEBUG_SERIAL
        if (err == ESP_OK)
        {
            Serial.println(F("[NVS] Configuration cleared."));
        }
        else
        {
            Serial.printf(F("[NVS] Failed to commit NVS erase: %s\n"), esp_err_to_name(err));
        }
#endif
        nvs_close(nvs_handle);
    }
    else
    {
#ifdef ROKOR_MESH_DEBUG_SERIAL
        Serial.printf(F("[NVS] Failed to open NVS for clearing: %s\n"), esp_err_to_name(err));
#endif
    }
}

// ESP-NOW статические callback-функции
void ROKOR_Mesh::_esp_now_on_data_sent(const uint8_t *mac_addr, esp_now_send_status_t status)
{
    if (global_ROKOR_Mesh_instance)
    {
        global_ROKOR_Mesh_instance->_pjon_bus.strategy.esp_now_send_callback(mac_addr, status);
    }
}

void ROKOR_Mesh::_esp_now_on_data_recv(const esp_now_recv_info_t *recv_info, const uint8_t *incoming_data, int len)
{
    if (global_ROKOR_Mesh_instance && recv_info && incoming_data && len > 0)
    {
        global_ROKOR_Mesh_instance->_pjon_bus.strategy.esp_now_receive_callback(recv_info->src_addr, incoming_data, len);
    }
}

bool ROKOR_Mesh::espNowInit()
{
    esp_err_t channel_err = esp_wifi_set_channel(_espNowChannel, WIFI_SECOND_CHAN_NONE);
    if (channel_err != ESP_OK)
    {
#ifdef ROKOR_MESH_DEBUG_SERIAL
        Serial.printf(F("[ROKOR_Mesh] Failed to set ESP-NOW channel %d: %s\n"), _espNowChannel, esp_err_to_name(channel_err));
#endif
        return false;
    }
#ifdef ROKOR_MESH_DEBUG_SERIAL
    Serial.printf(F("[ROKOR_Mesh] ESP-NOW channel set to: %d\n"), _espNowChannel);
#endif

    if (esp_now_init() != ESP_OK)
    {
#ifdef ROKOR_MESH_DEBUG_SERIAL
        Serial.println(F("[ROKOR_Mesh] Error initializing ESP-NOW"));
#endif
        return false;
    }

    if (strlen(_esp_now_pmk) > 0)
    {
        if (esp_now_set_pmk((const uint8_t *)_esp_now_pmk) != ESP_OK)
        {
#ifdef ROKOR_MESH_DEBUG_SERIAL
            Serial.println(F("[ROKOR_Mesh] Error setting ESP-NOW PMK. Encryption might fail."));
#endif
        }
        else
        {
#ifdef ROKOR_MESH_DEBUG_SERIAL
            Serial.println(F("[ROKOR_Mesh] ESP-NOW PMK set. Link will be encrypted if peer also has PMK."));
#endif
        }
    }
    else
    {
#ifdef ROKOR_MESH_DEBUG_SERIAL
        Serial.println(F("[ROKOR_Mesh] No PMK set for ESP-NOW. Link will be unencrypted."));
#endif
    }

    if (esp_now_register_send_cb(_esp_now_on_data_sent) != ESP_OK)
    {
#ifdef ROKOR_MESH_DEBUG_SERIAL
        Serial.println(F("[ROKOR_Mesh] Error registering ESP-NOW send callback"));
#endif
        esp_now_deinit();
        return false;
    }
    if (esp_now_register_recv_cb(_esp_now_on_data_recv) != ESP_OK)
    {
#ifdef ROKOR_MESH_DEBUG_SERIAL
        Serial.println(F("[ROKOR_Mesh] Error registering ESP-NOW receive callback"));
#endif
        esp_now_unregister_send_cb();
        esp_now_deinit();
        return false;
    }
#ifdef ROKOR_MESH_DEBUG_SERIAL
    Serial.println(F("[ROKOR_Mesh] ESP-NOW initialized successfully."));
#endif
    return true;
}

void ROKOR_Mesh::espNowDeinit()
{
    esp_now_unregister_recv_cb();
    esp_now_unregister_send_cb();
    esp_now_deinit();
#ifdef ROKOR_MESH_DEBUG_SERIAL
    Serial.println(F("[ROKOR_Mesh] ESP-NOW de-initialized."));
#endif
}

void ROKOR_Mesh::addEspNowPeer(const uint8_t *mac_address, uint8_t channel, bool encrypt_link)
{
    if (!mac_address)
        return;
#ifdef ROKOR_MESH_DEBUG_SERIAL
    Serial.printf("[ROKOR_Mesh] Adding/Modifying ESP-NOW peer: %02X:%02X:%02X:%02X:%02X:%02X on channel %d, encrypt: %d\n",
                  mac_address[0], mac_address[1], mac_address[2], mac_address[3], mac_address[4], mac_address[5], channel, encrypt_link);
#endif

    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, mac_address, ESP_NOW_ETH_ALEN);
    peerInfo.channel = channel;
    peerInfo.ifidx = WIFI_IF_STA;
    peerInfo.encrypt = encrypt_link && (strlen(_esp_now_pmk) > 0);

    if (esp_now_is_peer_exist(mac_address))
    {
        esp_err_t mod_err = esp_now_mod_peer(&peerInfo);
        if (mod_err != ESP_OK)
        {
#ifdef ROKOR_MESH_DEBUG_SERIAL
            Serial.printf(F("[ROKOR_Mesh] Failed to modify ESP-NOW peer: %s. Trying del/add.\n"), esp_err_to_name(mod_err));
#endif
            esp_now_del_peer(mac_address);
            esp_err_t add_err = esp_now_add_peer(&peerInfo);
#ifdef ROKOR_MESH_DEBUG_SERIAL
            if (add_err != ESP_OK)
            {
                Serial.printf(F("[ROKOR_Mesh] Failed to add ESP-NOW peer (after del): %s\n"), esp_err_to_name(add_err));
            }
            else
            {
                Serial.println(F("[ROKOR_Mesh] ESP-NOW peer added (after del/mod fail)."));
            }
#endif
        }
        else
        {
#ifdef ROKOR_MESH_DEBUG_SERIAL
            Serial.println(F("[ROKOR_Mesh] ESP-NOW peer modified."));
#endif
        }
    }
    else
    {
        esp_err_t add_err = esp_now_add_peer(&peerInfo);
#ifdef ROKOR_MESH_DEBUG_SERIAL
        if (add_err != ESP_OK)
        {
            Serial.printf(F("[ROKOR_Mesh] Failed to add ESP-NOW peer: %s\n"), esp_err_to_name(add_err));
        }
        else
        {
            Serial.println(F("[ROKOR_Mesh] ESP-NOW peer added."));
        }
#endif
    }
}

// --- Статические callback-функции PJON ---
void ROKOR_Mesh::_staticPjonReceiver(uint8_t *payload, uint16_t length, const PJON_Packet_Info &packet_info)
{
    if (global_ROKOR_Mesh_instance)
    {
        global_ROKOR_Mesh_instance->actualPjonReceiver(payload, length, packet_info);
    }
}
void ROKOR_Mesh::_staticPjonError(uint8_t code, uint16_t data, void *custom_pointer)
{
    if (global_ROKOR_Mesh_instance)
    {
        global_ROKOR_Mesh_instance->actualPjonError(code, data);
    }
}

// --- Методы-члены для обработки вызовов от PJON ---
void ROKOR_Mesh::actualPjonReceiver(uint8_t *payload, uint16_t length, const PJON_Packet_Info &packet_info)
{
    if (!payload || length == 0)
        return;

    MeshDiscoveryMessage msg_type = (MeshDiscoveryMessage)payload[0];
    const uint8_t *actual_payload = payload + 1;
    uint16_t actual_length = length - 1;

#ifdef ROKOR_MESH_DEBUG_SERIAL
    Serial.printf("[PJON RX] From ID: %d, MAC: %02X:%02X:%02X:%02X:%02X:%02X, Len: %d, Type: 0x%02X\n",
                  packet_info.sender_id,
                  packet_info.sender_ethernet_address[0], packet_info.sender_ethernet_address[1], packet_info.sender_ethernet_address[2],
                  packet_info.sender_ethernet_address[3], packet_info.sender_ethernet_address[4], packet_info.sender_ethernet_address[5],
                  length, payload[0]);
#endif

    if (_fsm_state == DiscoveryFSM::LISTEN_FOR_GATEWAY || _fsm_state == DiscoveryFSM::GATEWAY_ELECTION_DELAY ||
        (_fsm_state == DiscoveryFSM::CHECK_FORCED_ROLE && _current_role == ROLE_NODE && (_myPjonId == PJON_NOT_ASSIGNED || _myPjonId == 0)))
    {
        if (msg_type == MeshDiscoveryMessage::GATEWAY_ANNOUNCE && actual_length >= ESP_NOW_ETH_ALEN)
        {
            _gatewayPjonId = packet_info.sender_id;
            memcpy(_gateway_mac_addr, actual_payload, ESP_NOW_ETH_ALEN);

#ifdef ROKOR_MESH_DEBUG_SERIAL
            Serial.printf(F("[FSM RX] GATEWAY_ANNOUNCE from ID %d, MAC: %02X:%02X:%02X:%02X:%02X:%02X\n"),
                          _gatewayPjonId, _gateway_mac_addr[0], _gateway_mac_addr[1], _gateway_mac_addr[2], _gateway_mac_addr[3], _gateway_mac_addr[4], _gateway_mac_addr[5]);
#endif

            addEspNowPeer(_gateway_mac_addr, _espNowChannel, strlen(_esp_now_pmk) > 0);

            if (_current_role == ROLE_DISCOVERING || (_forced_role_active && _current_role == ROLE_NODE))
            {
                if (_myPjonId == PJON_NOT_ASSIGNED || _myPjonId == 0)
                {
#ifdef ROKOR_MESH_DEBUG_SERIAL
                    Serial.println(F("[FSM RX] GW Announce: My ID is not assigned. -> REQUEST_NODE_ID"));
#endif
                    _fsm_state = DiscoveryFSM::REQUEST_NODE_ID;
                    _fsm_timer_start = millis();
                    sendNodeIdRequest();
                }
                else
                {
#ifdef ROKOR_MESH_DEBUG_SERIAL
                    Serial.printf(F("[FSM RX] GW Announce: My ID is %d. -> OPERATIONAL_NODE\n"), _myPjonId);
#endif
                    _current_role = ROLE_NODE;
                    saveConfigToNVS();
                    _fsm_state = DiscoveryFSM::OPERATIONAL_NODE;
                    _current_gateway_connected_status = false;
                    _next_gateway_ping_time = millis();
                    _failed_gateway_pings_count = 0;
                }
            }
            return;
        }
    }

    if (_current_role == ROLE_GATEWAY)
    {
        if (msg_type == MeshDiscoveryMessage::NODE_ID_REQUEST && actual_length >= ESP_NOW_ETH_ALEN)
        {
            const uint8_t *node_mac = actual_payload;
#ifdef ROKOR_MESH_DEBUG_SERIAL
            Serial.printf(F("[GW RX] NODE_ID_REQUEST from MAC: %02X:%02X:%02X:%02X:%02X:%02X (PJON ID: %d)\n"),
                          node_mac[0], node_mac[1], node_mac[2], node_mac[3], node_mac[4], node_mac[5], packet_info.sender_id);
#endif
            handleNodeIdRequest(packet_info, node_mac);
        }
        else if (msg_type == MeshDiscoveryMessage::NODE_ID_ACK)
        {
            int node_idx = findNodeById(packet_info.sender_id);
            if (node_idx != -1)
            {
                _known_nodes[node_idx].id_assigned_this_session = false;
                _known_nodes[node_idx].last_seen = millis();
                updateNodeStatus(packet_info.sender_id, true, "ID_ACK");
#ifdef ROKOR_MESH_DEBUG_SERIAL
                Serial.printf(F("[GW RX] NODE_ID_ACK from Node ID %d.\n"), packet_info.sender_id);
#endif
            }
        }
        else if (msg_type == MeshDiscoveryMessage::NODE_PING_GATEWAY)
        {
            int node_idx = findNodeById(packet_info.sender_id);
            if (node_idx != -1)
            {
                _known_nodes[node_idx].last_seen = millis();
                uint8_t pong_payload[] = {(uint8_t)MeshDiscoveryMessage::GATEWAY_PONG_NODE};
                _pjon_bus.strategy.set_receiver_mac(_known_nodes[node_idx].mac_addr);
                _pjon_bus.set_receiver_id(packet_info.sender_id);
                _pjon_bus.send(pong_payload, sizeof(pong_payload));
                updateNodeStatus(packet_info.sender_id, true, "PING");
#ifdef ROKOR_MESH_DEBUG_SERIAL
                Serial.printf(F("[GW RX] NODE_PING from Node ID %d. Sent PONG.\n"), packet_info.sender_id);
#endif
            }
            else
            {
#ifdef ROKOR_MESH_DEBUG_SERIAL
                Serial.printf(F("[GW RX] NODE_PING from unknown Node ID %d. Ignoring.\n"), packet_info.sender_id);
#endif
            }
        }
        else
        {
            if (_user_receive_cb)
            {
                _user_receive_cb(packet_info.sender_id, payload, length, _user_receive_cb_custom_ptr);
            }
        }
    }
    else if (_current_role == ROLE_NODE)
    {
        if (packet_info.sender_id == _gatewayPjonId)
        {
            if (msg_type == MeshDiscoveryMessage::NODE_ID_ASSIGN && actual_length >= 1 + ESP_NOW_ETH_ALEN)
            {
                uint8_t assigned_id = actual_payload[0];
                const uint8_t *target_mac = actual_payload + 1;

                if (memcmp(target_mac, _my_mac_addr, ESP_NOW_ETH_ALEN) == 0)
                {
#ifdef ROKOR_MESH_DEBUG_SERIAL
                    Serial.printf(F("[Node RX] NODE_ID_ASSIGN received. Assigned ID: %d\n"), assigned_id);
#endif
                    _myPjonId = assigned_id;
                    _pjon_bus.set_id(_myPjonId);

                    sendNodeIdAck();

                    _current_role = ROLE_NODE;
                    saveConfigToNVS();
                    _fsm_state = DiscoveryFSM::OPERATIONAL_NODE;
                    _current_gateway_connected_status = true;
                    _last_ack_from_gateway_time = millis();
                    _failed_gateway_pings_count = 0;
                    _next_gateway_ping_time = millis() + _node_ping_gateway_interval_ms;
                    if (_user_gateway_status_cb)
                    {
                        _user_gateway_status_cb(true, _user_gateway_status_cb_custom_ptr);
                    }
                }
            }
            else if (msg_type == MeshDiscoveryMessage::GATEWAY_PONG_NODE)
            {
#ifdef ROKOR_MESH_DEBUG_SERIAL
                Serial.printf(F("[Node RX] GATEWAY_PONG from Gateway ID %d.\n"), _gatewayPjonId);
#endif
                _last_ack_from_gateway_time = millis();
                _failed_gateway_pings_count = 0;
                if (!_current_gateway_connected_status)
                {
                    _current_gateway_connected_status = true;
                    if (_user_gateway_status_cb)
                    {
                        _user_gateway_status_cb(true, _user_gateway_status_cb_custom_ptr);
                    }
#ifdef ROKOR_MESH_DEBUG_SERIAL
                    Serial.println(F("[Node] Connection to gateway RESTORED."));
#endif
                }
            }
            else if (msg_type == MeshDiscoveryMessage::GATEWAY_ANNOUNCE)
            {
                if (packet_info.sender_id == _gatewayPjonId && actual_length >= ESP_NOW_ETH_ALEN)
                {
                    memcpy(_gateway_mac_addr, actual_payload, ESP_NOW_ETH_ALEN);
                    addEspNowPeer(_gateway_mac_addr, _espNowChannel, strlen(_esp_now_pmk) > 0);
                }
            }
            else
            {
                if (_user_receive_cb)
                {
                    _user_receive_cb(packet_info.sender_id, payload, length, _user_receive_cb_custom_ptr);
                }
            }
        }
        else
        {
#ifdef ROKOR_MESH_DEBUG_SERIAL
            Serial.printf(F("[Node RX] Message from other Node ID %d. Ignoring.\n"), packet_info.sender_id);
#endif
        }
    }
}

void ROKOR_Mesh::actualPjonError(uint8_t code, uint16_t data)
{
#ifdef ROKOR_MESH_DEBUG_SERIAL
    Serial.printf(F("[PJON Error] Code: %d, Data: %d (Target ID)\n"), code, data);
#endif
    if (code == PJON_CONNECTION_LOST)
    {
        if (_current_role == ROLE_NODE && data == _gatewayPjonId)
        {
#ifdef ROKOR_MESH_DEBUG_SERIAL
            Serial.printf(F("[Node] PJON_CONNECTION_LOST with Gateway ID %d.\n"), _gatewayPjonId);
#endif
            _current_gateway_connected_status = false;
            if (_user_gateway_status_cb)
            {
                _user_gateway_status_cb(false, _user_gateway_status_cb_custom_ptr);
            }
            _fsm_state = DiscoveryFSM::LISTEN_FOR_GATEWAY;
            _fsm_timer_start = millis();
            _gatewayPjonId = PJON_NOT_ASSIGNED;
            memset(_gateway_mac_addr, 0, ESP_NOW_ETH_ALEN);
            _pjon_bus.end();
            initializePjonStack(PJON_NOT_ASSIGNED, _pjon_bus_id, false);
        }
        else if (_current_role == ROLE_GATEWAY)
        {
            int node_idx = findNodeById(data); // data здесь - это ID узла, с которым потеряна связь
            if (node_idx != -1)
            {
#ifdef ROKOR_MESH_DEBUG_SERIAL
                Serial.printf(F("[GW] PJON_CONNECTION_LOST with Node ID %d.\n"), data);
#endif
                updateNodeStatus(data, false, "CONN_LOST");
            }
        }
    }
}

void ROKOR_Mesh::operateAsNode()
{
    uint32_t current_time = millis();
    if (_gatewayPjonId == PJON_NOT_ASSIGNED)
    {
        if (_current_gateway_connected_status)
        {
            _current_gateway_connected_status = false;
            if (_user_gateway_status_cb)
                _user_gateway_status_cb(false, _user_gateway_status_cb_custom_ptr);
#ifdef ROKOR_MESH_DEBUG_SERIAL
            Serial.println(F("[Node] Gateway ID became unassigned. Status set to disconnected."));
#endif
        }
        if (_fsm_state == DiscoveryFSM::OPERATIONAL_NODE)
        {
            _fsm_state = DiscoveryFSM::LISTEN_FOR_GATEWAY;
            _fsm_timer_start = current_time;
#ifdef ROKOR_MESH_DEBUG_SERIAL
            Serial.println(F("[Node Op] No Gateway ID. -> LISTEN_FOR_GATEWAY"));
#endif
            _pjon_bus.end();
            initializePjonStack(PJON_NOT_ASSIGNED, _pjon_bus_id, false);
        }
        return;
    }

    if (current_time >= _next_gateway_ping_time)
    {
        if (_failed_gateway_pings_count >= _node_max_gateway_ping_attempts)
        {
            if (_current_gateway_connected_status)
            {
                _current_gateway_connected_status = false;
#ifdef ROKOR_MESH_DEBUG_SERIAL
                Serial.printf(F("[Node] Gateway ID %d timed out after %d attempts. Disconnected.\n"), _gatewayPjonId, _node_max_gateway_ping_attempts);
#endif
                if (_user_gateway_status_cb)
                {
                    _user_gateway_status_cb(false, _user_gateway_status_cb_custom_ptr);
                }
            }
            _fsm_state = DiscoveryFSM::LISTEN_FOR_GATEWAY;
            _fsm_timer_start = current_time;
            _gatewayPjonId = PJON_NOT_ASSIGNED;
            memset(_gateway_mac_addr, 0, ESP_NOW_ETH_ALEN);
            _pjon_bus.end();
            initializePjonStack(PJON_NOT_ASSIGNED, _pjon_bus_id, false);
#ifdef ROKOR_MESH_DEBUG_SERIAL
            Serial.println(F("[Node Op] Gateway timeout. -> LISTEN_FOR_GATEWAY"));
#endif
            return;
        }

        uint8_t ping_payload[] = {(uint8_t)MeshDiscoveryMessage::NODE_PING_GATEWAY};
#ifdef ROKOR_MESH_DEBUG_SERIAL
        Serial.printf(F("[Node] Sending PING to Gateway ID %d (Attempt %d).\n"), _gatewayPjonId, _failed_gateway_pings_count + 1);
#endif

        _pjon_bus.strategy.set_receiver_mac(_gateway_mac_addr);
        _pjon_bus.set_receiver_id(_gatewayPjonId);
        _pjon_bus.send(ping_payload, sizeof(ping_payload));

        _failed_gateway_pings_count++;
        _next_gateway_ping_time = current_time + _node_ping_gateway_interval_ms;
    }
}

void ROKOR_Mesh::operateAsGateway()
{
    uint32_t current_time = millis();
    if (current_time - _last_gateway_announce_time >= _gateway_announce_interval_ms)
    {
        sendGatewayAnnounce();
        _last_gateway_announce_time = current_time;
    }

    if (current_time - _last_node_cleanup_time >= NODE_CLEANUP_INTERVAL_MS)
    {
        cleanupInactiveNodes();
        _last_node_cleanup_time = current_time;
    }
}

// --- Управление узлами (для шлюза) ---
void ROKOR_Mesh::initNodeManagement()
{
    _known_nodes_count = 0;
    _next_available_node_id_candidate = 2;
    for (int i = 0; i < MAX_NODES_PER_GATEWAY; ++i)
    {
        _known_nodes[i].pjon_id = PJON_NOT_ASSIGNED;
        memset(_known_nodes[i].mac_addr, 0, ESP_NOW_ETH_ALEN);
        _known_nodes[i].last_seen = 0;
        _known_nodes[i].id_assigned_this_session = false;
    }
}

void ROKOR_Mesh::handleNodeIdRequest(const PJON_Packet_Info &request_info, const uint8_t *mac_from_payload)
{
    int existing_node_idx = -1;
    for (int i = 0; i < _known_nodes_count; ++i)
    {
        if (memcmp(_known_nodes[i].mac_addr, mac_from_payload, ESP_NOW_ETH_ALEN) == 0)
        {
            existing_node_idx = i;
            break;
        }
    }

    uint8_t assigned_id_to_send;

    if (existing_node_idx != -1)
    {
        assigned_id_to_send = _known_nodes[existing_node_idx].pjon_id;
        _known_nodes[existing_node_idx].last_seen = millis();
#ifdef ROKOR_MESH_DEBUG_SERIAL
        Serial.printf(F("[GW] Node with MAC %02X:%02X already known. Re-sending ID %d.\n"), mac_from_payload[0], mac_from_payload[1], assigned_id_to_send);
#endif
    }
    else
    {
        if (_known_nodes_count >= MAX_NODES_PER_GATEWAY)
        {
#ifdef ROKOR_MESH_DEBUG_SERIAL
            Serial.println(F("[GW] Max nodes reached. Cannot assign new ID."));
#endif
            return;
        }
        assigned_id_to_send = PJON_NOT_ASSIGNED;
        bool id_found = false;
        for (int attempt = 0; attempt < 254; ++attempt)
        {
            bool candidate_taken = false;
            if (_next_available_node_id_candidate == _myPjonId || _next_available_node_id_candidate == 0 || _next_available_node_id_candidate > 254)
            {
                _next_available_node_id_candidate = 2;
            }
            for (int i = 0; i < _known_nodes_count; ++i)
            {
                if (_known_nodes[i].pjon_id == _next_available_node_id_candidate)
                {
                    candidate_taken = true;
                    break;
                }
            }
            if (!candidate_taken)
            {
                assigned_id_to_send = _next_available_node_id_candidate;
                id_found = true;
                _next_available_node_id_candidate++;
                break;
            }
            _next_available_node_id_candidate++;
        }

        if (!id_found)
        {
#ifdef ROKOR_MESH_DEBUG_SERIAL
            Serial.println(F("[GW] Could not find an available PJON ID for new node."));
#endif
            return;
        }

        _known_nodes[_known_nodes_count].pjon_id = assigned_id_to_send;
        memcpy(_known_nodes[_known_nodes_count].mac_addr, mac_from_payload, ESP_NOW_ETH_ALEN);
        _known_nodes[_known_nodes_count].last_seen = millis();
        _known_nodes[_known_nodes_count].id_assigned_this_session = true;
        _known_nodes_count++;
#ifdef ROKOR_MESH_DEBUG_SERIAL
        Serial.printf(F("[GW] New node. Assigned ID %d to MAC %02X:%02X.\n"), assigned_id_to_send, mac_from_payload[0], mac_from_payload[1]);
#endif
    }

    const uint8_t *mac_to_add_peer = (memcmp(request_info.sender_ethernet_address, _esp_now_null_mac, ESP_NOW_ETH_ALEN) != 0) ? request_info.sender_ethernet_address : mac_from_payload;
    addEspNowPeer(mac_to_add_peer, _espNowChannel, strlen(_esp_now_pmk) > 0);

    sendPjonIdAssignment(assigned_id_to_send, mac_to_add_peer);
    updateNodeStatus(assigned_id_to_send, true, "ID_ASSIGN");
}

void ROKOR_Mesh::sendPjonIdAssignment(uint8_t assigned_id, const uint8_t target_mac[6])
{
    uint8_t payload[1 + 1 + ESP_NOW_ETH_ALEN];
    payload[0] = (uint8_t)MeshDiscoveryMessage::NODE_ID_ASSIGN;
    payload[1] = assigned_id;
    memcpy(&payload[2], target_mac, ESP_NOW_ETH_ALEN);

    _pjon_bus.strategy.set_receiver_mac(target_mac);
    _pjon_bus.set_receiver_id(PJON_BROADCAST_ADDRESS); // Узел должен отфильтровать по MAC в payload
    _pjon_bus.send(payload, sizeof(payload));
#ifdef ROKOR_MESH_DEBUG_SERIAL
    Serial.printf(F("[GW] Sent NODE_ID_ASSIGN (ID: %d) to MAC %02X:%02X\n"), assigned_id, target_mac[0], target_mac[1]);
#endif
}

void ROKOR_Mesh::cleanupInactiveNodes()
{
    uint32_t current_time = millis();
#ifdef ROKOR_MESH_DEBUG_SERIAL
    Serial.println(F("[GW] Running cleanup for inactive nodes..."));
#endif
    for (int i = 0; i < _known_nodes_count; ++i)
    {
        if (current_time - _known_nodes[i].last_seen > NODE_INACTIVITY_THRESHOLD_MS)
        {
#ifdef ROKOR_MESH_DEBUG_SERIAL
            Serial.printf(F("[GW] Node ID %d (MAC %02X:%02X) inactive. Removing.\n"),
                          _known_nodes[i].pjon_id, _known_nodes[i].mac_addr[0], _known_nodes[i].mac_addr[1]);
#endif

            updateNodeStatus(_known_nodes[i].pjon_id, false, "TIMEOUT");
            esp_now_del_peer(_known_nodes[i].mac_addr);

            for (int j = i; j < _known_nodes_count - 1; ++j)
            {
                _known_nodes[j] = _known_nodes[j + 1];
            }
            _known_nodes_count--;
            i--;
        }
    }
}

int ROKOR_Mesh::findNodeByMac(const uint8_t mac[6])
{
    for (int i = 0; i < _known_nodes_count; ++i)
    {
        if (memcmp(_known_nodes[i].mac_addr, mac, ESP_NOW_ETH_ALEN) == 0)
        {
            return i;
        }
    }
    return -1;
}

int ROKOR_Mesh::findNodeById(uint8_t id)
{
    if (id == PJON_NOT_ASSIGNED)
        return -1;
    for (int i = 0; i < _known_nodes_count; ++i)
    {
        if (_known_nodes[i].pjon_id == id)
        {
            return i;
        }
    }
    return -1;
}

void ROKOR_Mesh::updateNodeStatus(uint8_t nodeId, bool isConnected, const char *reason)
{
    if (_user_node_status_cb)
    {
        _user_node_status_cb(nodeId, isConnected, _user_node_status_cb_custom_ptr);
    }
#ifdef ROKOR_MESH_DEBUG_SERIAL
    Serial.printf("[GW Node Status] Node ID %d is now %s. Reason: %s\n", nodeId, isConnected ? "CONNECTED" : "DISCONNECTED", reason);
#endif
}

// --- Служебные сообщения ---
void ROKOR_Mesh::sendGatewayAnnounce()
{
    uint8_t payload[1 + ESP_NOW_ETH_ALEN];
    payload[0] = (uint8_t)MeshDiscoveryMessage::GATEWAY_ANNOUNCE;
    memcpy(&payload[1], _my_mac_addr, ESP_NOW_ETH_ALEN);

    addEspNowPeer(_esp_now_broadcast_mac, _espNowChannel, strlen(_esp_now_pmk) > 0);
    _pjon_bus.strategy.set_receiver_mac(_esp_now_broadcast_mac);
    _pjon_bus.set_receiver_id(PJON_BROADCAST_ADDRESS);
    _pjon_bus.send(payload, sizeof(payload));
#ifdef ROKOR_MESH_DEBUG_SERIAL
    Serial.printf(F("[GW] Sent GATEWAY_ANNOUNCE. My ID: %d\n"), _myPjonId);
#endif
}

void ROKOR_Mesh::sendNodeIdRequest()
{
    if (_gatewayPjonId == PJON_NOT_ASSIGNED || memcmp(_gateway_mac_addr, _esp_now_null_mac, ESP_NOW_ETH_ALEN) == 0)
    {
#ifdef ROKOR_MESH_DEBUG_SERIAL
        Serial.println(F("[Node] Cannot send ID request: Gateway MAC or ID unknown."));
#endif
        return;
    }
    uint8_t payload[1 + ESP_NOW_ETH_ALEN];
    payload[0] = (uint8_t)MeshDiscoveryMessage::NODE_ID_REQUEST;
    memcpy(&payload[1], _my_mac_addr, ESP_NOW_ETH_ALEN);

    _pjon_bus.strategy.set_receiver_mac(_gateway_mac_addr);
    _pjon_bus.set_receiver_id(_gatewayPjonId);
    _pjon_bus.send(payload, sizeof(payload));
#ifdef ROKOR_MESH_DEBUG_SERIAL
    Serial.printf(F("[Node] Sent NODE_ID_REQUEST to Gateway ID %d (MAC %02X:%02X).\n"), _gatewayPjonId, _gateway_mac_addr[0], _gateway_mac_addr[1]);
#endif
}

void ROKOR_Mesh::sendNodeIdAck()
{
    if (_gatewayPjonId == PJON_NOT_ASSIGNED || memcmp(_gateway_mac_addr, _esp_now_null_mac, ESP_NOW_ETH_ALEN) == 0)
    {
#ifdef ROKOR_MESH_DEBUG_SERIAL
        Serial.println(F("[Node] Cannot send ID ACK: Gateway MAC or ID unknown."));
#endif
        return;
    }
    uint8_t payload[] = {(uint8_t)MeshDiscoveryMessage::NODE_ID_ACK};
    _pjon_bus.strategy.set_receiver_mac(_gateway_mac_addr);
    _pjon_bus.set_receiver_id(_gatewayPjonId);
    _pjon_bus.send(payload, sizeof(payload));
#ifdef ROKOR_MESH_DEBUG_SERIAL
    Serial.printf(F("[Node] Sent NODE_ID_ACK to Gateway ID %d for my new ID %d.\n"), _gatewayPjonId, _myPjonId);
#endif
}
