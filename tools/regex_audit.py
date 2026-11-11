#!/usr/bin/env python3
"""Предполётный аудит yo_regexpr.dat на совместимость с RE2.

Повторяет MakeValidRegExpr из Main.pas (CP1251, как в оригинале) и
проверяет исходные и транслированные шаблоны на конструкции, которых
нет в RE2: lookaround, backreferences, атомарные группы, условные
группы, possessive-кванторы, рекурсия, \\0-формат и т.п.

Использование:
    python3 tools/regex_audit.py <path/to/yo_regexpr.dat> [--json out.json]

Результат — таблица в stdout и (опционально) JSON для FORMATS.md.
Код возврата: 0 — все правила переводимы, 3 — есть непереводимые.
"""

from __future__ import annotations

import argparse
import json
import sys

SPACE = frozenset(" \t\n\r\x0c\xa0")
YE = frozenset("еёЕЁ")


def is_alpha(c: str) -> bool:
    return (
        "а" <= c <= "я"
        or "А" <= c <= "Я"
        or c in "ёЁ"
        or "a" <= c <= "z"
        or "A" <= c <= "Z"
    )


def is_upper(c: str) -> bool:
    return "А" <= c <= "Я" or c == "Ё" or "A" <= c <= "Z"


def make_valid(s: str) -> str:
    """Точная копия MakeValidRegExpr (включая квирк с WasBlank)."""
    out: list[str] = []
    was_blank = False
    was_backslash = False
    brackets = 0
    for ch in s:
        if ch in SPACE:
            if not was_blank:
                out.append(r"\s+")
            else:
                was_blank = True
            continue
        was_blank = False
        if not was_backslash and ch in "([":
            brackets += 1
        elif not was_backslash and ch in ")]":
            brackets -= 1
        if brackets == 0:
            if ch in YE:
                out.append("([ЕЁ])" if is_upper(ch) else "([её])")
            elif ch == "-":
                out.append(r"-(\r\n){0,1}")
            else:
                out.append(ch)
        else:
            out.append(ch)
        was_backslash = ch == "\\"
    ss = "".join(out)
    if ss and is_alpha(ss[0]):
        ss = r"\b" + ss
    if ss and is_alpha(ss[-1]):
        ss = ss + r"\b"
    return ss


# (имя, regex, поддержка в RE2, комментарий)
CHECKS: list[tuple[str, str, str, str]] = [
    ("lookahead", r"\(\?[=!]", "нет", "переписать через ручную проверку границы"),
    ("lookbehind", r"\(\?<[=!]", "нет", "переписать или пометить правилом-исключением"),
    ("atomic_group", r"\(\?>", "нет", "убрать (RE2 не нуждается в атомарности)"),
    ("conditional", r"\(\?\(", "нет", "переписать на альтернативы"),
    ("comment_group", r"\(\?\#", "нет", "удалить комментарий"),
    ("named_group", r"\(\?P<|\(\?<[A-Za-z]", "нет", "заменить на нумерованную группу"),
    ("backreference", r"\\[1-9]|\\k<|\\g\{", "нет", "непереводимо без lookaround/захвата"),
    ("possessive", r"(?:[*+?]|\{\d+,?\d*\})\+", "нет", "заменить на жадный/ленивый"),
    ("recursion", r"\(\?R\)|\(\?0\)", "нет", "непереводимо"),
    ("nul_escape", r"\\0", "нет", "старый формат словаря"),
    ("escape_Z", r"\\Z", "нет", "заменить на \\z"),
    ("escape_G", r"\\G", "нет", "неподдерживаемо"),
    ("escape_K", r"\\K", "нет", "неподдерживаемо"),
    ("escape_Q", r"\\Q", "нет", "экранировать вручную"),
    ("escape_X", r"\\X", "нет", "неподдерживаемо"),
    ("posix_class", r"\[\[:", "частично", "RE2: только ASCII-классы"),
    ("nonword_boundary", r"\\B", "ASCII", "нужна трансляция \\b/\\B"),
    ("word_boundary", r"\\b", "ASCII", "внутри шаблона — отклонение"),
    ("class_w", r"\\[wW]", "ASCII", "транслируется в [А-Яа-яЁё]"),
    ("class_s", r"\\[sSdD]", "ASCII", "\\s -> MySpaceChars, \\d -> [0-9]"),
    ("inline_flag", r"\(\?[a-zA-Z-]+[:)]", "да", "RE2 поддерживает (?i)/(?i:...)"),
    ("escape_p", r"\\[pP]\{", "да", "RE2 поддерживает Unicode-свойства"),
    ("anchor_A_z", r"\\[Az]", "да", "RE2 поддерживает \\A и \\z"),
]


