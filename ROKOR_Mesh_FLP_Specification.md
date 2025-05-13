# Техническая Спецификация Библиотеки ROKOR_Mesh_FLP

**1. Название библиотеки:** `ROKOR_Mesh_FLP`

**2. Версия:** `0.1.0`

**3. Автор:** Roman Korotkykh (ROKOR), romankorotkykh@gmail.com

**4. Краткое описание:** Библиотека для ESP32 (включая XIAO_ESP32C6) для создания самоорганизующейся, отказоустойчивой mesh-сети на базе PJON и ESP-NOW. Реализует автоматическое определение роли узла/шлюза, динамическую адресацию и имеет упрощенный API, ориентированный на использование в среде FLProg.

**5. Целевая платформа:** ESP32 (включая XIAO_ESP32C6 и другие модели, поддерживающие ESP-NOW).

**6. Зависимости:**
    * PJON by Giovanni Blu Mitolo (v13.1 или новее, совместимая с ESP-NOW стратегией для ESP32).
    * ESP-IDF (компоненты для Wi-Fi, ESP-NOW, NVS – обычно интегрированы в Arduino Core для ESP32).
    * Библиотека NVS для ESP32 (обычно часть ESP-IDF/Arduino Core).

**7. Основные возможности/Функционал:**
    * Автоматическое определение роли устройства (Узел или Шлюз) при инициализации на основе имени сети (`networkName`).
    * Автоматическое формирование или подключение к существующей mesh-сети на основе общего `networkName`.
    * Надежный механизм выбора шлюза при одновременном старте нескольких кандидатов (случайная задержка + MAC-арбитраж) с потенциалом для базовой отказоустойчивости.
    * Использование PJON поверх ESP-NOW для беспроводной связи.
    * Динамическое присвоение PJON ID узлам (шлюз автоматически выступает в роли "мастера" адресации).
    * Сохранение конфигурации (роль, PJON ID, `bus_id` сети, ESP-NOW канал, PMK) в энергонезависимой памяти (NVS) для "plug and play" при перезапусках.
    * Асинхронная отправка сообщений (полезной нагрузки MQTT) шлюзу (для узлов) или конкретному узлу (для шлюза).
    * Прием сообщений через механизм callback-функций.
    * Обратная связь о состоянии подключения к шлюзу для узлов (через метод и callback).
    * Обратная связь о статусе подключенных узлов для шлюза (через callback, для мониторинга).
    * Автоматическая генерация ключа шифрования ESP-NOW (PMK) из `networkName` с возможностью ручной установки собственного PMK для повышенной безопасности.
    * Возможность ручной ("для гиков") установки роли и PJON ID через специальные методы API, минуя автоматические механизмы.
    * API, спроектированный для удобной интеграции с пользовательскими блоками FLProg.
    * Механизм "пинга" шлюза узлом для поддержания актуального статуса связи.
    * Автоматические попытки переподключения узла к шлюзу при потере связи.
    * Управление изменением `networkName` или `espNowChannel` "на лету" через методы `end()` и повторный вызов `begin()`.

**8. Структура библиотеки (API)**

