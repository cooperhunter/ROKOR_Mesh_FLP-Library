/**
 * ROKOR_Mesh_FLP - Пример Forced_Gateway
 *
 * Этот скетч демонстрирует, как принудительно установить роль устройства "Шлюз".
 * Шлюз будет анонсировать себя в сети и управлять подключением узлов.
 *
 * PJON ID шлюза может быть задан принудительно.
 */

#include <ROKOR_Mesh_FLP.h>

// --- Глобальные настройки сети ---
const char *MY_NETWORK_NAME = "AutoMeshNet_1"; // Имя сети (должно совпадать с именем сети узлов)
const uint8_t WIFI_CHANNEL = 1;                // Канал ESP-NOW (должен совпадать с каналом узлов)

// --- Настройки для принудительной роли Шлюза ---
const uint8_t MY_FORCED_GATEWAY_ID = ROKOR_MESH_DEFAULT_GATEWAY_ID; // PJON ID этого шлюза.
                                                                    // ROKOR_MESH_DEFAULT_GATEWAY_ID обычно равен 1.
                                                                    // Убедитесь, что ID уникален (если в сети несколько шлюзов, это нештатная ситуация для данной библиотеки).

// --- Экземпляр библиотеки ---
ROKOR_Mesh myMesh;
ROKOR_Mesh *global_ROKOR_Mesh_instance = &myMesh;

// --- Callback-функция для приема сообщений от узлов ---
void dataReceiver(uint8_t senderId, const uint8_t *payload, uint16_t length, void *custom_ptr)
{
    Serial.printf("[ШЛЮЗ ID %d] Сообщение от Узла ID %d, длина %d: '", myMesh.getPjonId(), senderId, length);
    for (uint16_t i = 0; i < length; i++)
    {
        Serial.print((char)payload[i]);
    }
    Serial.println("'");

    // Шлюз отвечает узлу
    char replyMsg[60];
    sprintf(replyMsg, "Шлюз ID %d: Ваше сообщение получено, Узел ID %d!", myMesh.getPjonId(), senderId);
    if (!myMesh.sendMessage(senderId, (uint8_t *)replyMsg, strlen(replyMsg)))
    {
        Serial.printf("[ШЛЮЗ ID %d] Ошибка отправки ответа Узлу ID %d\n", myMesh.getPjonId(), senderId);
    }
    else
    {
        Serial.printf("[ШЛЮЗ ID %d] Ответ отправлен Узлу ID %d\n", myMesh.getPjonId(), senderId);
    }
}

// --- Callback-функция для статуса узлов ---
void nodeStatusUpdate(uint8_t nodeId, bool isConnected, void *custom_ptr)
{
    Serial.printf("[ШЛЮЗ ID %d] Статус Узла: ID %d теперь %s.\n", myMesh.getPjonId(), nodeId, isConnected ? "ПОДКЛЮЧЕН" : "ОТКЛЮЧЕН");
}

void setup()
{
    Serial.begin(115200);
    while (!Serial)
    {
        delay(10);
    }
    delay(1000);
    Serial.println("\n--- ROKOR_Mesh_FLP: Демонстрация принудительной роли ШЛЮЗ ---");

    // 1. Установка callback-функций
    myMesh.setReceiveCallback(dataReceiver);
    myMesh.setNodeStatusCallback(nodeStatusUpdate); // Для отслеживания подключения/отключения узлов

    // 2. Принудительная установка роли ШЛЮЗ (до вызова begin)
    myMesh.forceRoleGateway(MY_FORCED_GATEWAY_ID);
    Serial.printf("Роль принудительно установлена: ШЛЮЗ. PJON ID: %d\n", MY_FORCED_GATEWAY_ID);

    // 3. Инициализация библиотеки
    // Третий параметр в begin() (pjonIdForGatewayRole) будет проигнорирован,
    // так как роль и ID уже установлены через forceRoleGateway().
    // Однако, для консистентности, можно передать тот же ID.
    Serial.printf("Инициализация сети '%s' на канале %d...\n", MY_NETWORK_NAME, WIFI_CHANNEL);
    if (myMesh.begin(MY_NETWORK_NAME, WIFI_CHANNEL, MY_FORCED_GATEWAY_ID))
    {
        Serial.println("Инициализация ROKOR_Mesh (Шлюз) прошла успешно!");
        Serial.printf("Шлюз активен с ID: %d в сети '%s'\n", myMesh.getPjonId(), myMesh.getNetworkName());
        const uint8_t *busId = myMesh.getBusId();
        Serial.printf("PJON Bus ID сети: %d.%d.%d.%d\n", busId[0], busId[1], busId[2], busId[3]);
    }
    else
    {
        Serial.println("Ошибка инициализации ROKOR_Mesh (Шлюз)!");
        while (true)
        {
            delay(1000);
        }
    }
}

void loop()
{
    myMesh.update(); // Регулярный вызов для обработки сетевых событий

    // Шлюз в основном работает через callback-функции (прием сообщений, обновление статуса узлов)
    // и внутреннюю логику (анонсы, управление узлами), которая обрабатывается в myMesh.update().

    // Здесь можно добавить дополнительную логику для шлюза, если требуется,
    // например, периодическую отправку команд всем узлам (широковещательно)
    // или конкретным узлам.

    static unsigned long lastBroadcastTime = 0;
    unsigned long broadcastInterval = 30000; // Каждые 30 секунд

    if (myMesh.isNetworkActive() && myMesh.getRole() == ROLE_GATEWAY)
    {
        if (millis() - lastBroadcastTime > broadcastInterval)
        {
            String broadcastMsg = "Шлюз ID " + String(myMesh.getPjonId()) + ": Общее объявление для всех узлов!";
            Serial.printf("[ШЛЮЗ ID %d] Отправка широковещательного сообщения: '%s'\n", myMesh.getPjonId(), broadcastMsg.c_str());

            // Отправка широковещательного сообщения (PJON_BROADCAST_ADDRESS = 0)
            // Обратите внимание: в текущей реализации sendMessage(payload, length) не поддерживает broadcast для шлюза.
            // Нужно использовать sendMessage(destinationId, payload, length) с PJON_BROADCAST_ADDRESS.
            if (!myMesh.sendMessage(PJON_BROADCAST_ADDRESS, (uint8_t *)broadcastMsg.c_str(), broadcastMsg.length()))
            {
                Serial.printf("[ШЛЮЗ ID %d] Ошибка отправки широковещательного сообщения.\n", myMesh.getPjonId());
            }
            lastBroadcastTime = millis();
        }
    }
    else if (myMesh.getRole() == ROLE_ERROR)
    {
        Serial.println("Ошибка сети у Шлюза! Проверьте настройки или перезагрузите устройство.");
        delay(5000);
    }
    delay(10);
}
