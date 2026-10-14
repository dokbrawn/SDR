# SDR-кружок: отчёты по практике

**Студент:** Анисимов Тимур Юрьевич, группа ИКС-431
**Оборудование:** ADALM-Pluto, GNU Radio, SoapySDR, libiio

## Навигация по занятиям

| № | Дата | Что сделано | Результат | Отчёт |
|---|------|-------------|-----------|-------|
| 1 | 08.09 | FM-приёмник в GNU Radio | звук станции на 101.1 МГц | [Неделя 1-2](Week_1-2/README.md#занятие-1-fm-приёмник-0809) |
| 2 | 15.09 | водопад 2.4 ГГц | пакеты Wi-Fi сети на 2437 МГц | [Неделя 1-2](Week_1-2/README.md#занятие-2-водопад-24-ггц-пакеты-wi-fi-1509) |
| 3 | 22.09 | SoapySDR и libiio, инициализация Pluto | устройство открыто, MTU TX 4096 / RX 32768 | [Неделя 3](Week_3/README.md) |
| 4 | 29.09 | приём IQ-отсчётов | замеры RMS при 0 и 40 дБ, графики IQ | [Недели 4-6](Week_4-5-6/README.md#занятие-4-приём-iq-отсчётов-2909) |
| 5 | 06.10 | среда Bazzite, проект на два Pluto | `pluto_tx`, `pluto_rx`, `loopback` | [Недели 4-6](Week_4-5-6/README.md#занятие-5-среда-и-проект-для-двух-pluto-0610) |
| 6 | 13.10 | передача между двумя Pluto | импульсы на синусоиде | [Недели 4-6](Week_4-5-6/README.md#занятие-6-приём-и-передача-между-двумя-pluto-1310) |
| - | - | персональное задание | трапециевидный сигнал | [Недели 4-6](Week_4-5-6/README.md#персональное-задание-трапеция) |

## Структура репозитория

```
Week_1-2/      отчёт и скриншоты, занятия 1-2
Week_3/        отчёт, занятие 3
Week_4-5-6/    отчёт и скриншоты, занятия 4-6
code/          исходники на C
scripts/       скрипты для графиков IQ
```

| Файл | Назначение |
|------|------------|
| [`code/bazzite_main/main.c`](code/bazzite_main/main.c) | бинарь tx/rx для двух Pluto (занятие 6) |
| [`code/reference_soapy_pluto_sdr_timestamp.c`](code/reference_soapy_pluto_sdr_timestamp.c) | пример преподавателя (sdrLessons) |
| [`scripts/plot_final.py`](scripts/plot_final.py) | график I, Q и огибающей из `.pcm` |
| [`scripts/plot_original.py`](scripts/plot_original.py) | исходный скрипт с yonote |

## Быстрый запуск

```
g++ code/bazzite_main/main.c -o main $(pkg-config --cflags --libs SoapySDR) -lm
./main rx usb:3.x.5        # терминал 1
./main tx usb:3.y.5        # терминал 2
python3 scripts/plot_final.py
```
