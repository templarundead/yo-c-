#!/usr/bin/env python3
# Загрузка словарей оригинала (vgiv/yo) в tests/data.
#
# Зачем скрипт: yox.dat — двоичный файл в CP1251 (1 865 705 байт),
# yo_regexpr.dat в апстриме лежит в UTF-8, а движок читает его как
# CP1251 (как и оригинальная программа). Любая промежуточная обработка
# «как текста UTF-8» портит словарь (все кириллические байты заменяются
# на U+FFFD) — поэтому источник данных фиксирован коммитом, а результат
# проверяется по размеру и содержимому.
#
# Использование:
#   python3 tools/fetch_data.py            # скачать в tests/data
#   python3 tools/fetch_data.py --check     # только проверить файлы
from __future__ import annotations

import base64
import hashlib
import json
import sys
import urllib.request
from pathlib import Path

# Коммит апстрима (vgiv/yo), к которому привязаны словари.
COMMIT = "686ba1b4e226c8aaf113b8c18818e6b8aee78d1c"

# Ожидаемые размеры и версии после подготовки.
EXPECT = {
	"yox.dat": 1865705,
	"yo_regexpr.dat": 1186,
}


def api(path: str) -> dict:
	url = f"https://api.github.com/repos/vgiv/yo/contents/{path}?ref={COMMIT}"
	with urllib.request.urlopen(url) as r:
		return json.load(r)


def blob(sha: str) -> bytes:
	url = f"https://api.github.com/repos/vgiv/yo/git/blobs/{sha}"
	with urllib.request.urlopen(url) as r:
		return base64.b64decode(json.load(r)["content"])


def fetch(out_dir: Path) -> None:
	out_dir.mkdir(parents=True, exist_ok=True)

	# yox.dat: CP1251 как есть.
	info = api("binary/yox.dat")
	raw = blob(info["sha"])
	assert len(raw) == EXPECT["yox.dat"], (len(raw), EXPECT["yox.dat"])
	out_dir.joinpath("yox.dat").write_bytes(raw)
	print(f"yox.dat: {len(raw)} байт, sha256 {hashlib.sha256(raw).hexdigest()[:16]}")

	# yo_regexpr.dat: движок читает CP1251. В апстриме файл может
	# отдаваться и как CP1251, и как UTF-8 (разные API GitHub) —
	# нормализуем к CP1251.
	info = api("binary/yo_regexpr.dat")
	raw_re = blob(info["sha"])
	if len(raw_re) == EXPECT["yo_regexpr.dat"]:
		cp = raw_re
	else:
		cp = raw_re.decode("utf-8").encode("cp1251")
	assert len(cp) == EXPECT["yo_regexpr.dat"], (len(cp), EXPECT["yo_regexpr.dat"])
	out_dir.joinpath("yo_regexpr.dat").write_bytes(cp)
	print(f"yo_regexpr.dat: {len(cp)} байт (UTF-8 → CP1251), "
		  f"sha256 {hashlib.sha256(cp).hexdigest()[:16]}")


def check(out_dir: Path) -> int:
	bad = 0
	for name, size in EXPECT.items():
		p = out_dir / name
		if not p.exists():
			print(f"НЕТ {p}")
			bad = 1
			continue
		got = p.stat().st_size
		status = "ok" if got == size else f"ПОРЧА (ожидалось {size})"
		print(f"{p}: {got} байт — {status}")
		if got != size:
			bad = 1
	if bad:
		print("Восстановить: python3 tools/fetch_data.py")
	return bad


def main() -> int:
	root = Path(__file__).resolve().parents[1]
	out_dir = root / "tests" / "data"
	if "--check" in sys.argv:
		return check(out_dir)
	fetch(out_dir)
	return check(out_dir)


if __name__ == "__main__":
	raise SystemExit(main())
