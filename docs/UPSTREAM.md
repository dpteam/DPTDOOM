# Учёт upstream DoomGeneric

База форка: `ozkl/doomgeneric`, commit
`e257a09527062bfb986b6e5e0780839b6cbd5b71`.
Проверено 2026-10-19: открытые PR №41, №31, №29 и список закрытых PR.
Не применять все PR подряд; сначала читать diff и проверять применимость.

| PR | Решение для DPT.DOOM |
|---|---|
| [31](https://github.com/ozkl/doomgeneric/pull/31), Slavchu, `6d7c337c3821784700133e58f5c5e8093e779b68` | Перенесён вручную с адаптацией: const-путь без malloc и с завершающим разделителем. У оригинала `.` склеивался с `default.cfg` в `.default.cfg`. Использован платформенный `DIR_SEPARATOR_S`; Windows `-cdrom` делает собственную копию пути для mutable `savegamedir`. |
| [29](https://github.com/ozkl/doomgeneric/pull/29), Buggem, `1384fedc0ba7490f55b9e00ea4943ea6caae86b2` | Не применён буквально: добавлен стандартный `string.h`, но оставлен POSIX `strings.h` для `strcasecmp`/`strncasecmp`. Изменение из PR основано на более старой версии файла и удаляет нужные POSIX declarations. |
| [41](https://github.com/ozkl/doomgeneric/pull/41) | Не применён. Это provisional-запрос уточнения лицензий, не подтверждённое право на изменение лицензии. Исходные LICENSE/copyright сохранены. |
| [39](https://github.com/ozkl/doomgeneric/pull/39), [38](https://github.com/ozkl/doomgeneric/pull/38), [30](https://github.com/ozkl/doomgeneric/pull/30) | Уже входят в базовый upstream; повторно не переносить. |
| [11](https://github.com/ozkl/doomgeneric/pull/11) | Закрытый, не merged. Diff рассмотрен; вместо SDL-only схемы написан собственный CMake с Win32/X11/SDL/headless и опциональным звуком. |

Остальные закрытые неслитые PR (порты специализированных устройств, эксперименты
и крупные refactor/release) не переносились. Старый PR №20 по headless не
использован как источник: текущий минимальный backend написан для тестов форка.
