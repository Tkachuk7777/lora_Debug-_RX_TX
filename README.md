# Прозрачный радиомост UART / RS485 ↔ LoRa (LC-EM-1307 / STM32F103 + SX127x)

Универсальная двусторонняя прошивка для плат **LC-EM-1307** на базе микроконтроллера **STM32F103** и радиомодуля **Semtech SX1276/SX1278 (433 МГц)**. Объединяет два устройства по интерфейсам UART (TTL) или RS485 в прозрачный беспроводной канал связи без изменения протокола обмена (подходит для Modbus RTU, телеметрии и произвольных бинарных/текстовых пакетов).

> **Примечание:** Обе платы прошиваются одним и тем же файлом прошивки (`.hex` / `.axf`) и автоматически работают в дуплексном режиме (приём и передача в обе стороны).

---

## 📁 Структура и путь проекта

* **Файл проекта Keil μVision:**
  ```text
  C:\Temp\LC_EM_1307\LC-EM-1307\project\lora_Debug _RX_TX\LoRaDriverSDK\project\sx127xDriverSDK.uvprojx
  ```
* **Выходной бинарный файл (после сборки):**
  ```text
  C:\Temp\LC_EM_1307\LC-EM-1307\project\lora_Debug _RX_TX\LoRaDriverSDK\project\Objects\sx127xDriverSDK.hex
  ```

---

## ⚙️ Настройка проекта в Keil MDK

Для корректной компиляции под **Arm Compiler 6 (ARMCLANG V6.19)** в настройках проекта (`Project → Options for Target` или `Alt + F7`) выполнены следующие параметры:

1. **Вкладка `Target`:**
   * **Xtal (MHz):** `8.0`
   * **ARM Compiler:** `Use default compiler version 6`
   * **Code Generation:** установлена галочка **Use MicroLIB** (необходимо для работы без полухостинга `__use_no_semihosting` и корректной линковки).
   * **Read/Only Memory Areas (IROM1):** Start `0x8000000`, Size `0x10000`.
2. **Вкладка `Output`:**
   * Установлена галочка **Create HEX File** (для прошивки через UART / STM32CubeProgrammer / FlyMcu).
3. **Вкладка `Debug` / `Utilities` (при прошивке через ST-Link):**
   * Выбран программатор **ST-Link Debugger**, в `Settings → Flash Download` отмечено **Reset and Run**, чтобы плата запускалась сразу после загрузки прошивки.
4. **Чистка дубликатов прерываний:**
   * Обработчики `USART1_IRQHandler` и `USART2_IRQHandler` перенесены в `main.c` (из `stm32f10x_it.c` дубликаты удалены, при этом используется системный таймер `GetTick()` из `stm32f10x_it.c`).

---

## 🛠 Сборка и прошивка из командной строки Windows (CMD / PowerShell)

### 1. Сборка проекта через Keil UV4 из консоли
```bat
"C:\Keil_v5\UV4\UV4.exe" -b "C:\Temp\LC_EM_1307\LC-EM-1307\project\lora_Debug _RX_TX\LoRaDriverSDK\project\sx127xDriverSDK.uvprojx" -j0 -o build_log.txt
```
*(Команда `-b` собирает проект в фоновом режиме и записывает результат в `build_log.txt`.)*

### 2. Прошивка через ST-Link (через Keil UV4)
```bat
"C:\Keil_v5\UV4\UV4.exe" -f "C:\Temp\LC_EM_1307\LC-EM-1307\project\lora_Debug _RX_TX\LoRaDriverSDK\project\sx127xDriverSDK.uvprojx" -o flash_log.txt
```

### 3. Прошивка по UART (через STM32CubeProgrammer CLI, если нет ST-Link)
Перед запуском переведите перемычку **BOOT0** в `1` и нажмите **RESET** (после прошивки верните **BOOT0** в `0`):
```bat
"C:\Program Files\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe" -c port=COM7 br=115200 -w "C:\Temp\LC_EM_1307\LC-EM-1307\project\lora_Debug _RX_TX\LoRaDriverSDK\project\Objects\sx127xDriverSDK.hex" -v -g
```
*(Замените `COM7` на актуальный COM-порт прошиваемой платы.)*

