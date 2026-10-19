# README showcase

Выбранные PNG добавлены по явной просьбе владельца. Это исключение из запрета
коммитить временные игровые captures, не разрешение добавлять IWAD/извлечённые
текстуры/спрайты/демо/сохранения. Скриншоты Doom II показывают работу source port;
права на игровые изображения принадлежат правообладателям Doom II, а не
переходят под GPL движка. Коммерческий IWAD не распространяется.

## Получение без фона Windows

```powershell
python tools/capture-showcase.py `
  --engine build/headless/dptdoom-headless.exe `
  --iwad refs/iwads/doom2.wad `
  --launcher build/windows-sdl/Release/dptdoom-launcher.exe
```

Нужен Pillow, а для launcher — Windows/WebView2 Runtime. По умолчанию результаты
идут в игнорируемый `build/showcase`, **не перезаписывают showcase в Git**.
Перед обновлением tracked PNG осмотреть каждый файл и скопировать только
выбранные изображения. Сохранять разумный размер/количество и PNG без metadata.

- Launcher: настоящий WebView2 из EXE, CDP `Page.captureScreenshot` (1180×1240),
  локальный isolated data dir. Light/dark темы. Скрыта строка с временным путём,
  EXE/IWAD пути заменены иллюстративными `C:\Games\...`; Save/Launch не вызываются.
  Скрипт не фотографирует экран/GDI/внешние окна, не меняет реальные профили.
- Game: headless-рендер текущего движка, встроенные IWAD DEMO1/2/3 на
  350/500/700 тиках. PPM берётся из RGB framebuffer и lossless преобразуется
  в PNG 640×400, без обрезки/дорисовки/изменения цветов или aspect correction.
  Это то же соотношение сторон, которое сейчас использует SDL backend.
- Все raw PPM/конфиги/логи создаются только во временном рабочем каталоге,
  не включаются в Git; снимки не являются evidence сетевой игры/нового рендерера.
- Эталон для этих трёх кадров — локальный Doom II IWAD владельца. Другие
  IWAD/демо/версии/параметры могут дать другое изображение; это не state baseline.

В binary-пакете showcase не отдельный runtime payload: только исходный SOURCE.zip
может содержать README-картинки. Игровые данные для запуска по-прежнему нужны свои.
