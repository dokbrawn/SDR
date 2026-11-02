# Отчёты по занятиям

Здесь лежат отчёты: **одно занятие — один файл**.

## Как устроено

```
reports/
├── README.md                              этот файл
├── TEMPLATE.md                            шаблон нового отчёта
├── assets/                                все картинки отчётов (скриншоты, графики)
├── lesson-01-02-fm-and-waterfall/         занятия 1–2, 08.09 и 15.09 (GNU Radio)
├── lesson-03-soapy-libiio/                занятие 3, 22.09 (SoapySDR, libiio)
├── lesson-04-06-iq-and-dual-pluto/        занятия 4–6, 29.09 / 06.10 / 13.10
└── lesson-07-08-tx-qpsk-shaping/          занятия 7–8, 20.10 / 27.10 (TX: QPSK, свёртка)
```

Именование каталога: `lesson-NN[-MM]-короткая-тема`. Внутри — `README.md`, чтобы GitHub
рендерил отчёт автоматически при открытии папки.

> Занятия 1–2 и 4–6 пока лежат в общих файлах (так сложилось исторически).
> Правильный вариант — разнести по одному файлу на занятие:
> `lesson-01-fm-receiver/README.md`, `lesson-02-waterfall-24ghz/README.md`,
> `lesson-04-iq-capture/README.md`, `lesson-05-dual-pluto-env/README.md`,
> `lesson-06-tx-rx-between-plutos/README.md`.


## Скриншоты и графики

Все изображения — в `reports/assets/`:
`rx_gain40.png`, `iq_zoom_between_bursts.png`, `waterfall_2437mhz.png`.
