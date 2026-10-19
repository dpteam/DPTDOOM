# Локальные csDoom/ACE: первичная карта источников

Проверено 19.10.2026 после добавления владельцем двух игровых деревьев в refs.
Это **source-only inventory**, не сборка/runtime-аудит и не решение включить код.
Все `refs/` остаются игнорируемыми; бинарники, obj/build trees, архивы и игровые
данные из этих каталогов не копируются и не запускаются.

## Что есть

| Каталог | Наблюдение | Кандидаты для будущего аудита |
|---|---|---|
| `refs/csDoomOld07` | `common/`, `doomcl/`, `doomsv/`, `serversspy/`; README называет complete client/server source; server welcome string `Version 0.7` | Исходная архитектура client/server, старый DeHackEd/BEX и map mechanics |
| `refs/csDoomNew` | `client/`, `server/`, `master/`, `launcher/`, `launcher2/`, `stats/`, `tools/`; server welcome string `Version 1.4a` | Сравнить изменения сервера/протокола, prediction и parser с 0.7 |
| `refs/csDoomMaster` | Отдельный старый directory/master | Уже изучен; не игровой порт, текущий DPT master написан отдельно |
| `refs/doom_ace-master` | DOS Doom II 1.9 enhancement + injection/exploit часть | `engine/dehacked.*` и другие engine modules после проверки зависимостей; exploit не включать |

Имена `New`/`Old07` и welcome strings — локальные признаки, **не подтверждение**
точного upstream commit, даты или полноты релизов. Историю происхождения архива
и модификации каждого файла ещё установить перед переносом.

## Уже найдены нужные места

- DeHackEd/BEX: `csDoomNew/{client,server}/d_dehacked.cpp` и
  `csDoomOld07/{doomcl,doomsv}/src/d_dehacked.cpp`.
  Есть handlers Thing/Sound/Frame/Sprite/Ammo/Weapon/Pointer/Cheat/Misc/Text,
  BEX `[STRINGS]`, `[PARS]`, `[CODEPTR]`, загрузка `DEHACKED` lump и внешнего patch.
  Это свидетельство наличия реализации, **не** проверка её parsing safety или
  полноты совместимости. Зависит от info/states, strings, sounds, g_level и
  специфичных типов порта; прямое копирование файла не даст готовую поддержку.
- Boom: `doomdata.h` (ML_PASSUSE_BOOM), `p_floor.cpp` (generalized sector/stairs),
  `p_setup.cpp` (map/blockmap extensions), `info.h` (push/pull), `m_random.h`.
  Требуется проверить реальную семантику/перечень specials, а не только комментарии.
- MBF: в обоих деревьях `r_plane.cpp` содержит linedef-controlled skies.
  Это **не доказательство полного MBF**. MBF21 по первичному поиску не найден;
  не приписывать старому порту современный compatibility level.
- Сеть: `server/sv_main.cpp`, `sv_main.h`, `d_protocol.*`, `i_net.*`,
  `client/cl_main.*`, `cl_pred.cpp`. Старый client/server protocol не совместим
  с нынешним DPT direct v2; повторное использование потребует отдельного дизайна,
  bounds/security review и tests, не незаметной замены сетевого кода.
- Проверенные пары DeHackEd client/server, sv_main и cl_pred между New/Old
  **не побайтово одинаковы даже после нормализации CRLF**. Не считать New просто
  дубликатом Old и не объединять деревья автоматически.

## Лицензии: не делать вывод по одному имени файла

- `csDoomOld07/readme.txt` объявляет GPL для кода; рядом `gnu.txt` — GPL v2.
  Это полезное свидетельство, но не отменяет сторонние лицензии/старые headers.
- `csDoomNew/client/license.txt` — **MIDAS Digital Audio System**, содержит
  ограничения коммерческого использования. Это не общая лицензия игрового csDoom.
  Старый MIDAS/его библиотеки не включать в GPL bundle автоматически; у нас SDL audio.
- `csDoomNew/server/Licence` — лицензия старого **NASM**, не server source.
- В игровых файлах, например `client/doomdef.h`, встречаются старые DOOM Source
  Code License headers. Нужны file-level provenance и подтверждение применимого
  GPL/relicensing для заимствуемого кода, особенно старых ZDoom/сторонних частей.
  У `d_dehacked.cpp` есть attribution к исходному DeHackEd Greg Lewis — его
  происхождение/условия также проверить. Никакого blanket «всё refs = GPL».
- ACE LICENSE — LGPL 2.1; file-level notices, dependencies и условия адаптации
  в GPL-проект нужно учитывать отдельно, как и 32-bit/DOS memory assumptions.

## Порядок дальнейшей работы

1. Составить scope classic DeHackEd/BEX и набор маленьких synthetic patches.
2. Сравнить ACE, csDoom New/0.7 и доступную GPL-базу DPT по конкретным handlers.
3. Для выбранных фрагментов зафиксировать origin/version/license/copyright и
   проверить parser bounds/индексы/числа/includes, 64-bit portability.
4. Внедрять маленькими модулями с vanilla baseline, save/load и network сверкой.
5. Boom/MBF mechanics оценить отдельно; наличие parser не включает их само собой.

Пока из этих новых csDoom деревьев **ничего в движок не перенесено**.

Detailed compatibility/parser plan: `docs/DEHACKED-MBF-AUDIT.md`.
