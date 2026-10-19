# DPT.DOOM Launcher — Windows HTML playtest

`dptdoom-launcher.exe` открывает **локальный HTML/CSS/JS-интерфейс внутри WebView2**.
Страница встроена в ресурсы EXE, без CDN, Node.js, Python/.NET runtime или
локального HTTP-сервера. Нативная часть проверяет параметры/ресурсы и вызывает
`CreateProcessW` с явным EXE и аргументами, **не shell**.

## Запуск

Распаковать весь Windows SDL-пакет, открыть `bin/dptdoom-launcher.exe`, выбрать
свой легальный IWAD и нажать «Запустить игру». Рядом нужны `dptdoom.exe`, SDL DLL
и `WebView2Loader.dll`. Также требуется **Microsoft Edge WebView2 Evergreen Runtime**:
он уже есть на машине владельца, но не гарантирован на других Windows.
Если его нет, launcher выдаёт ошибку; ничего не скачивает/не устанавливает сам.
Официальный runtime: <https://developer.microsoft.com/microsoft-edge/webview2/>.
Игру по-прежнему можно запустить напрямую без WebView2.

Три профиля, EXE/IWAD chooser, до 16 plain PWAD с порядком загрузки, preview,
эпизод/карта/сложность/monster flags, single-player и direct COOP host/join на двоих.
Для подключения — numeric IPv4 и TCP-порт; LAN host требует подтверждения.
Оба игрока выбирают одинаковые build, IWAD/PWAD/порядок, map/skill/flags.
Это экспериментальная trusted-LAN сеть, без auth/encryption/NAT traversal.

Оформление адаптировано по визуальному референсу владельца `ANOM_Style`:
синие градиентные шапки, объёмные кнопки, светлые панели и парная тёмная палитра.
Компоновка launcher сохранена. Переключатель темы слева; по умолчанию светлая,
выбор сохраняется в localStorage выбранного WebView-профиля (`webview/`), значит
portable-тема остаётся внутри `user/`. Шрифт — системный Tahoma/Verdana: Snowstorm
из refs не распространяется из-за отсутствующей лицензии.

## Обычный и portable-режимы

По умолчанию данные в `%LOCALAPPDATA%\DPTDOOM`.
Создать **любой файл `portable.txt` рядом с launcher EXE** (содержимое не важно),
закрыть/открыть launcher — данные пойдут в **`user/` рядом с EXE**, независимо от
рабочего каталога вызывающей программы. Каталог с именем portable.txt не считается
маркером. При невозможности записи — ошибка, без тихого fallback в AppData.

```text
dptdoom-launcher.exe
dptdoom.exe
WebView2Loader.dll
SDL2.dll / SDL2_mixer.dll
portable.txt
user/
  launcher.ini             # 3 профиля
  game/profile1/           # настройки движка и .savegame/
  game/profile2/
  game/profile3/
  sessions/<unique>/game.log
  webview/                 # профиль/кэш WebView2
```

При первом portable-запуске без `user/launcher.ini`, если найдены AppData-профили,
предлагается **копирование с подтверждением**: launcher.ini и `.cfg`/`.dsg` из
`game/`. Оригиналы остаются, существующие файлы назначения не перезаписываются.
Логи, кэш браузера, IWAD/PWAD не копируются. При отмене остаётся новый пустой
portable-профиль. Это не импорт произвольных старых конфигов из всех папок.

Пути к ресурсам внутри каталога launcher сохраняются относительно него в
portable-профиле. Ресурсы вне него остаются абсолютными: на другом ПК их нужно
выбрать заново. При удалении маркера launcher снова использует AppData, не
синхронизирует portable-настройки назад автоматически.

Игровой working directory постоянен для каждого слота: конфиги и saves не
теряются при следующем запуске. Логи уникальны для каждого запуска. Один игровой
слот блокируется на время работы дочерней игры, даже если закрыть launcher;
для второго peer на том же ПК использовать другой слот/каталог данных.
`--data-dir directory` — явное переопределение для изолированных тестов/профилей,
имеет приоритет над маркером. Никаких реальных профилей игрока tests не меняют.

