# Прозрачный радиомост RS485 ↔ LoRa (LC-EM-1307 / STM32F103 + SX127x)

Универсальная двусторонняя прошивка для плат **LC-EM-1307** на базе микроконтроллера **STM32F103** и радиомодуля **Semtech SX1276/SX1278 (434 МГц)**. Объединяет два устройства по интерфейсу **RS485** (через `USART2`) в прозрачный беспроводной канал связи без изменения протокола обмена (подходит для Modbus RTU, телеметрии и произвольных бинарных/текстовых пакетов).

> **Примечание:** Обе платы прошиваются одним и тем же файлом прошивки (`.hex`) и автоматически работают в двустороннем режиме (приём и передача в обе стороны).

---

## 📁 Структура и пути проекта

* **Файл проекта Keil μVision:**
  ```text
  C:\Temp\LC_EM_1307\LC-EM-1307\project\lora_Debug _RX_TX\LoRaDriverSDK\project\sx127xDriverSDK.uvprojx
  ```
* **Выходной бинарный файл (после сборки):**
  ```text
  C:\Temp\LC_EM_1307\LC-EM-1307\project\lora_Debug _RX_TX\LoRaDriverSDK\project\Objects\sx127xDriverSDK.hex
  ```
* **Утилита прошивки по UART (`stm32flash`):**
  ```text
  C:\tem\stm32flash-0.7-binaries\stm32flash.exe
  ```

---

## ⚙️ Настройка проекта в Keil MDK

Для корректной компиляции под **Arm Compiler 6 (ARMCLANG V6.19)** в настройках проекта (`Project → Options for Target` или `Alt + F7`) установлены следующие параметры:

1. **Вкладка `Target`:**
   * **Xtal (MHz):** `8.0`
   * **ARM Compiler:** `Use default compiler version 6`
   * **Code Generation:** установлена галочка **Use MicroLIB** (необходимо для работы без полухостинга `__use_no_semihosting` и корректной линковки).
   * **Read/Only Memory Areas (IROM1):** Start `0x8000000`, Size `0x10000`.
2. **Вкладка `Output`:**
   * Установлена галочка **Create HEX File** (генерация `.hex` для загрузки через `stm32flash`).
3. **Периферия и прерывания:**
   * В проект подключен модуль драйвера сторожевого таймера `stm32f10x_iwdg.c` (и заголовок `stm32f10x_iwdg.h`).
   * Обработчик `USART2_IRQHandler` реализован в `main.c`; системный таймер `GetTick()` (шаг 1 мс) используется из `stm32f10x_it.c`.

---

## 🛠 Сборка и прошивка из командной строки Windows (CMD / PowerShell)

### 1. Сборка проекта через Keil UV4 из консоли
```bat
"C:\Keil_v5\UV4\UV4.exe" -b "C:\Temp\LC_EM_1307\LC-EM-1307\project\lora_Debug _RX_TX\LoRaDriverSDK\project\sx127xDriverSDK.uvprojx" -j0 -o build_log.txt
```
*(Ключ `-b` запускает фоновую сборку и сохраняет отчёт компилятора в `build_log.txt`.)*

### 2. Прошивка через UART с помощью `stm32flash.exe`
Перед запуском команды переведите плату в режим встроенного загрузчика (Bootloader):
1. Установите перемычку **BOOT0** в положение **`1`** (высокий уровень).
2. Нажмите кнопку **RESET** на плате (или переподключите питание).
3. Выполните команду прошивки в консоли (указав актуальный COM-порт платы, например `COM5` или `COM7`):

```bat
C:\tem\stm32flash-0.7-binaries\stm32flash.exe -w "C:\Temp\LC_EM_1307\LC-EM-1307\project\lora_Debug _RX_TX\LoRaDriverSDK\project\Objects\sx127xDriverSDK.hex" -v -g 0x0 COM5
```

**Расшифровка ключей `stm32flash`:**
* `-w "<путь>"` — запись указанного Intel HEX файла во Flash-память микроконтроллера.
* `-v` — верификация (проверка записанных данных после прошивки, до `100.00% Done`).
* `-g 0x0` — автоматический запуск основной программы с начального адреса (`0x08000000`) сразу после завершения прошивки.
* `COM5` — порт USB-UART переходника, подключённого к `USART1` (`PA9`/`PA10`).

4. После появления сообщения `Starting execution at address 0x08000000... done.` верните перемычку **BOOT0** в положение **`0`**, чтобы при последующих включениях питания плата сразу запускала прошивку.

---

## 💻 Основные изменения в текущей версии прошивки (`main.c`)

1. **Прямое управление трансивером SX127x по SPI:**
   * Удалены блокирующие функции SDK, привязанные к выводу `DIO0`.
   * Передача и приём контролируются напрямую через регистр прерываний `RegIrqFlags` (`0x12`): ожидание `TxDone` (`0x08`) с защитным таймаутом `1000 мс` и чтение пакета по флагу `RxDone` (`0x40`) с проверкой целостности `PayloadCrcError` (`0x20`).