* **Класс: `ROKOR_Mesh`**
    * **Описание класса:**
        Класс `ROKOR_Mesh` предоставляет основной интерфейс для работы с самоорганизующейся mesh-сетью на базе PJON и ESP-NOW. Он инкапсулирует логику автоматического определения роли устройства (Узел/Шлюз), подключения к сети, отправки и приема данных, а также управления конфигурацией. Предназначен для использования с микроконтроллерами ESP32 и ориентирован на интеграцию с FLProg.

    * **Публичные методы:**

        * `ROKOR_Mesh();`
            * **Описание:** Конструктор класса. Инициализирует внутренние переменные значениями по умолчанию.
            * **Параметры:** Нет.
            * **Возвращает:** Нет.

        * `bool begin(const char* networkName, uint8_t espNowChannel = 1, uint8_t pjonIdForGatewayRole = ROKOR_MESH_DEFAULT_GATEWAY_ID);`
            * **Описание:** Основной метод инициализации. Если роль не была принудительно установлена через `forceRoleNode`/`forceRoleGateway`, запускает процесс автоматического определения роли устройства в сети (`networkName`), настраивает ESP-NOW на указанном канале (`espNowChannel`) и PJON. Если устройство становится шлюзом, оно использует `pjonIdForGatewayRole` (или ID по умолчанию, если `pjonIdForGatewayRole`=0). Для узлов PJON ID получается динамически от шлюза. Загружает сохраненную конфигурацию из NVS или создает новую. PMK по умолчанию генерируется из `networkName`, если `setEspNowPmk` не был вызван ранее.
            * **Параметры:**
                * `const char* networkName`: Имя сети (макс. `ROKOR_MESH_MAX_NETWORK_NAME_LEN` символов).
                * `uint8_t espNowChannel` (опционально): Wi-Fi канал ESP-NOW (1-13). По умолчанию 1.
                * `uint8_t pjonIdForGatewayRole` (опционально): PJON ID для шлюза. По умолчанию `ROKOR_MESH_DEFAULT_GATEWAY_ID`.
            * **Возвращает:** `true` при успехе, `false` при ошибке.

        * `void end();`
            * **Описание:** Корректно останавливает сетевую активность, очищает состояния. Вызывать перед сменой `networkName` или `espNowChannel` "на лету".
            * **Параметры:** Нет.
            * **Возвращает:** Нет.

        * `void setEspNowPmk(const char* pmk);`
            * **Описание:** Устанавливает пользовательский ESP-NOW PMK (16 ASCII-символов). Вызывать до `begin()`. При несоответствии длины, ключ будет дополнен/обрезан.
            * **Параметры:** `const char* pmk`: Строка PMK.
            * **Возвращает:** Нет.

        * `void forceRoleNode(uint8_t pjonId, uint8_t gatewayToConnectPjonId = 0);`
            * **Описание:** Принудительно устанавливает роль УЗЕЛ. Вызывать до `begin()`.
            * **Параметры:**
                * `uint8_t pjonId`: PJON ID узла (1-254). 0 для динамического получения.
                * `uint8_t gatewayToConnectPjonId` (опционально): PJON ID шлюза. 0 для автообнаружения.
            * **Возвращает:** Нет.

        * `void forceRoleGateway(uint8_t pjonId = ROKOR_MESH_DEFAULT_GATEWAY_ID);`
            * **Описание:** Принудительно устанавливает роль ШЛЮЗ. Вызывать до `begin()`.
            * **Параметры:** `uint8_t pjonId` (опционально): PJON ID шлюза. 0 для ID по умолчанию.
            * **Возвращает:** Нет.

        * `void update();`
            * **Описание:** Главный обработчик. Вызывать регулярно в `loop()`.
            * **Параметры:** Нет.
            * **Возвращает:** Нет.

        * `bool sendMessage(uint8_t destinationId, const uint8_t* payload, uint16_t length);`
            * **Описание:** Асинхронно отправляет сообщение. Рекомендуемый макс. `length` - `ROKOR_MESH_MAX_PAYLOAD_SIZE`.
            * **Параметры:** `uint8_t destinationId`, `const uint8_t* payload`, `uint16_t length`.
            * **Возвращает:** `true` при успешной постановке в очередь, `false` иначе.

        * `bool sendMessage(const uint8_t* payload, uint16_t length);`
            * **Описание:** Перегрузка для Узлов (отправка шлюзу).
            * **Параметры:** `const uint8_t* payload`, `uint16_t length`.
            * **Возвращает:** `true` при успешной постановке в очередь, `false` иначе.

        * `void setReceiveCallback(ROKOR_Mesh_ReceiveCallback callback, void* custom_ptr = nullptr);`
            * **Описание:** Регистрирует callback для входящих сообщений.
            * **Параметры:** `ROKOR_Mesh_ReceiveCallback callback`, `void* custom_ptr` (опционально).
            * **Возвращает:** Нет.

        * `void setGatewayStatusCallback(ROKOR_Mesh_GatewayStatusCallback callback, void* custom_ptr = nullptr);`
            * **Описание:** (Для Узлов) Регистрирует callback для статуса связи со шлюзом.
            * **Параметры:** `ROKOR_Mesh_GatewayStatusCallback callback`, `void* custom_ptr` (опционально).
            * **Возвращает:** Нет.

        * `void setNodeStatusCallback(ROKOR_Mesh_NodeStatusCallback callback, void* custom_ptr = nullptr);`
            * **Описание:** (Для Шлюзов) Регистрирует callback для статуса связи с узлами.
            * **Параметры:** `ROKOR_Mesh_NodeStatusCallback callback`, `void* custom_ptr` (опционально).
            * **Возвращает:** Нет.

        * `ROKOR_Mesh_Role getRole() const;`
            * **Возвращает:** `ROKOR_Mesh_Role`.
        * `uint8_t getPjonId() const;`
            * **Возвращает:** `uint8_t` PJON ID.
        * `const uint8_t* getBusId() const;`
            * **Возвращает:** `const uint8_t*` на PJON `bus_id` (4 байта).
        * `const char* getNetworkName() const;`
            * **Возвращает:** `const char*` на имя сети.
        * `bool isNetworkActive() const;`
            * **Возвращает:** `bool` (активна ли сеть).
        * `bool isGatewayConnected() const;`
            * **Описание:** (Для Узлов) `true`, если связь со шлюзом активна.
            * **Возвращает:** `bool`.

        * **Сеттеры для таймаутов/интервалов (вызываются до `begin()`):**
            * `void setDiscoveryTimeout(uint32_t timeout_ms);`
            * `void setGatewayContentionWindow(uint32_t window_ms);`
            * `void setGatewayAnnounceInterval(uint32_t interval_ms);`
            * `void setNodePingGatewayInterval(uint32_t interval_ms);`
            * `void setNodeMaxGatewayPingAttempts(uint8_t attempts);`

