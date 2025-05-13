/**
 * ROKOR_Mesh_FLP - Пример Auto_Role_Detection
 *
 * Этот скетч демонстрирует автоматическое определение роли устройства (Узел или Шлюз)
 * в сети ROKOR_Mesh_FLP. Если загрузить этот скетч на два или более устройства ESP32
 * с одинаковым именем сети и каналом, одно из них станет Шлюзом, а остальные - Узлами.
 *
 * Узлы будут периодически отправлять сообщение "Привет!" шлюзу.
 * Все устройства будут выводить свой статус и полученные сообщения в Serial Monitor.
 *
 * Для работы этого примера необходима библиотека ROKOR_Mesh_FLP и ее зависимости (PJON).
 */

#include <ROKOR_Mesh_FLP.h> // Подключаем библиотеку ROKOR_Mesh_FLP
// #include <WiFi.h> // WiFi.h уже включен в ROKOR_Mesh_FLP.h, но может быть полезен для других задач

// --- Глобальные настройки сети ---
const char *MY_NETWORK_NAME = "AutoMeshNet_1"; // Имя вашей сети (должно быть одинаковым на всех устройствах)
const uint8_t WIFI_CHANNEL = 1;                // Канал ESP-NOW (1-13, должен быть одинаковым на всех устройствах)

// --- Экземпляр библиотеки ---
ROKOR_Mesh myMesh;
// Обязательный глобальный указатель для работы внутренних callback-функций PJON/ESP-NOW
ROKOR_Mesh *global_ROKOR_Mesh_instance = &myMesh;

// --- Переменные для отправки сообщений ---
unsigned long lastSendTime = 0;
const unsigned long sendInterval = 15000; // Интервал отправки сообщений для узла (15 секунд)

// --- Callback-функция для приема сообщений ---
void dataReceiver(uint8_t senderId, const uint8_t *payload, uint16_t length, void *custom_ptr)
{
    Serial.printf("[%s] Сообщение от ID %d, длина %d: '", myMesh.getRole() == ROLE_GATEWAY ? "ШЛЮЗ" : "УЗЕЛ", senderId, length);
    for (uint16_t i = 0; i < length; i++)
    {
        Serial.print((char)payload[i]);
    }
    Serial.println("'");

    // Если это шлюз и он получил сообщение, он может ответить
    if (myMesh.getRole() == ROLE_GATEWAY)
    {
        char replyMsg[50];
        sprintf(replyMsg, "Шлюз ID %d получил ваше сообщение!", myMesh.getPjonId());
        // Отправляем ответ обратно отправителю
        if (!myMesh.sendMessage(senderId, (uint8_t *)replyMsg, strlen(replyMsg)))
        {
            Serial.printf("[%s] Ошибка отправки ответа узлу ID %d\n", "ШЛЮЗ", senderId);
        }
        else
        {
            Serial.printf("[%s] Ответ отправлен узлу ID %d\n", "ШЛЮЗ", senderId);
        }
    }
}

// --- Callback-функция для статуса подключения к шлюзу (для Узлов) ---
void gatewayStatusUpdate(bool connected, void *custom_ptr)
{
    if (connected)
    {
        Serial.println("[УЗЕЛ] Связь со шлюзом УСТАНОВЛЕНА.");
    }
    else
    {
        Serial.println("[УЗЕЛ] Связь со шлюзом ПОТЕРЯНА!");
    }
}

// --- Callback-функция для статуса узлов (для Шлюзов) ---
void nodeStatusUpdate(uint8_t nodeId, bool isConnected, void *custom_ptr)
{
    Serial.printf("[ШЛЮЗ] Узел ID %d теперь %s.\n", nodeId, isConnected ? "ПОДКЛЮЧЕН" : "ОТКЛЮЧЕН");
}

void setup()
{
    Serial.begin(115200);
    while (!Serial)
    {
        delay(10); // Ожидание подключения Serial Monitor
    }
    delay(1000); // Небольшая задержка для стабилизации
    Serial.println("\n--- ROKOR_Mesh_FLP: Демонстрация автоматического определения роли ---");

    // 1. Установка callback-функций (до вызова begin)
    myMesh.setReceiveCallback(dataReceiver);
    myMesh.setGatewayStatusCallback(gatewayStatusUpdate); // Для узлов
    myMesh.setNodeStatusCallback(nodeStatusUpdate);       // Для шлюзов

    // Опционально: настройка таймаутов (если значения по умолчанию не подходят)
    // myMesh.setDiscoveryTimeout(7000); // Таймаут обнаружения шлюза (мс)
    // myMesh.setNodePingGatewayInterval(25000); // Интервал пинга шлюза узлом (мс)

    // 2. Инициализация библиотеки
    // Третий параметр (ID шлюза по умолчанию) можно опустить, если используется ROKOR_MESH_DEFAULT_GATEWAY_ID (1)
    Serial.printf("Инициализация сети '%s' на канале %d...\n", MY_NETWORK_NAME, WIFI_CHANNEL);
    if (myMesh.begin(MY_NETWORK_NAME, WIFI_CHANNEL))
    {
        Serial.println("Инициализация ROKOR_Mesh прошла успешно!");
        // Роль может быть еще ROLE_DISCOVERING на этом этапе, окончательно определится в update()
    }
    else
    {
        Serial.println("Ошибка инициализации ROKOR_Mesh!");
        // В случае ошибки можно предпринять действия, например, перезагрузить ESP
        while (true)
        {
            delay(1000);
        }
    }
}

void loop()
{
    // 3. Регулярный вызов update() для обработки сетевых событий и конечного автомата
    myMesh.update();

    // После того как роль определена и сеть активна:
    if (myMesh.isNetworkActive())
    {
        if (myMesh.getRole() == ROLE_NODE)
        {
            // Логика для Узла
            if (millis() - lastSendTime > sendInterval)
            {
                if (myMesh.isGatewayConnected())
                {
                    String msg = "Привет от Узла ID " + String(myMesh.getPjonId()) + "!";
                    Serial.printf("[УЗЕЛ] Отправка сообщения шлюзу: '%s'\n", msg.c_str());
                    if (!myMesh.sendMessage((uint8_t *)msg.c_str(), msg.length()))
                    {
                        Serial.println("[УЗЕЛ] Ошибка: сообщение не было поставлено в очередь отправки.");
                    }
                }
                else
                {
                    Serial.println("[УZЕЛ] Шлюз не доступен, не могу отправить сообщение.");
                }
                lastSendTime = millis();
            }
        }
        else if (myMesh.getRole() == ROLE_GATEWAY)
        {
            // Логика для Шлюза (например, отправка команд узлам или сбор данных)
            // В этом примере шлюз только принимает и отвечает в dataReceiver
        }
    }
    else if (myMesh.getRole() == ROLE_DISCOVERING)
    {
        // Можно выводить сообщение о том, что устройство в процессе определения роли
        // Serial.println("Определение роли в сети...");
        // delay(1000); // Небольшая задержка, чтобы не спамить в лог
    }
    else if (myMesh.getRole() == ROLE_ERROR)
    {
        Serial.println("Ошибка сети! Проверьте настройки или перезагрузите устройство.");
        delay(5000);
    }

    delay(10); // Небольшая задержка в основном цикле
}
