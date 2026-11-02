# Установка и сборка

Проверено на: Fedora Bazzite (host) + контейнер distrobox `sdr-build`,
2 × ADALM-Pluto по USB. Ниже — весь путь от пустой машины до графика IQ.

> Если повторяете на другой машине: сначала раздел 1, потом 2, потом 3.
> Всё, что ставится через `sudo make install`, я ставлю **внутри контейнера**,
> чтобы не засорять host-систему (Bazzite — immutable, `/usr` read-only).

---

## 0. Содержимое репозитория

```bash
git clone https://github.com/dokbrawn/SDR.git
cd SDR
```

Минимальный пример работы с Pluto через SoapySDR лежит в репозитории обычным файлом:
[`examples/soapy_pluto_sdr_timestamp.c`](examples/soapy_pluto_sdr_timestamp.c)
(источник — репозиторий курса `fzybot/sdrLessons`, коммит `6fcf285`). Мой код его не
собирает: это справочный пример занятия 3, разбор — в
[отчёте](reports/lesson-03-soapy-libiio/README.md).

---

## 1. Системные пакеты

```bash
sudo apt-get install -y cmake g++ gcc pkg-config git \
    python3-pip python3-setuptools python3-dev python3-numpy python3-matplotlib swig \
    libusb-1.0-0-dev libaio-dev libxml2 libxml2-dev bison flex libcdk5-dev \
    libavahi-common-dev libavahi-client-dev
```

Для GNU Radio Companion (занятия 1–2) дополнительно `gnuradio` и `gr-osmosdr`/модуль PlutoSDR;
на Bazzite ставил через пакетный менеджер дистрибутива.

## 2. Библиотеки для Pluto (сборка из исходников)

Порядок важен: SoapySDR → libiio → libad9361-iio → SoapyPlutoSDR.
Ветки — те, на которых работает таймстемпинг (см. `.gitmodules` сабмодуля sdrLessons).

```bash
mkdir -p ~/build && cd ~/build

# --- SoapySDR 0.8.1 ---
git clone --branch soapy-sdr-0.8.1 https://github.com/TelecomDep/SoapySDR.git
cmake -S SoapySDR -B SoapySDR/build && cmake --build SoapySDR/build -j"$(nproc)"
sudo cmake --install SoapySDR/build && sudo ldconfig

# --- libiio v0.24 ---
git clone --branch v0.24 https://github.com/TelecomDep/libiio.git
cmake -S libiio -B libiio/build && cmake --build libiio/build -j"$(nproc)"
sudo cmake --install libiio/build && sudo ldconfig

# --- libad9361-iio v0.3 ---
git clone --branch v0.3 https://github.com/TelecomDep/libad9361-iio.git
cmake -S libad9361-iio -B libad9361-iio/build && cmake --build libad9361-iio/build -j"$(nproc)"
sudo cmake --install libad9361-iio/build && sudo ldconfig

# --- SoapyPlutoSDR (ветка с таймстемпами) ---
git clone --branch sdr_gadget_timestamping https://github.com/TelecomDep/SoapyPlutoSDR.git
cmake -S SoapyPlutoSDR -B SoapyPlutoSDR/build && cmake --build SoapyPlutoSDR/build -j"$(nproc)"
sudo cmake --install SoapyPlutoSDR/build && sudo ldconfig
```

Список библиотек и веток взят из материалов курса (репозиторий `fzybot/sdrLessons`:
файл `thirdparty/README.md` и `ansible_install.yml` с `./install.sh`).

### Проверка, что драйвер встал

```bash
SoapySDRUtil --info          # должен быть модуль plutosdr
SoapySDRUtil --find          # должен перечислить Pluto с uri=usb:...
```

Пример вывода с двумя платами:

```
Found device 0
  device = PlutoSDR
  driver = plutosdr
  label = PlutoSDR #0 usb:3.4.5
  uri = usb:3.4.5
Found device 1
  device = PlutoSDR
  driver = plutosdr
  label = PlutoSDR #1 usb:3.5.5
  uri = usb:3.5.5
```

URI **меняется после переподключения** — перед каждым запуском сверяйтесь с `--find`.

## 3. Сборка проекта

```bash
cd SDR
cmake -S . -B build
cmake --build build -j"$(nproc)"
```

Результат — единственный исполняемый файл `build/pluto_link`.

Каталог `build/` в git **не коммитится** (он в `.gitignore`): это генерируемый мусор,
он не переносим между машинами и раздувает репозиторий. Коммитятся только исходники
и `CMakeLists.txt`.

Альтернатива без CMake (одной командой, так я собирал на занятии 6):

```bash
gcc src/main.c -o build/pluto_link $(pkg-config --cflags --libs SoapySDR) -lm
```

### Требования к версии CMake

`cmake_minimum_required(VERSION 3.16)`, C11. Зависимости: только SoapySDR
(ищется через `find_package(SoapySDR CONFIG)`, fallback — `pkg-config`) и `libm`.

## 4. Запуск

Два терминала, по одному на плату. Плата в режиме `rx` слушает 800 МГц и пишет
`rx_from_800.pcm`; плата в режиме `tx` передаёт на 800 МГц, слушает 840 МГц и пишет
`rx_from_840.pcm`.

```bash
# терминал 1
./build/pluto_link rx usb:3.4.5
# терминал 2
./build/pluto_link tx usb:3.5.5
```

Остановка — `Ctrl+C` (SIGINT): потоки деактивируются, файл закрывается, запись сохраняется.
Каждые 100 буферов в stdout печатается номер буфера и RMS принятого сигнала.

## 5. Обработка записи

```bash
python3 -m pip install --user numpy matplotlib

python3 scripts/plot_iq.py rx_from_800.pcm                  # окно 100 000 отсчётов из середины
python3 scripts/plot_iq.py rx_from_800.pcm --window 20000 --offset 0
python3 scripts/plot_iq.py rx_from_800.pcm --save reports/assets/iq_rx800.png
```

Формат `.pcm`: сырые int16, чередование I,Q (little-endian), Fs = 1 МГц.
Один отсчёт = 4 байта, т.е. 81 264 640 отсчётов ≈ 325 МБ — такие файлы в git не кладём.

## 6. Типовые ошибки

| Симптом | Причина | Что делать |
|---|---|---|
| `Unable to claim interface ... Device or resource busy` | прошлый процесс не отпустил USB | `pkill -9 -f pluto_link`, подождать 2–3 с, снова `--find` |
| `no device context found` | URI устарел после переподключения | взять актуальный URI из `SoapySDRUtil --find` |
| `timestamp_every only currently supported with direct mode` | таймстемпы работают только в direct mode | в аргументы устройства добавить `direct=1` |
| `'sqrt' was not declared` / undefined reference to `sqrt` | не подключена math | `#include <math.h>` и `-lm` при линковке |
| `SoapySDRUtil --find` не видит плату | нет прав на USB / не собран SoapyPlutoSDR | пересобрать шаг 2, проверить `lsusb`, при необходимости udev-правила |
| график на 81 млн отсчётов «висит» | matplotlib рисует всё подряд | `plot_iq.py` читает через `numpy.fromfile` и рисует только окно |
| CMake: `Could not find a package configuration file provided by "SoapySDR"` | SoapySDR не установлен / не в `CMAKE_PREFIX_PATH` | `sudo ldconfig`, при нужде `-DCMAKE_PREFIX_PATH=/usr/local` |
