# SDR-практика: ADALM-Pluto, GNU Radio, SoapySDR

Приём и передача сигналов на ADALM-Pluto: от FM-приёмника в GNU Radio до полнодуплексного
обмена IQ-отсчётами между двумя платами из кода на C.

**Студент:** Анисимов Тимур Юрьевич, группа ИКС-431
**Оборудование:** 2 × ADALM-Pluto, антенны, USB-хаб
**Софт:** GNU Radio Companion, SoapySDR 0.8.1, libiio 0.24, libad9361-iio 0.3,
SoapyPlutoSDR (ветка `sdr_gadget_timestamping`), CMake, Python 3 (numpy, matplotlib)
**Стенд:** Fedora Bazzite, сборка в контейнере distrobox `sdr-build`

---

## С чего начать

| Хочу… | Идти сюда |
|---|---|
| понять, что вообще сделано и в каком порядке | [CHANGELOG.md](CHANGELOG.md) |
| прочитать отчёты по занятиям | [reports/](reports/) |
| поставить зависимости и собрать проект | [BUILD.md](BUILD.md) |
| посмотреть/поправить код | [src/](src/), [scripts/](scripts/) |
| написать отчёт по следующему занятию | [reports/TEMPLATE.md](reports/TEMPLATE.md) |
| посмотреть минимальный пример работы с SoapySDR | [examples/README.md](examples/README.md) |

## Структура репозитория

```
.
├── README.md                  этот файл
├── BUILD.md                   зависимости, сборка, запуск, типовые ошибки
├── CHANGELOG.md               что сделано по неделям: получилось / не получилось / дальше
├── CMakeLists.txt             цели: pluto_link, pluto_gui, dsp_tests
├── .gitignore                 build/, *.pcm и прочий мусор не коммитим
├── reports/                   ОТЧЁТЫ по занятиям (одно занятие или группа = одна папка)
│   ├── README.md              как устроены отчёты
│   ├── TEMPLATE.md            шаблон отчёта
│   ├── assets/                скриншоты и графики
│   ├── lesson-01-02-fm-and-waterfall/
│   ├── lesson-03-soapy-libiio/
│   ├── lesson-04-06-iq-and-dual-pluto/
│   ├── lesson-07-08-tx-qpsk-shaping/
│   └── lesson-09-12-receiver-gui/
├── src/
│   ├── README.md              устройство программ и приёмника
│   ├── main.c                 TX/RX для двух Pluto (SoapySDR, CS16), цель pluto_link
│   ├── backend.h, backend.cpp приёмник: DC, поиск пакета, частота, тактовая синхронизация, маркер, декодер
│   ├── timing.h               петля тактовой синхронизации (ZC, Гарднер, Мюллер и Мюллер)
│   └── gui.cpp                окно приёмника на ImGui + ImPlot + SDL2, цель pluto_gui
├── tests/
│   └── test_dsp.cpp           тесты приёмника на синтетическом сигнале, цель dsp_tests
├── data/
│   └── clean_real_burst.h     запись одного реального всплеска (2040 отсчётов)
├── scripts/                   Python: офлайн-приёмник и графики занятий 4–8
├── examples/                  минимальный пример SoapySDR (занятие 3)
└── thirdparty/                сабмодули: SoapyPlutoSDR, SoapySDR, libiio, libad9361-iio, imgui, implot
```

## Быстрый старт

Полная инструкция — в [BUILD.md](BUILD.md). Коротко:

```bash
git clone --recurse-submodules https://github.com/dokbrawn/SDR.git
cd SDR

cmake -S . -B build
cmake --build build -j"$(nproc)"
ctest --test-dir build

./build/pluto_gui --sim
./build/pluto_gui
./build/pluto_gui --rx usb:3.4.5 --tx usb:3.5.5
```

- `pluto_gui --sim` — приёмник на встроенной симуляции, железо не нужно.
- `pluto_gui` без аргументов сам находит платы, запускает `main` на приём и на передачу и рисует результат.
- `pluto_link rx|tx <uri>` — отдельная программа записи IQ, как на занятиях 6–8.
- `ctest` прогоняет 11 тестов приёмника.

Каталог `build/` в git не попадает.

## Занятия

| № | Дата | Тема | Итог | Отчёт |
|---|------|------|------|-------|
| 1 | 08.09 | FM-приёмник в GNU Radio | звук станции на 101.1 МГц | [reports/lesson-01-02…](reports/lesson-01-02-fm-and-waterfall/README.md#занятие-1-fm-приёмник-0809) |
| 2 | 15.09 | водопад 2.4 ГГц | пакеты своей Wi-Fi сети на 2437 МГц | [reports/lesson-01-02…](reports/lesson-01-02-fm-and-waterfall/README.md#занятие-2-водопад-24-ггц-пакеты-wi-fi-1509) |
| 3 | 22.09 | SoapySDR и libiio, инициализация Pluto | устройство открыто, MTU TX 4096 / RX 32768 | [reports/lesson-03…](reports/lesson-03-soapy-libiio/README.md) |
| 4 | 29.09 | приём IQ-отсчётов | замеры RMS при 0 и 40 дБ, графики IQ | [reports/lesson-04-06…](reports/lesson-04-06-iq-and-dual-pluto/README.md#занятие-4-приём-iq-отсчётов-2909) |
| 5 | 06.10 | проект на два Pluto | `pluto_tx`, `pluto_rx`, `loopback` | [reports/lesson-04-06…](reports/lesson-04-06-iq-and-dual-pluto/README.md#занятие-5-среда-и-проект-для-двух-pluto-0610) |
| 6 | 13.10 | передача между двумя Pluto | IQ-запись 81 млн отсчётов, разбор биений | [reports/lesson-04-06…](reports/lesson-04-06-iq-and-dual-pluto/README.md#занятие-6-приём-и-передача-между-двумя-pluto-1310) |
| 7 | 20.10 | приём-передача на 2 SDR, подготовка к цифре | тот же тракт, переход к файловому TX | [reports/lesson-07-08…](reports/lesson-07-08-tx-qpsk-shaping/README.md) |
| 8 | 27.10 | передатчик: биты → QPSK → upsampling → свёртка → TX | кадр передан и принят; созвездие | [reports/lesson-07-08…](reports/lesson-07-08-tx-qpsk-shaping/README.md) |
| 9 | 03.11 | приёмник: фильтрация, синхронизация, демодуляция | офлайн-приёмник на Python: [scripts/README.md](scripts/README.md) | [reports/lesson-09-12…](reports/lesson-09-12-receiver-gui/README.md) |
| 10 | 10.11 | выбор символьных отсчётов, интерфейс ImGui | приёмник на C++ и окно с графиками | [reports/lesson-09-12…](reports/lesson-09-12-receiver-gui/README.md) |
| 11 | 17.11 | символьная синхронизация, интерфейс | детекторы ZC и Гарднера, вкладка синхронизации | [reports/lesson-09-12…](reports/lesson-09-12-receiver-gui/README.md) |
| 12 | 24.11 | схема Мюллера и Мюллера, синхропоследовательность | M&M, маркер Баркера-13, корреляционный поиск пакета | [reports/lesson-09-12…](reports/lesson-09-12-receiver-gui/README.md) |

Прогресс по неделям и «что не получилось» — в [CHANGELOG.md](CHANGELOG.md).
