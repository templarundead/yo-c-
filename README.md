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
  Direct2D/DirectWrite, меню и акселераторы, открытие/сохранение
  в исходной кодировке, буфер обмена, find-bar, полосы прокрутки,
  drag&drop, тёмный заголовок, per-monitor DPI.
- `src/core/yofytext.hpp` — ёфикация документа (Ctrl+E): выделение или
  весь файл, одной группой undo, подсветка ё и статистика в статусбаре.
- `src/core/askpanel.hpp` — панель вопросов ёфикации: тексты и раскладка
  кнопок в ядре (проверяются тестами без Windows); горячие клавиши `1`–`6`,
  `Enter`, `Esc`, ответы Е/Ё/Всегда/Пропустить/Откат/Прервать.
- Сборка GUI проверяется кросс-компиляцией (zig + CMake, mingw-цель,
  включая Abseil/RE2): `yo-gui.exe` собирается и линкуется.
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
