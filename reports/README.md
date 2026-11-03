# Отчёты по занятиям

Здесь лежат отчеты по заданиям

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


## Скриншоты и графики

Все изображения — в `reports/assets/`:
`rx_gain40.png`, `iq_zoom_between_bursts.png`, `waterfall_2437mhz.png`.
Вставка: `![подпись](../assets/имя.png)` + строка с параметрами съёмки под картинкой.
`.pcm`-записи (сотни МБ) в git не коммитим — они в `.gitignore`.
