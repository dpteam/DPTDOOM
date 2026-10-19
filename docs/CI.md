# Непрерывная интеграция

Workflow: `.github/workflows/ci.yml`. Запускается на push, pull request и вручную
через `workflow_dispatch`. **В этой сессии workflow не запускался на GitHub**:
без команды владельца push не выполняется. Наличие YAML не означает зелёный CI.

## Матрица

| Job | Runner | Проверки без игровых данных |
|---|---|---|
| headless | Windows 2022 / Ubuntu 24.04 / macOS 14 | Сборка, версия, запуск без IWAD, timer и zone allocator |
| windows-sdl | Windows 2022 | Официальные SDL ZIP с SHA-256, сборка, dummy input/audio |
| unix-sdl | Ubuntu 24.04 / macOS 14 | SDL development packages, сборка, dummy input/audio |
| unix-sdl/X11 | Ubuntu 24.04 | X11-сборка, версия и корректная ошибка без DISPLAY |
| windows-asan | Windows 2022 | MSVC ASan runtime preflight, timer/allocator и probes |

В job headless на Unix не нужны X11 или SDL. В Unix SDL job зависимости
устанавливаются **только на одноразовом GitHub runner** (apt/brew), не на машине
владельца. Пакеты Ubuntu/Homebrew следуют версии runner/repository; в отличие
от Windows SDL ZIP они не закреплены по хешу содержимого. macOS runtime-аудио
и настоящее X11-окно этими тестами не проверяются.

## Игровые данные и полномочия

- CI не получает `refs/`, коммерческие IWAD, сохранения или секреты.
- Никаких загрузок Doom II, shareware или модов в workflow нет.
- Тесты demo/state/save-load требуют локального `DPTDOOM_TEST_IWAD` и остаются
  отдельным более глубоким набором. Зелёный data-free CI их **не заменяет**.
- Actions закреплены по commit SHA, token permissions — `contents: read`.
- Не используется `pull_request_target`, публикация релизов или push из workflow.
- Устаревшие запуски отменяются, матрица не останавливается после первой ошибки.
- При ошибке загружается только конкретный `LastTest.log` на 7 дней;
  бинарники, whole build tree и игровые ресурсы не публикуются.

## Что проверено локально

- Workflow прошёл `actionlint 1.7.12`; инструмент скачан из официального релиза
  с проверкой SHA-256, лежит только в игнорируемой `build/tools`.
- Чистая Windows MSVC headless-сборка **без IWAD**: 5/5 tests.
- Windows ASan data-free subset: 7/7 tests.
- SDL regression suite без interactive tests: 12/12 с локальным engine test.
- Linux/macOS исполнение workflow пока не подтверждено. WSL Debian найден,
  но build tools/SDL в него **не устанавливались по решению владельца**.

Для чистой локальной проверки Windows без игровых данных:

```powershell
cmake -S . -B build/ci-local -G "Visual Studio 17 2022" -A x64 -DDPTDOOM_BACKEND=headless
cmake --build build/ci-local --config Release --parallel 4
ctest --test-dir build/ci-local -C Release --output-on-failure
```

Не передавать `DPTDOOM_TEST_IWAD` в этот build tree. Для полной локальной проверки
игры — отдельные build trees из `docs/TESTING.md` и `docs/STATE-TESTING.md`.

После первого разрешённого push проверить все jobs и логи. Только после реального
успешного прогона можно отмечать Linux/macOS как проверенные соответствующими
CI-сценариями. Успешная компиляция не равна ручной игровой проверке платформы.
