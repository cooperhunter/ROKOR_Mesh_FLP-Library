/**
 * ROKOR_Mesh_FLP - Пример Forced_Node
 *
 * Этот скетч демонстрирует, как принудительно установить роль устройства "Узел".
 * Узел попытается подключиться к шлюзу в указанной сети.
 *
 * PJON ID узла может быть задан принудительно или получен динамически от шлюза.
 * PJON ID шлюза может быть указан явно или будет искаться любой шлюз в сети.
 *
 * Узел будет периодически отправлять сообщение "Привет от принудительного узла!" шлюзу.
 */

#include <ROKOR_Mesh_FLP.h>

// --- Глобальные настройки сети ---
const char *MY_NETWORK_NAME = "AutoMeshNet_1"; // Имя сети (должно совпадать с именем сети шлюза)
const uint8_t WIFI_CHANNEL = 1;                // Канал ESP-NOW (должен совпадать с каналом шлюза)

// --- Настройки для принудительной роли Узла ---
const uint8_t MY_FORCED_NODE_ID = 10;                            // PJON ID этого узла (0 или PJON_NOT_ASSIGNED для динамического получения)
                                                                 // Если задан ID, убедитесь, что он уникален в сети (кроме ID шлюза).
const uint8_t TARGET_GATEWAY_ID = ROKOR_MESH_DEFAULT_GATEWAY_ID; // PJON ID шлюза, к которому подключаться.
                                                                 // 0 или PJON_NOT_ASSIGNED для подключения к любому найденному шлюзу.
                                                                 // ROKOR_MESH_DEFAULT_GATEWAY_ID обычно равен 1.

// --- Экземпляр библиотеки ---
ROKOR_Mesh myMesh;
ROKOR_Mesh *global_ROKOR_Mesh_instance = &myMesh;

// --- Переменные для отправки сообщений ---
unsigned long lastSendTime = 0;
const unsigned long sendInterval = 12000; // Интервал отправки (12 секунд)

// --- Callback-функция для приема сообщений ---
void dataReceiver(uint8_t senderId, const uint8_t *payload, uint16_t length, void *custom_ptr)
{
    Serial.printf("[УЗЕЛ ID %d] Сообщение от ID %d (Шлюз), длина %d: '", myMesh.getPjonId(), senderId, length);
    for (uint16_t i = 0; i < length; i++)
    {
        Serial.print((char)payload[i]);
    }
    Serial.println("'");
}

// --- Callback-функция для статуса подключения к шлюзу ---
void gatewayStatusUpdate(bool connected, void *custom_ptr)
{
    if (connected)
    {
        Serial.printf("[УЗЕЛ ID %d] Связь со шлюзом (ID %d) УСТАНОВЛЕНА.\n", myMesh.getPjonId(), myMesh.isNetworkActive() ? myMesh.sendMessage(nullptr, 0) /*hack to get gateway id, should be a getter*/ : 0); // TODO: Add a getter for gateway ID in the library
    }
    else
    {
        Serial.printf("[УЗЕЛ ID %d] Связь со шлюзом ПОТЕРЯНА!\n", myMesh.getPjonId());
    }
}

void setup()
{
    Serial.begin(115200);
    while (!Serial)
    {
        delay(10);
    }
    delay(1000);
    Serial.println("\n--- ROKOR_Mesh_FLP: Демонстрация принудительной роли УЗЕЛ ---");

    // 1. Установка callback-функций
    myMesh.setReceiveCallback(dataReceiver);
    myMesh.setGatewayStatusCallback(gatewayStatusUpdate);

    // 2. Принудительная установка роли УЗЕЛ (до вызова begin)
    // Если MY_FORCED_NODE_ID = 0 (или PJON_NOT_ASSIGNED), ID будет запрошен у шлюза.
    // Если TARGET_GATEWAY_ID = 0 (или PJON_NOT_ASSIGNED), узел будет искать любой шлюз.
    myMesh.forceRoleNode(MY_FORCED_NODE_ID, TARGET_GATEWAY_ID);
    Serial.printf("Роль принудительно установлена: УЗЕЛ. Запрашиваемый ID: %d, Целевой Шлюз ID: %d\n",
                  MY_FORCED_NODE_ID == 0 ? "Динамический" : String(MY_FORCED_NODE_ID).c_str(),
                  TARGET_GATEWAY_ID == 0 ? "Любой" : String(TARGET_GATEWAY_ID).c_str());

    // 3. Инициализация библиотеки
    Serial.printf("Инициализация сети '%s' на канале %d...\n", MY_NETWORK_NAME, WIFI_CHANNEL);
    if (myMesh.begin(MY_NETWORK_NAME, WIFI_CHANNEL))
    {
        Serial.println("Инициализация ROKOR_Mesh (Узел) прошла успешно!");
        // Окончательный ID узла будет доступен после подключения к шлюзу (если запрашивался динамически)
    }
    else
    {
        Serial.println("Ошибка инициализации ROKOR_Mesh (Узел)!");
        while (true)
        {
            delay(1000);
        }
    }
}

void loop()
{
    myMesh.update();

    if (myMesh.isNetworkActive() && myMesh.getRole() == ROLE_NODE)
    {
        if (millis() - lastSendTime > sendInterval)
        {
            if (myMesh.isGatewayConnected())
            {
                String msg = "Привет от принудительного Узла ID " + String(myMesh.getPjonId()) + "!";
                Serial.printf("[УЗЕЛ ID %d] Отправка сообщения шлюзу: '%s'\n", myMesh.getPjonId(), msg.c_str());
                if (!myMesh.sendMessage((uint8_t *)msg.c_str(), msg.length()))
                {
                    Serial.printf("[УЗЕЛ ID %d] Ошибка: сообщение не было поставлено в очередь.\n", myMesh.getPjonId());
                }
            }
            else
            {
                Serial.printf("[УЗЕЛ ID %d] Шлюз не доступен, не могу отправить сообщение.\n", myMesh.getPjonId());
            }
            lastSendTime = millis();
        }
    }
    else if (myMesh.getRole() == ROLE_DISCOVERING ||
             (myMesh.getRole() == ROLE_NODE && !myMesh.isGatewayConnected()))
    {
        // Serial.printf("Узел (ID: %d) ищет шлюз или ожидает подключения...\n", myMesh.getPjonId());
        // delay(2000);
    }
    else if (myMesh.getRole() == ROLE_ERROR)
    {
        Serial.println("Ошибка сети у Узла! Проверьте настройки или перезагрузите устройство.");
        delay(5000);
    }
    delay(10);
}