2. **Атомарный приём данных по `USART2` (RS485) с защитой от гонки данных:**
   * Приём байтов ведётся по прерыванию `USART2_IRQHandler` в кольцевой буфер `rx_buf2[128]` с подтяжкой линии RX к питанию (`GPIO_Mode_IPU`) для защиты от наводок при переключении шины RS485.
   * Реализована корректная очистка флага переполнения `USART_FLAG_ORE` без потери принятого байта.
   * Вычитка накопленного кадра в основном цикле выполняется в единой критической секции (`__disable_irq()` / `__enable_irq()`) по таймауту тишины `FRAME_GAP_MS = 50 мс` или при заполнении буфера (128 байт).
3. **Точное управление направлением RS485 (DE/RE) через счётчик тактов DWT:**
   * Для формирования точных микросекундных задержек (`delay_us(10)`) задействован аппаратный счётчик циклов ядра Cortex-M3 (`DWT_CYCCNT` по прямым адресам регистров `0xE0001000`–`0xE000EDFC`).
   * Переключение драйвера RS485 (`PB13`, `PA8`) выполняется с гарантированной выдержкой 10 мкс до начала передачи и 10 мкс после установки флага полного завершения сдвига `USART_FLAG_TC`.
4. **Неблокирующая светодиодная индикация и сторожевой таймер (IWDG):**
   * Индикация приёма пакета по LoRa (`PB12`) выполнена без блокирующих задержек (`Soft_delay_ms`) с использованием беззнаковой арифметики таймера `GetTick()`, устойчивой к переполнению 32-битного счётчика (через 49.7 суток).
   * Подключён независимый сторожевой таймер **IWDG** (период сброса ~2 секунды), который активируется строго после успешной инициализации радиомодуля и автоматически перезапускает МК при любых аппаратных сбоях.

---

## 🔌 Распиновка платы (LC-EM-1307 / STM32F103)

| Назначение | Вывод STM32 | Примечание |
| :--- | :--- | :--- |
| **USART1 TX / RX** | `PA9` / `PA10` | Системный UART для загрузки прошивки (`stm32flash`) |
| **USART2 TX / RX** | `PA2` / `PA3` | Рабочий канал данных, подключённый к трансиверу RS485 (`RX` с `IPU`) |
| **RS485 DE / RE** | `PB13`, `PA8` | `0` — приём (RX по умолчанию), `1` — передача (TX) |
| **SPI1 (SX127x)** | `PA4` (NSS), `PA5` (SCK), `PA6` (MISO), `PA7` (MOSI) | Аппаратный интерфейс SPI1 |
| **SX127x RESET** | `PB10` | Аппаратный сброс радиочипа (активный `0`) |
| **Индикация (LED)** | `PB12` | Статусный светодиод |

---

## 📡 Параметры связи

* **Интерфейс RS485 (`USART2`):** `9600` бод, `8N1`, максимальный размер пакета `128` байт, таймаут конца кадра `50` мс.
* **Радиоканал LoRa (`SX127x`):**
  * Несущая частота: **`434.0` МГц** (`0x6C8000`)
  * Полоса пропускания (BW): `125` кГц (`RegModemConfig1 = 0x72`)
  * Коэффициент расширения спектра: `SF7`, кодирование `4/5`, `CRC On` (`RegModemConfig2 = 0x74`)
  * Автоматическая регулировка усиления: `AGC Auto On` (`RegModemConfig3 = 0x04`)
  * Выходная мощность: `+17` дБм через `PA_BOOST` (`RegPaConfig = 0x8F`)
  * Слово синхронизации (`SyncWord`): `0x12`

---

## 💡 Индикация светодиода `PB12`

* **Частое мигание (интервал 100 мс)** — ошибка связи с чипом SX127x по SPI (не пройден опрос `RegVersion == 0x12`).
* **2 вспышки при старте** (и выдача в RS485 строки `BRIDGE_READY\r\n`) — радиомодуль успешно инициализирован, мост перешёл в рабочий режим.
* **Свечение во время передачи** — отправка сформированного кадра в радиоэфир LoRa.
* **Вспышка длительностью 100 мс (неблокирующая)** — успешный приём пакета из эфира LoRa и выдача данных в линию RS485.

---

## 🐍 Скрипт двусторонней проверки моста (Python)

Для проверки связи подключите оба преобразователя **USB-RS485** к ПК (например, `COM7` и `COM5`), установите библиотеку `pyserial` (`pip install pyserial`) и запустите тестовый скрипт:

import serial
import time
import sys