def scan(rx: str) -> list[tuple[str, str, str]]:
    import re

    hits = []
    for name, pat, support, note in CHECKS:
        if re.search(pat, rx):
            hits.append((name, support, note))
    return hits


def inner_word_boundary(valid: str, raw: str) -> bool:
    body = valid
    if body.startswith(r"\b"):
        body = body[2:]
    if body.endswith(r"\b"):
        body = body[:-2]
    return r"\b" in body or r"\B" in body


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("path")
    ap.add_argument("--json")
    ap.add_argument("--verbose", action="store_true")
    args = ap.parse_args()

    data = open(args.path, "rb").read().decode("cp1251")
    rules: list[str] = []
    comments: list[str] = []
    for line in data.splitlines():
        s = line.rstrip("\r")
        if not s.strip() or s.lstrip().startswith("#"):
            comments.append(s)
            continue
        rules.append(s)

    rows = []
    counts: dict[str, int] = {}
    for n, raw in enumerate(rules, 1):
        valid = make_valid(raw)
        raw_hits = scan(raw)
        valid_hits = scan(valid)
        inner_b = inner_word_boundary(valid, raw)
        rows.append(
            {
                "n": n,
                "raw": raw,
                "valid": valid,
                "raw_hits": [h[0] for h in raw_hits],
                "valid_hits": [h[0] for h in valid_hits],
                "inner_word_boundary": inner_b,
            }
        )
        for name in {h[0] for h in raw_hits} | {h[0] for h in valid_hits}:
            counts[name] = counts.get(name, 0) + 1

    print(f"файл: {args.path}")
    print(f"строк: {len(comments) + len(rules)}, правил: {len(rules)}, "
          f"комментариев: {len(comments)}")
    version = [c for c in comments if "ерсия" in c]
    if version:
        print(f"версия: {version[0].strip()}")
    print("\nконструкции (правил с конструкцией):")
    for name, cnt in sorted(counts.items(), key=lambda kv: -kv[1]):
        support = next(s for n, _, s, _ in CHECKS if n == name)
        print(f"  {name:22s} {cnt:3d}   RE2: {support}")

    hard = [r for r in rows if r["raw_hits"] and not set(r["raw_hits"]) <= {"inline_flag", "escape_p", "anchor_A_z"}]
    print(f"\nправил с конструкциями, требующими переписывания: {len(hard)}")
    for r in hard:
        print(f"  #{r['n']}: {r['raw']!r} -> {r['valid']!r}  {r['raw_hits']}")

    inner = [r for r in rows if r["inner_word_boundary"]]
    print(f"\nправил с \\b/\\B внутри шаблона: {len(inner)}")
    for r in inner:
        print(f"  #{r['n']}: {r['raw']!r} -> {r['valid']!r}")

    if args.verbose:
        print("\nвсе правила:")
        for r in rows:
            print(f"  #{r['n']:3d} {r['raw']!r}\n        -> {r['valid']!r}")

    if args.json:
        json.dump({"rules": rows, "counts": counts}, open(args.json, "w"),
                  ensure_ascii=False, indent=1)
    return 3 if hard else 0


if __name__ == "__main__":
    sys.exit(main())
