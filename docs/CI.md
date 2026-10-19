# Непрерывная интеграция

Workflow: `.github/workflows/ci.yml`. Запускается на push, pull request и вручную
через `workflow_dispatch`. Push выполняет владелец; агент без отдельной команды
не пушит. Подтверждённые remote runs перечислены ниже; наличие YAML не означает
зелёный CI текущей ревизии.

Обновление 19.10.2026: через public GitHub API подтверждён успешный run
[#7, 21aae1e](https://github.com/dpteam/DPTDOOM/actions/runs/39262022307) до master.
В run [#9, 330fcd8](https://github.com/dpteam/DPTDOOM/actions/runs/39265440252)
Windows headless/SDL/ASan jobs успешны, Linux/macOS jobs падают после добавления
master-loopback. Подробный traceback без авторизации недоступен. Подготовлены
test portability fixes (loopback alias capability, paced full-list test с TTL60)
и case-level failure logs; успешный remote rerun нового master пока не подтверждён.
Это не полная ручная проверка игры Linux/macOS.

Подтверждено исправление: run
[#11, 6061a13](https://github.com/dpteam/DPTDOOM/actions/runs/39269095812) завершён
**success** (public GitHub API, 19.10.2026), также подтверждено владельцем.
Headless/SDL Windows/Linux/macOS и Windows ASan в этом run прошли. Это проверка
именно той ревизии и data-free сценариев; новые browser/publication изменения
ещё требуют следующего remote run. Ручная игра Linux/macOS не проверена.

Поздний remote run [#17, cd497cc](https://github.com/dpteam/DPTDOOM/actions/runs/39292636104)
подтвердил: Windows SDL/headless/ASan и Ubuntu SDL/headless зелёные, падали только
macOS SDL/headless. Его job logs без GitHub authentication недоступны (API отдаёт
403); annotations показывают только `ctest` exit 8. Причина этого нового падения
**пока не установлена**: предыдущие loopback-alias guards/pacing уже входили в
6061a13 и не объясняют автоматически failure более позднего cd497cc. Нужен
traceback из macOS LastTest.log/Actions и новый remote run после исправления.
Локальные Windows suites не являются подтверждением исправленного macOS.

Node warning про actions, которые таргетят Node 20 и forced runner Node 24, не
является падением теста. Pinned action revisions пока не менять без отдельной
проверки release notes/actionlint; GitHub уже выполняет их на Node 24.

Владелец уточнил test failure macOS: **master-client-wire**, не master-loopback
и не compiler failure (96%, 1/24). Точный traceback ещё не предоставлен. Устранена
гонка самого slow-peer fixture: вместо fixed sleep 3s vs native deadline 2.5s
сервер ждёт фактического выхода клиента, сохраняя соединение открытым; deadline
клиента не увеличен. Добавлены имена каждого сценария, EOF guard в request read
и native diagnostics до assertion. Это test-hardening, не подтверждённый root
cause/fix macOS до получения traceback/нового remote run.

Получен полный traceback владельца: macOS прошёл все response/timeout/rejection
cases и упал **только на финальном bound-but-not-listening socket**: native client
корректно вернул `stage=connect reason=deadline sent=0/16`, а fixture ошибочно
требовал `reason=connect os_error!=0`. Это не ошибка парсинга master и не Node.
Тест теперь допускает два корректных ОС-исхода (refusal или deadline), но требует
FAILED/connect, нулевую отправку/приём; для refusal по-прежнему нужен ненулевой
OS error. Отдельно проверяется порт после закрытия reservation. Клиентские
таймауты не увеличены, macOS scenarios не пропускаются. Нужен remote rerun.

Подтверждён remote rerun: [ddd69cc / run 39304002144](https://github.com/dpteam/DPTDOOM/actions/runs/39304002144)
завершён **success** (public API, также подтверждено владельцем). Таким образом,
исправление bound-non-listener fixture прошло macOS Headless/SDL в Actions.
Новый Thing subset после этой ревизии ещё требует следующего remote run.

## Матрица

| Job | Runner | Проверки без игровых данных |
|---|---|---|
| headless | Windows 2022 / Ubuntu 24.04 / macOS 14 | Сборка, версия, startup, timer/zone, native master model/loopback |
| windows-sdl | Windows 2022 | SDL + WebView2 SDK с SHA-256, сборка, dummy input/audio, launcher-model |
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
  whole build tree и игровые ресурсы не публикуются.

## Скачать тестовые сборки (Artifacts)

После успешного будущего Actions run открыть GitHub → Actions → нужный запуск →
Artifacts. GitHub обычно требует вход в аккаунт для скачивания artifacts.

- `dptdoom-windows-x64-sdl-<commit>` — игровой ZIP, EXE + SDL DLL, без IWAD.
  Также HTML launcher, WebView2Loader.dll и SDK LICENSE/NOTICE. Launcher требует
  установленного WebView2 Evergreen Runtime; standalone engine от него не зависит.
  Portable: `portable.txt` рядом с launcher → `user/`, см. [LAUNCHER.md](LAUNCHER.md).
- `dptdoom-headless-<os>-<commit>` — диагностический tar.gz, без окна/аудио.
- В обоих видах bundle есть отдельный нативный `dptdoom-master[.exe]` — пока
  только directory-прототип, не игровой server; [MASTER.md](MASTER.md).
- Срок хранения — 30 дней. Это CI playtest builds, **не релизы**.
- Пакеты содержат `START-HERE.txt`, `BUILD.json`, `SHA256SUMS.txt`, лицензии и
  `SOURCE.zip` с исходниками того же коммита (для GPL и воспроизводимости).
- Проверка `packaging` на синтетическом git fixture проверяет allowlist,
  запрет dirty/overwrite и недопущение tracked game data в source ZIP.
- Игровой Windows preset использует статический MSVC CRT; SDL DLL копируются
  через CMake install. Если нужны дополнительные системные компоненты Windows,
  инструкция должна уточняться после проверки на чистой машине.
- Unix-пакеты пока только headless. Playable Linux/macOS SDL binary bundle не
  публикуется, пока не настроена корректная упаковка системных SDL-зависимостей.
- ASan runtime не публикуется как обычная игровая сборка.

Упаковщик `tools/package-build.py` берёт **строгий allowlist** файлов из CMake
install, не целый build directory. Источник — `git archive HEAD`; упаковка
отказывается работать при изменённых tracked sources и не затирает существующий
архив. Поэтому коммитить перед упаковкой. Проверка source ZIP запрещает game
data/save/config/frame/log файлы. Будущие tracked config templates потребуют
отдельного осмысленного изменения allowlist, не отключения проверки.

Пакеты не создаются при провалившихся тестах соответствующего job. Объём
проверки без IWAD ограничен; перед игрой с друзьями дополнительно прогнать
локальные network tests и убедиться, что оба скачали **один и тот же artifact**.

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
