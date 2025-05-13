/**
 * ROKOR_Mesh_FLP - Пример Custom_PMK_Node
 *
 * Этот скетч демонстрирует, как установить пользовательский Primary Master Key (PMK)
 * для шифрования ESP-NOW канала связи.
 *
 * Устройство будет работать как Узел с принудительно заданной ролью.
 *
 * ВАЖНО: Для успешного шифрованного соединения Шлюз, к которому подключается
 * этот узел, также должен быть настроен с ИДЕНТИЧНЫМ PMK.
 * Если PMK на узле и шлюзе не совпадают, связь не будет установлена (или будет нешифрованной,
 * в зависимости от настроек ESP-NOW по умолчанию при ошибке PMK).
 *
 * PMK должен быть строкой из 16 ASCII-символов.
 */

#include <ROKOR_Mesh_FLP.h>

// --- Глобальные настройки сети ---
const char *MY_NETWORK_NAME = "SecureMeshNet"; // Имя вашей защищенной сети
const uint8_t WIFI_CHANNEL = 3;                // Канал ESP-NOW

// --- Пользовательский PMK ---
// ВАЖНО: Длина PMK должна быть ровно ROKOR_MESH_ESPNOW_PMK_LEN (16 символов)
const char *MY_CUSTOM_PMK = "MySecureKey12345"; // Пример 16-символьного ключа

// --- Настройки для принудительной роли Узла ---
const uint8_t MY_FORCED_NODE_ID = 15;
const uint8_t TARGET_GATEWAY_ID = 1; // Предполагаем, что шлюз имеет ID 1

// --- Экземпляр библиотеки ---
ROKOR_Mesh myMesh;
ROKOR_Mesh *global_ROKOR_Mesh_instance = &myMesh;

// --- Переменные для отправки сообщений ---
unsigned long lastSendTime = 0;
const unsigned long sendInterval = 10000; // Интервал отправки (10 секунд)

// --- Callback-функция для приема сообщений ---
void dataReceiver(uint8_t senderId, const uint8_t *payload, uint16_t length, void *custom_ptr)
{
    Serial.printf("[УЗЕЛ PMK ID %d] Зашифрованное сообщение от ID %d (Шлюз), длина %d: '", myMesh.getPjonId(), senderId, length);
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
        Serial.printf("[УЗЕЛ PMK ID %d] Связь со шлюзом (ID %d) УСТАНОВЛЕНА (ожидается шифрование).\n", myMesh.getPjonId(), TARGET_GATEWAY_ID);
    }
    else
    {
        Serial.printf("[УЗЕЛ PMK ID %d] Связь со шлюзом ПОТЕРЯНА!\n", myMesh.getPjonId());
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
    Serial.println("\n--- ROKOR_Mesh_FLP: Демонстрация Узла с пользовательским PMK ---");

    // 1. Проверка длины PMK (для наглядности, библиотека сама обработает)
    if (strlen(MY_CUSTOM_PMK) != ROKOR_MESH_ESPNOW_PMK_LEN)
    {
        Serial.printf("ВНИМАНИЕ: Длина PMK ('%s') не равна %d символам! Это может вызвать проблемы с шифрованием.\n", MY_CUSTOM_PMK, ROKOR_MESH_ESPNOW_PMK_LEN);
        // Библиотека попытается его использовать как есть, но ESP-NOW может его не принять или обрезать/дополнить.
    }

    // 2. Установка пользовательского PMK (ОБЯЗАТЕЛЬНО до вызова myMesh.begin())
    myMesh.setEspNowPmk(MY_CUSTOM_PMK);
    Serial.printf("Пользовательский PMK '%s' установлен.\n", MY_CUSTOM_PMK);

    // 3. Установка callback-функций
    myMesh.setReceiveCallback(dataReceiver);
    myMesh.setGatewayStatusCallback(gatewayStatusUpdate);

    // 4. Принудительная установка роли УЗЕЛ (до вызова begin)
    myMesh.forceRoleNode(MY_FORCED_NODE_ID, TARGET_GATEWAY_ID);
    Serial.printf("Роль принудительно установлена: УЗЕЛ. ID: %d, Целевой Шлюз ID: %d\n", MY_FORCED_NODE_ID, TARGET_GATEWAY_ID);

    // 5. Инициализация библиотеки
    Serial.printf("Инициализация сети '%s' на канале %d с пользовательским PMK...\n", MY_NETWORK_NAME, WIFI_CHANNEL);
    if (myMesh.begin(MY_NETWORK_NAME, WIFI_CHANNEL))
    {
        Serial.println("Инициализация ROKOR_Mesh (Узел с PMK) прошла успешно!");
    }
    else
    {
        Serial.println("Ошибка инициализации ROKOR_Mesh (Узел с PMK)!");
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
                String msg = "Привет от Узла ID " + String(myMesh.getPjonId()) + " с PMK!";
                Serial.printf("[УЗЕЛ PMK ID %d] Отправка (предположительно шифрованного) сообщения: '%s'\n", myMesh.getPjonId(), msg.c_str());
                if (!myMesh.sendMessage((uint8_t *)msg.c_str(), msg.length()))
                {
                    Serial.printf("[УЗЕЛ PMK ID %d] Ошибка: сообщение не было поставлено в очередь.\n", myMesh.getPjonId());
                }
            }
            else
            {
                Serial.printf("[УЗЕЛ PMK ID %d] Шлюз не доступен.\n", myMesh.getPjonId());
            }
            lastSendTime = millis();
        }
    }
    else if (myMesh.getRole() == ROLE_DISCOVERING ||
             (myMesh.getRole() == ROLE_NODE && !myMesh.isGatewayConnected()))
    {
        // Serial.printf("Узел (ID: %d) с PMK ищет шлюз или ожидает подключения...\n", myMesh.getPjonId());
        // delay(2000);
    }
    else if (myMesh.getRole() == ROLE_ERROR)
    {
        Serial.println("Ошибка сети у Узла с PMK! Проверьте настройки или перезагрузите устройство.");
        delay(5000);
    }
    delay(10);
}
