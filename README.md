# yo-c- — «Ё лайт» на C++23 + Win32

Модернизация программы YOL («Ё лайт», https://github.com/vgiv/yo) —
ёфикатора русских текстов Владимира Иванова (Delphi 7, VCL, RichEdit).
Переписывание на C++23 + голый Win32 API (Direct2D/DirectWrite, DWM)
с сохранением алгоритмов и форматов словарей оригинала.
Лицензия: GPL-3.0 (как у оригинала).

## Статус

**Этап 1 — аудит и проектирование: готово, архитектура утверждена**
(с условиями §12, они выполнены на этапе 2).

**Этап 2 — MVP ядра, CLI и golden-тестов: готово.**
**Этап 3 — GUI-редактор: в работе (ядро и окно готовы, идёт полировка).**

- `src/core` — статическая библиотека без Win32: CP1251/UTF-8/UTF-16,
  словарь `yox.dat`, словарь РВ `yo_regexpr.dat` (RE2 + транслятор),
  движок ёфикации (точный перенос `SelectWord`/`FindReplacement`/
  `LookInRegExps`/`CheckWord`).
- `src/cli/yo-cli` — unix-фильтр: результат в stdout, отчёты в stderr,
  `--on-ambiguous=skip|ye|yo|fail|ask`, `--in-place` (атомарно) +
  `--backup`, `-o/--out[--dir]`, `--encoding/--from/--to`,
  `--regex-log`, `--report=text|json`, `--stats`, `-q`;
  коды возврата 0/1/2/3.
- `tests/` — Catch2: **37 тест-кейсов, 661 366 проверок**, включая
  **golden-паритет 24/24** (8 файлов × 3 политики, слово в слово
  и счётчики статистики) с Python-оракулом `tools/golden.py`
  и fuzz-сверку документного буфера с эталонной моделью
  (правки, undo/redo, CRLF/LF/CR, mmap).
- `src/core/mapped.hpp`, `textbuf.hpp` — документное ядро GUI:
  `MappedFile` (mmap, файл не копируется и не меняется) + `TextBuffer`
  (piece table, индекс начал строк, undo/redo); 1 МБ открывается
  за ~2–3 мс, 100 МБ — за ~0.2 с, **1 ГБ — за ~2.5 с** (индекс
  262 МБ на 32.7 млн строк); правка в конце файла — единицы микросекунд,
  в середине 20 МБ — ~0.4 мс (ARCHITECTURE.md §2.3.1).
- `src/core/editor.hpp` — редактор без Win32: курсор, выделение, ввод,
  прокрутка, ленивая выдача видимых строк, геометрия моноширинного
  текста (всё покрыто тестами).
- `src/app/` — GUI (Windows, `-DYO_BUILD_GUI=ON`): окно Win32,
  Direct2D/DirectWrite, меню, контекстное меню правой кнопки и сочетания
  клавиш, открытие/сохранение в исходной кодировке и выбор кодировки
  (CP1251/UTF-8/UTF-8 BOM/UTF-16LE/BE) и перевода строк (LF/CRLF/CR),
  буфер обмена (`CF_UNICODETEXT` и `CF_TEXT`), полоса поиска и замены,
  прокрутка колесом
  (в т.ч. тачпадами), полосой и Shift+колесом по горизонтали, масштаб
  Ctrl+колесом, drag&drop, тёмное оформление (манифест с Common Controls 6,
  `DarkMode_Explorer` для полос прокрутки), per-monitor DPI.
  Манифест — `src/app/yo-gui.manifest`: без него системные полосы прокрутки
  и меню рисуются в классическом стиле и выглядят «белыми».
  При `YO_GUI_TRACE=1` в `%TEMP%\yo-gui-trace.log` пишутся длительности
  вставки, отрисовки (>16 мс) и ёфикации — для разбора «зависаний».
- `src/core/find.hpp` — поиск и замена без Win32: регистр, «слово целиком»,
  все совпадения, замена одной группой undo, поиск по образцу-регулярному
  выражению (RE2 — тот же движок, что у словаря РВ). В GUI — полоса снизу
  (`Ctrl+F` — найти, `Ctrl+H` — заменить): образец и строка замены, «Aa» и
  «Слово», «Далее»/«Назад», «Заменить»/«Заменить всё», подсветка совпадений
  в видимых строках и счётчик «N из M» в статусбаре; `F3`/`Shift+F3`,
  `Ctrl+Shift+R` — образец РВ. Меню «Вид» — номера строк (`Ctrl+Shift+L`)
  и перенос по словам (`Ctrl+Shift+W`, по умолчанию выключен; в оригинале
  переноса не было),
  `Ctrl+W`/`Ctrl+Q` — переход по сделанным заменам, `Shift+F10` — убрать их
  подсветку, `Ctrl+P` — свойства файла, `F1` — о программе.
- `src/core/yofytext.hpp` — ёфикация кусками по 2048 строк (`yofify_document`)
  и одиночным диапазоном (`yofify_range`): память ограничена куском, все
  правки — одна группа undo, прогресс и `Esc` работают на всём диапазоне.
  Ответы «Всегда Е/Ё» сохраняются в `yo_user.dat` рядом с `yo.ini`
  (`src/core/usersel.hpp`), поэтому переживают перезапуск, а словарь
  остаётся в формате поставки.
- `src/core/yofytext.hpp` — ёфикация документа (Ctrl+E): выделение или
  весь файл, одной группой undo, подсветка ё и статистика в статусбаре.
- Последние файлы — как у оригинала: `[Files] Name1..9` в `yo.ini`,
  меню «Файл» с пунктами `Ctrl+1…9`, «Очистить список последних файлов»,
  `RecallLastFile` (последний файл открывается при старте), запоминание и
  восстановление позиции курсора и кодировки файла.
- Редактор словарей (`Ctrl+Shift+D`, меню «Файл») — панель поверх документа:
  вкладки «Слова» (`yox.dat`) и «Правила РВ» (`yo_regexpr.dat`), таблица с
  фильтром (без учёта регистра и е/ё), добавление/правка/удаление с
  проверкой (`IsWord`, наличие «ё», дубликаты, компиляция правил RE2),
  автоматическое сохранение порядка `CmpYeEx`, запись обратно в CP1251/CRLF
  с шапкой-комментариями, тестовое поле «контекст → ответ» для правил.
  Модель и проверки — в ядре (`src/core/dictedit.hpp`), покрыты тестами.
- Окно настроек (`Ctrl+,`) — наложение в тёмной теме: галочки ёфикации и
  редактора, размер текста, пути словарей; «Применить», «Запомнить»
  (пишет `yo.ini`), «Отмена», мышь и клавиатура.
- Настройки — `yo.ini` рядом с exe (portable, как у оригинала): размер
  шрифта, геометрия окна, `AlwaysAsk`, `SilentYofy`, `Mark`,
  `ProposeLast`, `RegExprs`, `ToConfirmAbbr`, пути `Dictionary` и
  `RegExprDictionary`, `WordWrap` (наше дополнение). Ключи названы как в оригинале (FORMATS.md §3),
  файл пишется при изменении настроек и при закрытии окна.
- `src/core/askpanel.hpp` — панель вопросов ёфикации: тексты и раскладка
  кнопок в ядре (проверяются тестами без Windows); горячие клавиши `1`–`6`,
  `Enter`, `Esc`, ответы Е/Ё/Всегда/Пропустить/Откат/Прервать. Панель —
  полоса во всю ширину над строкой состояния (как AskPanel оригинала):
  предложение с обрабатываемым словом по центру (слово красным), под ним
  заголовок, ниже две колонки кнопок (`1` — синяя, `2` — зелёная,
  «Прервать» — во всю ширину); «Откат» снимает последнюю сделанную замену
  прохода, а не весь проход;
  ответ «Пропустить» запоминается на текущий проход, поэтому частое слово не
  порождает тысячи одинаковых вопросов. Проход отменяется `Esc` и закрытием
  окна даже между вопросами (`yofify_range` принимает внешний предикат
  отмены), а в статусбаре идёт «ёфикация: строка N из M».
- Сборка GUI проверяется кросс-компиляцией (zig + CMake, mingw-цель,
  включая Abseil/RE2): `yo-gui.exe` собирается и линкуется. Одной командой:
  `tools/build-windows.sh` (см. «Кросс-сборка под Windows» ниже).
- `bench/` — Google Benchmark: загрузка словаря, поиск словоформы,
  правило РВ, ёфикация 1 МБ и 10 МБ (**≈31 МБ/с** на ядро,
  см. ARCHITECTURE.md §5.2.1), документное ядро и кадр отрисовки
  (1 ГБ, 100 МБ, правки, §2.3.1).

Осознанные отличия от оригинала — [DEVIATIONS.md](DEVIATIONS.md),
сторонние компоненты и их лицензии — [NOTICE](NOTICE).

- [FEATURES.md](FEATURES.md) — полный список функций оригинала
  (меню, ёфикация, поиск, файлы, настройки, статистика, квирки).
- [FORMATS.md](FORMATS.md) — точные форматы `yox.dat`, `yo_regexpr.dat`,
  `yo.ini`, текстовых файлов и лога regex.
- [ARCHITECTURE.md](ARCHITECTURE.md) — схема модулей и потоков данных,
  структура каталогов, CMake presets, риски, обоснование альтернатив
  (свой контрол vs RichEdit, RE2 vs свой движок, UTF-8 vs UTF-16).
- [CMakePresets.json](CMakePresets.json) — пресеты
  (clang-cl Windows + Linux CI для ядра).
- [DEVIATIONS.md](DEVIATIONS.md) — осознанные отличия от оригинала.
- [NOTICE](NOTICE) — сторонние компоненты и их лицензии.

## Структура проекта

```
src/core/    # статическая библиотека без Win32 (готово, этап 2):
             #   cyr/cp1251/codec — кириллица, кодировки, LineReader/LineWriter
             #   dict            — yox.dat и поиск словоформы
             #   regexdict       — yo_regexpr.dat, RE2 + транслятор
             #   yofy            — движок ёфикации
src/cli/     # yo-cli.exe: unix-фильтр (готово, этап 2)
src/app/     # GUI (yo.exe): собственный редактор, AskPanel, диалоги (этап 3+)
tests/       # Catch2: юнит-тесты + golden-паритет 24/24
bench/       # Google Benchmark: словарь, поиск, РВ, ёфикация 1/10 МБ
tools/       # golden.py (Python-оракул), regex_audit.py, regex_probe.cpp
```

## Сборка

Целевая платформа: Windows 10+, clang-cl (LLVM), CMake 3.28+, Ninja.
Зависимости (RE2, Catch2, Google Benchmark) — через FetchContent,
без vcpkg (см. ARCHITECTURE.md §12 п.7).

```powershell
cmake --preset win-clang-debug
cmake --build --preset win-clang-debug
ctest --preset win-clang-debug
```

Компилятор: **clang ≥ 21** (в том числе clang из zig 0.15+) — под него
настроены предупреждения (`-Wconversion -Wsign-conversion -Wold-style-cast`)
и на нём идут CI и замеры; **MSVC 19.4x** — для Windows-поставки
(`win-msvc-release`). GCC не проверялся: CLI требует `<print>` из C++23
(GCC ≥ 14), а набор предупреждений проекта рассчитан на clang.

Ядро и CLI собираются и на Linux (zig-clang / clang++ ≥ 21) — так
идут CI и замеры:

```bash
cmake -G Ninja -S . -B build \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/zig-clang.cmake \
  -DCMAKE_BUILD_TYPE=Release -DYO_BUILD_BENCH=ON -DYO_BUILD_TESTS=ON
cmake --build build -j
./build/tests/yo_tests                    # тесты
./build/bench/yo_bench --benchmark_min_time=0.3s   # бенчмарки
```

### Кросс-сборка под Windows с Linux-хоста

Windows-SDK не нужен: zig приносит и компилятор, и mingw-заголовки/CRT
с библиотеками `d2d1`, `dwrite`, `dwmapi`, `comdlg32`, `shell32`.

```bash
python3 -m pip install --user ziglang ninja   # cmake >= 3.28 — отдельно
export PATH="$HOME/.local/bin:$PATH"

tools/build-windows.sh          # или вручную:
# cmake -G Ninja -S . -B build-win64 \
#   -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/zig-windows.cmake \
#   -DCMAKE_BUILD_TYPE=Release \
#   -DYO_BUILD_GUI=ON -DYO_BUILD_CLI=ON -DYO_BUILD_TESTS=OFF
# cmake --build build-win64 -j
```

Результат: `build-win64/src/app/yo-gui.exe` и `build-win64/src/cli/yo-cli.exe`
(PE32+ x86-64), плюс `dist-win64/` и `yo-windows-x64.zip` с exe, словарями
`yox.dat`/`yo_regexpr.dat` и лицензиями — в таком виде комплект запускается
на Windows как есть.

Проверить без Windows можно так: `zig`-сборка статически линкует mingw-CRT,
поэтому под wine работает и GUI, и CLI, и тесты (`-DYO_BUILD_TESTS=ON`,
запуск `wine build-win64/tests/yo_tests.exe`). Поставочная сборка для Windows
всё равно собирается на Windows через `clang-cl` (`CMakePresets.json`).

Куда попадают бинарники (пути повторяют дерево `src/`, не корень сборки):

| Цель | Путь |
|---|---|
| `yo_tests` | `build/tests/yo_tests` |
| `yo-cli` | `build/src/cli/yo-cli` |
| `yo-regex-probe` | `build/src/cli/yo-regex-probe` |
| `yo_bench` | `build/bench/yo_bench` (только с `-DYO_BUILD_BENCH=ON`) |
| `yo-gui.exe` | `build/src/app/yo-gui.exe` (только Windows, `-DYO_BUILD_GUI=ON`) |

`yo-cli` использует `std::print`/`std::println` из C++23 (`<print>`): нужен
libstdc++ ≥ 14, libc++ ≥ 18 (проще всего собрать драйвером zig) или MSVC STL
17.10+. На Debian 12 штатный libstdc++ 12 такого заголовка не имеет — clang
соберёт ядро и тесты, а `yo-cli` останется «несобранным», и CMake выдаст
предупреждение при конфигурации.

Офлайн-сборка: `-DFETCHCONTENT_SOURCE_DIR_RE2=...` и т.п. (см.
cmake/Dependencies.cmake).

### Данные для тестов

`tests/data/yox.dat` (CP1251, 1 865 705 байт) и `tests/data/yo_regexpr.dat`
(CP1251, 1186 байт) взяты из апстрима `vgiv/yo` и зафиксированы по
коммиту. Перевыкачать и проверить:

```bash
python3 tools/fetch_data.py           # скачать в tests/data
python3 tools/fetch_data.py --check   # проверить размеры
```

Важно: словари — **CP1251**. Любая обработка «как текста UTF-8»
(редакторы, конвертеры, копирование через текстовые буферы) заменяет
кириллические байты на U+FFFD и тихо ломает паритет — тесты это
ловят (`словарь yox.dat загружается без нарушений`,
`словарь регулярных выражений: 94 правила компилируются`).

## CLI: примеры

```bash
yo-cli --dic yox.dat --regex yo_regexpr.dat text.txt > out.txt
yo-cli --dic yox.dat --regex yo_regexpr.dat --in-place --backup *.txt
yo-cli --dic yox.dat --on-ambiguous=yo --stats < in.txt > out.txt
yo-cli --dic yox.dat --report=json text.txt 2> report.json > out.txt
```

Кодировка, BOM и переводы строк сохраняются; `--from/--to` их
переопределяют. Неоднозначности: `skip` (по умолчанию вне TTY),
`ye`/`yo`, `fail` (код 2), `ask` (только в TTY).