PORT_A = "COM7"  # Плата 1 (USB-RS485)
PORT_B = "COM5"  # Плата 2 (USB-RS485)
BAUD = 9600
LORA_WAIT = 1.5  # Увеличено для LoRa (было 0.5)

def open_port(port):
    try:
        s = serial.Serial(
            port=port, baudrate=BAUD, timeout=0.5,
            bytesize=serial.EIGHTBITS, parity=serial.PARITY_NONE, stopbits=serial.STOPBITS_ONE
        )
        s.dtr = False
        s.rts = False
        s.reset_input_buffer()
        s.reset_output_buffer()
        print(f"✅ {port} открыт | Настройки: {s.baudrate} 8N1 | CTS:{s.cts} DSR:{s.dsr}")
        return s
    except Exception as e:
        print(f"❌ Ошибка открытия {port}: {e}")
        return None

def diagnose_silence(sender, receiver, wait_time):
    print(f"    🔍 ДИАГНОСТИКА тишины на {receiver.port}:")
    print(f"       1. Статус портов: {sender.port}={'OPEN' if sender.is_open else 'CLOSED'}, {receiver.port}={'OPEN' if receiver.is_open else 'CLOSED'}")
    print(f"       2. Очередь на отправку в {sender.port}: {sender.out_waiting} байт (если >0, драйвер/RS485 завис)")
    print(f"       3. Линии {receiver.port}: CTS={receiver.cts}, DSR={receiver.dsr}, CD={receiver.cd}")
    
    # Проверка: не прилетел ли пакет с опозданием (характерно для LoRa)
    print(f"       4. Ждем еще 1.5 сек (проверка задержки эфира LoRa)...")
    time.sleep(1.5)
    if receiver.in_waiting > 0:
        late_data = receiver.read(receiver.in_waiting)
        print(f"       ⚠️ НАЙДЕНО! Пакет пришел с опозданием (> {wait_time}с): {late_data}")
        print(f"       💡 Решение: увеличьте LORA_WAIT в начале скрипта!")
    else:
        print(f"       ❌ Пусто даже спустя {wait_time + 1.5} сек.")
        print(f"       💡 Проверьте: линии A/B (не перепутаны ли?), питание LoRa-модулей, одинаковые ли частота/канал/адрес в настройках LoRa.")

s_a = open_port(PORT_A)
s_b = open_port(PORT_B)

if not s_a or not s_b:
    print("Нужны оба порта. Проверь подключение.")
    sys.exit(1)

print("\n🚀 Тест моста: COM7 <-> COM5 (через LoRa)")
print(f"Таймаут ожидания радиоканала: {LORA_WAIT} сек.\n")

stats = {"A_to_B": 0, "B_to_A": 0, "total": 10}

try:
    for i in range(1, stats["total"] + 1):
        # 1. COM7 -> COM5
        s_b.reset_input_buffer()  # Очищаем мусор перед отправкой
        msg = f"FROM_COM7_{i}\n"
        print(f"[{i}] 📤 Отправка в {PORT_A}: {msg.strip()}")
        written = s_a.write(msg.encode())
        s_a.flush()
        print(f"    (В буфер {PORT_A} записано {written} байт)")
        time.sleep(LORA_WAIT)
        
        if s_b.in_waiting > 0:
            data = s_b.read(s_b.in_waiting)
            print(f"    📥 Принято в {PORT_B} ({len(data)} байт): {data}")
            stats["A_to_B"] += 1
        else:
            print(f"    ❌ Тишина в {PORT_B}")
            diagnose_silence(s_a, s_b, LORA_WAIT)

        # 2. COM5 -> COM7
        s_a.reset_input_buffer()
        msg2 = f"FROM_COM5_{i}\n"
        print(f"[{i}] 📤 Отправка в {PORT_B}: {msg2.strip()}")
        written2 = s_b.write(msg2.encode())
        s_b.flush()
        print(f"    (В буфер {PORT_B} записано {written2} байт)")
        time.sleep(LORA_WAIT)
        
        if s_a.in_waiting > 0:
            data = s_a.read(s_a.in_waiting)
            print(f"    📥 Принято в {PORT_A} ({len(data)} байт): {data}")
            stats["B_to_A"] += 1
        else:
            print(f"    ❌ Тишина в {PORT_A}")
            diagnose_silence(s_b, s_a, LORA_WAIT)
        
        print("-" * 50)
        time.sleep(0.5)

except KeyboardInterrupt:
    print("\n👋 Остановлено пользователем.")
finally:
    print("\n📊 ИТОГОВАЯ ДИАГНОСТИКА:")
    print(f"   Успешно {PORT_A} -> {PORT_B}: {stats['A_to_B']} из {stats['total']}")
    print(f"   Успешно {PORT_B} -> {PORT_A}: {stats['B_to_A']} из {stats['total']}")
    s_a.close()
    s_b.close()