## Границы и безопасность

- Нативный мост принимает ограниченный versioned набор полей/действий, не
  произвольные команды/скрипты/пути для чтения и записи. WAD header/directory,
  лимиты, наличие выбранной IWAD-карты и дубликаты проверяются перед запуском.
- HTML отдаётся из EXE на зарезервированном `https://dptdoom.local.invalid/`:
  обработчик перехватывает запрос, внешнего сервиса/DNS для страницы нет.
  Другие переходы, popup/download и permissions запрещены; CSP блокирует внешние
  ресурсы и fetch. Это не обещание отсутствия внутренней фоновой активности
  самого Microsoft Runtime; его обновления управляются Windows/Edge.
- Пользовательские строки выводятся через textContent/JSON, не innerHTML/ExecuteScript.
- Пути движка проверяются на ANSI-представимость и длину: `CreateProcessW`
  не исправляет `main(char**)/stdio` в движке. Unicode-only engine paths пока нет.
- Выбранная карта должна быть в IWAD; расширение списка карт только PWAD пока
  не поддерживается валидатором. Проверка directory не гарантирует безопасность
  любых недоверенных WAD и не заменяет engine-side parsing audit.
- Нет PK3/ZIP/DECORATE, drag/drop/search, nickname/color/voice profiles.
- Раздел «Серверы» работает с native directory: IP/домен:порт master,
  «Обновить», выбор объявления, opt-in публикация waiting host и ASCII session name.
  Default `play.dartpower.xyz:25135`; сохраняется в `[Master]` launcher.ini.
  Запросы native TCP, не fetch из HTML; DNS/query в worker, UI остаётся доступным.
  Никаких auto refresh/download. Все записи UNVERIFIED; выбор только заполняет
  форму, окончательная совместимость — handshake. Подробнее `docs/MASTER.md`.
  Раздел «Игрок» по-прежнему недоступен; ping/favorites/filter пока нет.
- Закрытие launcher не завершает игру. «Лог сессии» открывает фиксированный
  системный Notepad, не файловые ассоциации/shell. Ошибки игры идут в лог (`-nogui`).

## Сборка и проверки

```powershell
powershell -NoProfile -File tools/fetch-sdl.ps1
powershell -NoProfile -File tools/fetch-webview2.ps1
cmake --preset windows-sdl -DDPTDOOM_TEST_IWAD="$PWD/refs/iwads/doom2.wad"
cmake --build --preset windows-sdl
ctest --preset windows-sdl -LE interactive
ctest --preset windows-sdl -R '^launcher-windows$'
& ./build/windows-sdl/Release/dptdoom-launcher.exe
```

SDK pinned `Microsoft.Web.WebView2 1.0.3537.50`, SHA-256 проверяется при fetch,
пакет лежит только в `build/deps`. Можно задать `DPTDOOM_WEBVIEW2_SDK` вручную.
Движок/model остаются C99; только Windows host использует C++17/WRL (MSVC).
Для engine-only/MinGW: `-DDPTDOOM_LAUNCHER=OFF`; Linux/macOS launcher пока нет.
Сборка/packaging включают Loader DLL и SDK LICENSE/NOTICE (BSD-style license).

`launcher-model` без GUI/IWAD проверяет quoting/argument round-trip, режимы,
границы WAD, три профиля, portable detection, copy/no-overwrite и write errors.
`launcher-windows` требует Runtime/локальный IWAD и имеет метку `interactive`:
настоящий WebView2 bridge, portable из другого CWD, профили/escaping/bad input,
SP дважды с постоянным config, host/join через HTML-кнопки, blocked navigation,
light/dark tokens, неизменная компоновка и сохранение темы после перезагрузки.
Также настоящий master EXE, DNS localhost, waiting-host publication/list/select,
HTML-like session name как текст, remove после join и понятная ошибка outage.
Для автоматизации только дочерним тестовым процессам задаётся локальный CDP-port
через environment; обычный launcher не включает remote debugging.
Оконный тест может кратко показать окна, затем скрывает их; звук dummy.
