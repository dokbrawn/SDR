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
├── BUILD.md                   установка зависимостей, сборка, запуск, типовые ошибки
├── CHANGELOG.md               что сделано по неделям: получилось / не получилось / дальше
├── CMakeLists.txt             сборка src/main.c → build/pluto_link
├── LICENSE
├── reports/                   ОТЧЁТЫ по занятиям (одно занятие = один файл)
│   ├── README.md              как устроены отчёты и по какому шаблону пишутся
│   ├── TEMPLATE.md            шаблон отчёта
│   ├── assets/                скриншоты и графики, на которые ссылаются отчёты
│   ├── lesson-01-02-fm-and-waterfall/README.md
│   ├── lesson-03-soapy-libiio/README.md
│   ├── lesson-04-06-iq-and-dual-pluto/README.md
│   └── lesson-07-08-tx-qpsk-shaping/README.md
├── src/
│   ├── README.md              что за программа, аргументы, режимы, параметры
│   └── main.c                 полнодуплексный TX/RX для двух Pluto (SoapySDR, CS16),
│                              tx читает кадр из файла tx_name.pcm
├── scripts/
│   ├── README.md              цепочка обработки rx_offline и проверки
│   ├── sdr_common.py          общий код TX/RX: Gray-QPSK, преамбула, фильтры, CS16
│   ├── gen_tx_name.py         текст -> кадр -> tx_name.pcm
│   ├── rx_offline.py          офлайн-приёмник: sync, созвездие, decode, --selftest
│   ├── plot_iq.py             график I, Q и огибающей из .pcm (окно, savefig)
│   ├── plot_original.py       первая версия скрипта (занятие 4), оставлена как есть
│   └── lesson08/              снапшот кода занятия 8, не редактируется
└── examples/
    ├── README.md              зачем пример и как его собрать
    └── soapy_pluto_sdr_timestamp.c   минимальный пример SoapySDR + Pluto (копия, занятие 3)
```

## Быстрый старт

Полная инструкция — в [BUILD.md](BUILD.md). Коротко:

```bash
# 1. clone
git clone https://github.com/dokbrawn/SDR.git
cd SDR

# 2. зависимости (SoapySDR, libiio, libad9361-iio, SoapyPlutoSDR) — см. BUILD.md
#    проверка, что драйвер Pluto виден:
SoapySDRUtil --find

# 3. сборка (каталог build/ в git не попадает, он в .gitignore)
cmake -S . -B build
cmake --build build -j"$(nproc)"

# 4. запуск: две платы, два терминала, URI берём из вывода --find
./build/pluto_link rx usb:3.4.5      # терминал 1 → пишет rx_from_800.pcm
./build/pluto_link tx usb:3.5.5      # терминал 2 → пишет rx_from_840.pcm
# остановка — Ctrl+C, файл закрывается и сохраняется

# 5. графики и декодирование
python3 scripts/plot_iq.py rx_from_800.pcm --save reports/assets/iq_rx800.png
python3 scripts/gen_tx_name.py --name TIMUR          # подготовить кадр для tx
python3 scripts/rx_offline.py rx_from_800.pcm decode  # офлайн-приёмник
python3 scripts/rx_offline.py --selftest              # самопроверка тракта на синтетике
```

Исполняемый файл после сборки ровно один — `build/pluto_link` (цель CMake `pluto_link`).
Режим работы задаётся первым аргументом (`tx` или `rx`), а не отдельными бинарями.

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
| 8 | 27.10 | передатчик: биты → QPSK → upsampling → свёртка → TX | кадр передан, созвездие НЕ сошлось: полный диагноз | [reports/lesson-07-08…](reports/lesson-07-08-tx-qpsk-shaping/README.md) |
| 9 | 03.11 | приёмник: фильтрация, синхронизация, демодуляция | занятие ещё не было; офлайн-приёмник готов: [scripts/README.md](scripts/README.md) | отчёт будет после занятия |

Прогресс по неделям и «что не получилось» — в [CHANGELOG.md](CHANGELOG.md).