**9. Структуры данных (Публичные)**

* `enum ROKOR_Mesh_Role { ROLE_UNINITIALIZED, ROLE_DISCOVERING, ROLE_NODE, ROLE_GATEWAY, ROLE_ERROR };`
    * **Описание:** Определяет возможные роли устройства в сети.
* `typedef void (*ROKOR_Mesh_ReceiveCallback)(uint8_t senderId, const uint8_t* payload, uint16_t length, void* custom_ptr);`
    * **Описание:** Тип указателя на функцию для обработки входящих сообщений.
* `typedef void (*ROKOR_Mesh_GatewayStatusCallback)(bool connected, void* custom_ptr);`
    * **Описание:** Тип указателя на функцию для уведомления об изменении статуса связи со шлюзом (для узлов).
* `typedef void (*ROKOR_Mesh_NodeStatusCallback)(uint8_t nodeId, bool isConnected, void* custom_ptr);`
    * **Описание:** Тип указателя на функцию для уведомления об изменении статуса узла (для шлюзов).

**10. Константы и определения (Публичные, доступные через `#include`)**

* `#define ROKOR_MESH_DEFAULT_GATEWAY_ID 1` // PJON ID шлюза по умолчанию.
* `#define ROKOR_MESH_MAX_NETWORK_NAME_LEN 32` // Максимальная длина имени сети, включая '\0'.
* `#define ROKOR_MESH_ESPNOW_PMK_LEN 16` // Обязательная длина PMK для ESP-NOW.
* `#define ROKOR_MESH_MAX_PAYLOAD_SIZE 200` // Рекомендуемый максимальный размер полезной нагрузки для `sendMessage`.

*(Внутренние константы для таймаутов и интервалов будут иметь значения по умолчанию, например:*
* `DEFAULT_DISCOVERY_TIMEOUT_MS (3000)`
* `DEFAULT_CONTENTION_WINDOW_MS (1000)`
* `DEFAULT_GATEWAY_ANNOUNCE_INTERVAL_MS (10000)`
* `DEFAULT_NODE_PING_INTERVAL_MS (45000)`
* `DEFAULT_NODE_MAX_PING_ATTEMPTS (3)`
* *Эти значения могут быть изменены через соответствующие публичные сеттеры.)*

**11. Пример(ы) использования**