---

## 💻 Основные изменения в коде (`main.c`)

1. **Прозрачный двусторонний мост (UART/RS485 ↔ LoRa):**
   * Удалены блокирующие функции SDK (`sx127xSend` / `sx127xRx`), зависящие от аппаратного пина `DIO0`.
   * Управление радиочипом SX127x реализовано напрямую по SPI через контроль битов `TxDone` (`0x08`) и `RxDone` (`0x40`) в регистре `RegIrqFlags` (`0x12`).
2. **Раздельные буферы на прерываниях для USART1 и USART2:**
   * Введены независимые буферы (`rx_buf1` для USART1 и `rx_buf2` для USART2) по 128 байт, что исключает перемешивание байтов при одновременной работе двух каналов.
   * В `USART1_IRQHandler` и `USART2_IRQHandler` добавлена принудительная очистка флага переполнения `USART_FLAG_ORE` (защита от зависания приёма при помехах на линии).
3. **Нарезка кадров по таймауту тишины (50 мс):**
   * Определение конца пакета ведётся по аппаратному системному таймеру `GetTick()` (`FRAME_GAP_MS = 50`), что обеспечивает целостную передачу кадров Modbus RTU без дробления на отдельные байты.
4. **Управление направлением RS485 (DE/RE):**
   * Добавлено автоматическое переключение приёмопередатчика MAX485 (`rs485_set_tx`) на выводах `PB13` и `PA8` (`0` — приём по умолчанию, `1` — передача с защитными паузами 5 мс).

---

## 🔌 Распиновка платы (LC-EM-1307 / STM32F103)

| Назначение | Вывод STM32 | Примечание |
| :--- | :--- | :--- |
| **USART1 TX / RX** | `PA9` / `PA10` | Основной канал TTL / прошивки |
| **USART2 TX / RX** | `PA2` / `PA3` | Второй канал данных / выход на RS485 |
| **RS485 DE/RE** | `PB13`, `PA8` | `0` — приём (RX), `1` — передача (TX) |
| **SPI1 (SX127x)** | `PA4` (NSS), `PA5` (SCK), `PA6` (MISO), `PA7` (MOSI) | Аппаратный SPI1 |
| **SX127x RESET** | `PB10` | Аппаратный сброс радиомодуля (активный `0`) |
| **Индикация (LED)** | `PB12` | Статусный светодиод |

---

## 📡 Параметры связи

* **UART / RS485:** `9600` бод, `8N1`, максимальный пакет `128` байт, пауза конца кадра `50` мс.
* **Радиоканал LoRa (SX127x):** частота `433.0` МГц (`0x6C4000`), полоса `125` кГц, `SF7`, кодирование `4/5`, `CRC On`, мощность `+17` дБм (`PA_BOOST`), `SyncWord = 0x12`.

---

## 💡 Индикация светодиода `PB12` и проверка работы

* **2 вспышки при включении** (и отправка `BRIDGE_READY\r\n` в оба UART) — радиочип SX127x найден (`RegVersion == 0x12`), мост готов к работе.
* **Непрерывное частое мигание (100 мс)** — ошибка связи с радиочипом по SPI.
* **Одиночное свечение во время посылки** — отправка пакета в радиоэфир.
* **3 быстрые вспышки** — приём корректного пакета из радиоэфира и выдача его в UART/RS485.

---

## 🐍 Скрипт проверки моста (Python)

```python
import time
import serial


def open_port(port_name):
    s = serial.Serial()
    s.port = port_name
    s.baudrate = 9600
    s.timeout = 0.2
    s.dtr = False  # Отключаем автосброс STM32
    s.rts = False
    s.open()
    time.sleep(0.1)
    s.reset_input_buffer()
    return s


s5 = open_port("COM5")
s7 = open_port("COM7")

s7.write(b"HELLO_LORA_BRIDGE\(\r\\)n")
time.sleep(0.5)
print("COM7 -> COM5:", s5.read(s5.in_waiting or 64))

s5.write(b"BACK_TO_COM7\(\r\\)n")
time.sleep(0.5)
print("COM5 -> COM7:", s7.read(s7.in_waiting or 64))

s5.close()
s7.close()
```
