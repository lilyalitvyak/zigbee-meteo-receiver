# zigbee_meteo_receiver — ESP-IDF проект

Собран из присланных файлов. Ваши файлы (`CMakeLists.txt`, `idf_component.yml`, `main.c`,
`main.h`, `zigbee_device.c`, `zigbee_device.h`) — это содержимое компонента `main`,
поэтому они помещены в `main/`. Добавлены недостающие файлы верхнего уровня проекта:

```
zigbee_meteo_receiver/
├── CMakeLists.txt          # корневой файл проекта (новый)
├── sdkconfig.defaults      # настройки Zigbee/PM/partition table (новый)
├── partitions.csv          # таблица разделов с zb_storage/zb_fct (новый)
├── .gitignore               (новый)
└── main/
    ├── CMakeLists.txt       # ваш файл
    ├── idf_component.yml    # ваш файл
    ├── main.c                # ваш файл
    ├── main.h                # ваш файл
    ├── zigbee_device.c       # ваш файл
    └── zigbee_device.h       # ваш файл
```

## Сборка

```bash
. $IDF_PATH/export.sh
cd zigbee_meteo_receiver
idf.py set-target esp32h2
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

Компонент `espressif/esp-zigbee-lib` (>=2.0.0) и ESP-IDF (>=5.5.0) подтянутся
автоматически через IDF Component Manager по `main/idf_component.yml`.

## Правки, внесённые в код

Префикс API `ezb_*` — часть вашего SDK Zigbee 2.0, это подтверждено, менять не нужно.
Остальные найденные проблемы исправлены прямо в файлах:

1. **`#include "driver/uart.h"`** добавлен в `zigbee_device.c` (нужен для `uart_driver_delete()`
   в `zigbee_sleep_timer_cb()`).
2. **Зависимость `driver`** добавлена в `REQUIRES` в `main/CMakeLists.txt`.
3. **`zigbee_analog_set_value()`** реализована в `zigbee_device.c`: находит устройство по
   номеру endpoint, ограничивает значение диапазоном `min_value`/`max_value` (как и в ветке
   `EZB_ZCL_CORE_SET_ATTR_VALUE_CB_ID`) и обновляет `current_value`.
4. **`CONFIG_PM_ENABLE=y`** включён в `sdkconfig.defaults` — требуется для `esp_pm_dump_locks()`.
5. **`zigbee_schedule_sleep()`** добавлена в `zigbee_device.h`, чтобы её можно было вызывать
   из `main.c` или откуда угодно ещё — сейчас вызов в `zigbee_device.c` закомментирован,
   раскомментируйте его в `zigbee_zcl_core_action_handler()`, если хотите, чтобы устройство
   уходило в глубокий сон через 300 мс после получения команды записи атрибута.

Всё должно собираться без ошибок компоновки/линковки, которые были в исходном варианте.