* **Пример 1: Автоматическая инициализация сети (два устройства)**
    * Код идентичен для обоих устройств. При первом запуске одно устройство становится шлюзом, второе – узлом. Узел периодически отправляет данные шлюзу.
        ```cpp
        #include <ROKOR_Mesh_FLP.h> 
        #include <WiFi.h>          

        ROKOR_Mesh myMesh;
        ROKOR_Mesh* global_ROKOR_Mesh_instance = &myMesh; 

        const char* MY_NETWORK_NAME = "Sklad_A_Linia_1";
        const uint8_t WIFI_CHANNEL = 1; 

        void dataReceiver(uint8_t senderId, const uint8_t* payload, uint16_t length, void* custom_ptr) {
          Serial.printf("Сообщение от ID %d, длина %d: ", senderId, length);
          for (uint16_t i = 0; i < length; i++) {
            Serial.print((char)payload[i]);
          }
          Serial.println();

          if(myMesh.getRole() == ROLE_NODE && senderId == myMesh.getPjonId() /* Неверно, ID шлюза будет другим */ ) { 
            // Корректнее было бы: if(myMesh.getRole() == ROLE_NODE && myMesh.isGatewayConnected()) 
            // или если шлюз отвечает, то senderId будет ID шлюза
            char replyMsg[30];
            sprintf(replyMsg, "Node %d got msg from %d", myMesh.getPjonId(), senderId);
            myMesh.sendMessage((uint8_t*)replyMsg, strlen(replyMsg)); // Отвечаем шлюзу
          }
        }

        void gatewayStatusUpdate(bool connected, void* custom_ptr) {
          if (connected) {
            Serial.println("Связь со шлюзом УСТАНОВЛЕНА.");
          } else {
            Serial.println("Связь со шлюзом ПОТЕРЯНА!");
          }
        }
        
        void nodeStatusUpdate(uint8_t nodeId, bool isConnected, void* custom_ptr) {
            Serial.printf("Шлюз: Узел %d теперь %s\n", nodeId, isConnected ? "ПОДКЛЮЧЕН" : "ОТКЛЮЧЕН");
        }

        void setup() {
          Serial.begin(115200);
          delay(1000);
          Serial.println("\n--- ROKOR_Mesh_FLP Auto Init Demo ---");

          myMesh.setReceiveCallback(dataReceiver);
          myMesh.setGatewayStatusCallback(gatewayStatusUpdate);
          myMesh.setNodeStatusCallback(nodeStatusUpdate); 

          Serial.printf("Инициализация сети '%s' на канале %d...\n", MY_NETWORK_NAME, WIFI_CHANNEL);
          if (myMesh.begin(MY_NETWORK_NAME, WIFI_CHANNEL)) {
            Serial.println("Инициализация успешна!");
            Serial.printf("Моя роль: %s\n", myMesh.getRole() == ROLE_GATEWAY ? "ШЛЮЗ" : (myMesh.getRole() == ROLE_NODE ? "УЗЕЛ" : "ОПРЕДЕЛЕНИЕ"));
            Serial.printf("Мой PJON ID: %d\n", myMesh.getPjonId());
            const uint8_t* busId = myMesh.getBusId();
            Serial.printf("PJON Bus ID: %d.%d.%d.%d\n", busId[0], busId[1], busId[2], busId[3]);
          } else {
            Serial.println("Ошибка инициализации сети!");
          }
        }

        uint32_t lastSendTime = 0;

        void loop() {
          myMesh.update(); 

          if (myMesh.isNetworkActive()) {
            if (myMesh.getRole() == ROLE_NODE) {
              if (millis() - lastSendTime > 10000) { 
                if (myMesh.isGatewayConnected()) {
                  String msg = "Привет от Узла ID " + String(myMesh.getPjonId());
                  Serial.printf("Узел %d: Отправка сообщения шлюзу: %s\n", myMesh.getPjonId(), msg.c_str());
                  if (!myMesh.sendMessage((uint8_t*)msg.c_str(), msg.length())) {
                    Serial.println("Ошибка: сообщение не поставлено в очередь.");
                  }
                } else {
                  Serial.printf("Узел %d: Шлюз не доступен, не могу отправить сообщение.\n", myMesh.getPjonId());
                }
                lastSendTime = millis();
              }
            }
          }
          delay(10); 
        }
        ```

