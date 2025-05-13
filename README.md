# ROKOR_Mesh_FLP Library

Версия: 0.1.0
Автор: Roman Korotkykh (ROKOR), romankorotkykh@gmail.com

## Описание

`ROKOR_Mesh_FLP` – это библиотека для микроконтроллеров ESP32 (включая XIAO_ESP32C6), предназначенная для создания самоорганизующихся и отказоустойчивых mesh-сетей. В качестве основы используются протокол PJON и технология ESP-NOW для беспроводной связи.

Ключевые особенности:
* **Автоматическое определение роли:** Устройство само определяет, должно ли оно быть Узлом или Шлюзом в сети.
* **Автоматическое формирование сети:** Устройства с одинаковым именем сети (`networkName`) автоматически объединяются.
* **Надежный выбор шлюза:** Реализован механизм выбора шлюза с разрешением конфликтов.
* **Динамическая адресация:** Шлюз автоматически назначает PJON ID новым узлам.
* **Энергонезависимая конфигурация:** Роль, ID и параметры сети сохраняются в NVS.
* **Упрощенный API:** Асинхронные методы для отправки и приема данных, ориентированные на FLProg.
* **ESP-NOW безопасность:** Автоматическая генерация PMK из имени сети или установка пользовательского ключа.
* **Обратная связь:** Callback-функции для отслеживания статуса сети и связи.
* **Гибкость:** Возможность ручной настройки для опытных пользователей.

## Целевая платформа

* ESP32 (включая XIAO_ESP32C6 и другие модели, поддерживающие ESP-NOW).

## Зависимости

* **PJON** by Giovanni Blu Mitolo (v13.1 или новее, с поддержкой ESPNOW для ESP32).
* Стандартные компоненты ESP-IDF (WiFi, ESP-NOW, NVS), обычно включенные в Arduino Core для ESP32.

## Установка

1.  Скопируйте папку `ROKOR_Mesh_FLP` в вашу папку `libraries` для Arduino IDE.
2.  Убедитесь, что библиотека PJON также установлена.

## Основное использование

```cpp
#include <ROKOR_Mesh_FLP.h>

ROKOR_Mesh myMesh;
ROKOR_Mesh* global_ROKOR_Mesh_instance = &myMesh; // Обязательно для callback-ов

const char* MY_NET_NAME = "MySmartWarehouse_Line1";
const uint8_t ESP_CHANNEL = 1;

void data_receiver(uint8_t senderId, const uint8_t* payload, uint16_t length, void* custom_ptr) {
  Serial.printf("Получено от ID %d: ", senderId);
  for (uint16_t i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  myMesh.setReceiveCallback(data_receiver);
  // myMesh.setEspNowPmk("YourSecureKey123"); // Опционально

  if (myMesh.begin(MY_NET_NAME, ESP_CHANNEL)) {
    Serial.printf("Сеть '%s' инициализирована. Роль: %d, PJON ID: %d\n",
                  myMesh.getNetworkName(), (int)myMesh.getRole(), myMesh.getPjonId());
  } else {
    Serial.println("Ошибка инициализации сети!");
  }
}

void loop() {
  myMesh.update(); // Важно вызывать регулярно

  if (myMesh.isNetworkActive() && myMesh.getRole() == ROLE_NODE) {
    static uint32_t last_send = 0;
    if (millis() - last_send > 15000) { // Каждые 15 секунд
      if (myMesh.isGatewayConnected()) {
        String msg = "Привет от Узла " + String(myMesh.getPjonId());
        myMesh.sendMessage((uint8_t*)msg.c_str(), msg.length());
        Serial.println("Узел: Сообщение отправлено шлюзу.");
      }
      last_send = millis();
    }
  }
  delay(10);
}
```

Подробное описание API смотрите в файле `ROKOR_Mesh_FLP.h` и в полной технической спецификации.

## Рекомендации для FLProg
Этот раздел будет дополнен подробными инструкциями и примерами блоков для FLProg в ближайшее время.
*(Здесь будут размещены рекомендации по созданию пользовательских блоков FLProg: инициализация, update, отправка, прием через глобальные переменные/флаги).*

## Лицензия
Данный проект лицензирован под лицензией Apache License 2.0. Подробности смотрите в файле `LICENSE.md`.

## TODO / Планы на будущее

* Более продвинутая логика отказоустойчивости шлюза.
* Оптимизация управления списком узлов на шлюзе.
* Добавление примеров для более сложных сценариев.
