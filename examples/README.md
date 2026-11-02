# Примеры

| Файл | Что это |
|---|---|
| [`soapy_pluto_sdr_timestamp.c`](soapy_pluto_sdr_timestamp.c) | минимальный пример работы с Pluto через SoapySDR: открыть устройство, настроить RX/TX, создать потоки CS16, прочитать/записать буферы, корректно закрыть. С него начиналось занятие 3 |

Пример собирается как C:

```
gcc examples/soapy_pluto_sdr_timestamp.c -o /tmp/example $(pkg-config --cflags --libs SoapySDR)
/tmp/example
```

Источник файла: репозиторий курса `fzybot/sdrLessons`, коммит `6fcf285`
(`tests/soapy_pluto/soapy_pluto_sdr_timestamp.c`), лежит обычной копией без изменений.

Разбор примера по шагам — в [отчёте занятия 3](../reports/lesson-03-soapy-libiio/README.md).
Собственный код проекта — в [`src/`](../src/), обработка записей — в [`scripts/`](../scripts/).