* **Пример 2: Ручная инициализация Узла**
    ```cpp
    #include <ROKOR_Mesh_FLP.h>
    ROKOR_Mesh myMesh;
    ROKOR_Mesh* global_ROKOR_Mesh_instance = &myMesh;

    const char* MY_NETWORK_NAME = "LabNetwork";
    const uint8_t WIFI_CHANNEL = 7;
    const uint8_t MY_FORCED_NODE_ID = 10;
    const uint8_t GATEWAY_TARGET_ID = 1; 

    // ... dataReceiver, gatewayStatusUpdate ...

    void setup() {
      Serial.begin(115200); // ...
      myMesh.setReceiveCallback(dataReceiver);
      myMesh.setGatewayStatusCallback(gatewayStatusUpdate);
      
      myMesh.forceRoleNode(MY_FORCED_NODE_ID, GATEWAY_TARGET_ID);
      
      Serial.printf("Ручная инициализация Узла (ID: %d) для сети '%s', шлюз ID: %d...\n",
                    MY_FORCED_NODE_ID, MY_NETWORK_NAME, GATEWAY_TARGET_ID);

      if (myMesh.begin(MY_NETWORK_NAME, WIFI_CHANNEL)) { 
        Serial.println("Инициализация Узла успешна!");
      } else { /* ... */ }
    }
    void loop() { myMesh.update(); delay(10); }
    ```

* **Пример 3: Ручная инициализация Шлюза**
    ```cpp
    // ... (аналогично Примеру 2, но с вызовом)
    // myMesh.forceRoleGateway(MY_FORCED_GATEWAY_ID); // MY_FORCED_GATEWAY_ID = 1, например
    // ...
    // if (myMesh.begin(MY_NETWORK_NAME, WIFI_CHANNEL, MY_FORCED_GATEWAY_ID)) { ... }
    ```

* **Пример 4: Установка пользовательского PMK и изменение таймаутов**
    ```cpp
    // ...
    void setup() {
      Serial.begin(115200); // ...
      myMesh.setEspNowPmk("MySecurePMK12345"); // 16 символов
      myMesh.setNodePingGatewayInterval(15000); 
      myMesh.setNodeMaxGatewayPingAttempts(5);   
      // ... setReceiveCallback и т.д. ...
      if (myMesh.begin("SecureNet", 6)) { /* ... */ }
    }
    // ...
    ```
* **Пример 5: Смена сети "на лету"**
    ```cpp
    // ... (инициализация с myMesh.begin("Сеть1"); ) ...
    bool condition_to_change_network = false; // Управляется извне
    char new_network_name[ROKOR_MESH_MAX_NETWORK_NAME_LEN + 1] = "Сеть2";
    uint8_t new_channel = 5;

    void loop() {
      myMesh.update();
      if (condition_to_change_network) {
        Serial.println("Меняем сеть...");
        myMesh.end(); 
        if (myMesh.begin(new_network_name, new_channel)) {
          Serial.printf("Успешно переключились на '%s'\n", new_network_name);
        } else {
          Serial.printf("Ошибка переключения на '%s'\n", new_network_name);
        }
        condition_to_change_network = false; // Сбросить флаг
      }
    }
    ```

**12. Инициализация (сводка шагов)**
    1.  **Подключить библиотеку:** `#include <ROKOR_Mesh_FLP.h>`
    2.  **Создать глобальный экземпляр:** `ROKOR_Mesh myMesh;`
    3.  **Инициализировать глобальный указатель:** `ROKOR_Mesh* global_ROKOR_Mesh_instance = &myMesh;`
    4.  **В `setup()`:**
        * (Опционально) Вызвать сеттеры для ручной настройки (`forceRoleNode`, `forceRoleGateway`, `setEspNowPmk`, сеттеры таймаутов).
        * Зарегистрировать callback-функции (`setReceiveCallback`, `setGatewayStatusCallback`, `setNodeStatusCallback`).
        * Вызвать `myMesh.begin("ИмяСети", канал_ESPNOW, id_шлюза_по_умолч);`.
        * Проверить результат `begin()` и `myMesh.isNetworkActive()`.
    5.  **В `loop()`:** Регулярно вызывать `myMesh.update();`.

**13. Обработка ошибок**
    * **Возвращаемые значения методов:** `begin()`, `sendMessage()` возвращают `bool`.
    * **Методы статуса:** `isNetworkActive()`, `isGatewayConnected()`, `getRole()` (может вернуть `ROLE_ERROR`).
    * **Callback-функции статуса:** `setGatewayStatusCallback`, `setNodeStatusCallback`.
    * **Внутренняя обработка ошибок PJON:**
        * `PJON_CONNECTION_LOST`: Для Узла -> статус шлюза `false`, вызов callback, попытка переподключения. Для Шлюза -> вызов callback о статусе узла.
        * `PJON_PACKETS_BUFFER_FULL`: `sendMessage()` вернет `false`.
        * `PJON_CONTENT_TOO_LONG`: Предотвращается проверкой в `sendMessage()` (на `ROKOR_MESH_MAX_PAYLOAD_SIZE`).

**14. Рекомендации по использованию в FLProg**
    * **Глобальный экземпляр и указатель:** Объявлять в секции C++ кода, доступной глобально для пользовательских блоков.
        ```cpp
        // Глобально в проекте FLProg (или в секции C++ основного пользовательского блока)
        #include <ROKOR_Mesh_FLP.h>
        ROKOR_Mesh myMesh;
        ROKOR_Mesh* global_ROKOR_Mesh_instance = &myMesh;
        // Глобальные переменные и флаги для обмена с callback-функциями
        volatile bool flprog_newMessageAvailable = false;
        uint8_t flprog_receivedSenderId;
        uint8_t flprog_receivedDataBuffer[ROKOR_MESH_MAX_PAYLOAD_SIZE];
        uint16_t flprog_receivedDataLength;
        volatile bool flprog_currentGatewayStatus = false;
        volatile uint8_t flprog_nodeStatus_nodeId;
        volatile bool flprog_nodeStatus_isConnected;
        volatile bool flprog_newNodeStatusAvailable = false;

        // Глобальные C++ callback-функции, изменяющие эти переменные
        void myDataReceiver_flp(uint8_t senderId, const uint8_t* payload, uint16_t length, void* custom_ptr) {
          if (length <= ROKOR_MESH_MAX_PAYLOAD_SIZE) { 
            memcpy(flprog_receivedDataBuffer, payload, length);
            flprog_receivedDataLength = length;
            flprog_receivedSenderId = senderId;
            flprog_newMessageAvailable = true;
          }
        }
        void myGatewayStatusUpdater_flp(bool connected, void* custom_ptr) {
          flprog_currentGatewayStatus = connected;
        }
        void myNodeStatusUpdater_flp(uint8_t nodeId, bool isConnected, void* custom_ptr) {
          flprog_nodeStatus_nodeId = nodeId;
          flprog_nodeStatus_isConnected = isConnected;
          flprog_newNodeStatusAvailable = true;
        }
        ```
    * **Блок "Инициализация ROKOR_Mesh":** (в `SetupSection` FLProg)
        * Вызывает `myMesh.setReceiveCallback(myDataReceiver_flp)` и другие сеттеры.
        * Вызывает `myMesh.begin(...)`.
        * Выходы блока FLProg (`InitOK`, `Role`, `PjonID`) получают значения из `myMesh.isNetworkActive()`, `myMesh.getRole()`, `myMesh.getPjonId()`.
    * **Блок "ROKOR_Mesh Update":** (в `LoopSection` FLProg, должен выполняться часто)
        * Содержит вызов `if (global_ROKOR_Mesh_instance) global_ROKOR_Mesh_instance->update();`.
    * **Блок "ROKOR_Mesh Send":** (логика в `LoopSection` или вызываемая функция)
        * Входы FLProg: `Execute` (импульс), `DestID` (число), `PayloadString` (строка).
        * Выход FLProg: `QueuedOK`.
        * Код: `// if (вход_Execute && myMesh.isNetworkActive()) { выход_QueuedOK = myMesh.sendMessage(... (const uint8_t*)PayloadString.c_str(), strlen(PayloadString.c_str())); }` (адаптировать для роли и типа данных).
    * **Блок "ROKOR_Mesh Receive Check":** (в `LoopSection`)
        * Выходы FLProg: `NewMsg` (импульс), `SenderID`, `DataString`, `DataLen`.
        * Логика: Проверяет флаг `flprog_newMessageAvailable`. Если `true`, устанавливает выходы значениями из `flprog_received...` и сбрасывает флаг.
    * **FLProg и Callback:** Взаимодействие C++ callback-ов с FBD-логикой FLProg осуществляется через общие глобальные переменные и флаги.
