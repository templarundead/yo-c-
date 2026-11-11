#!/usr/bin/env python3
"""Эталонная реализация логики ёфикации оригинала (Main.pas) на Python.

Независимая реализация нужна как оракул для golden-тестов C++-ядра:
оригинальный yol.exe — Windows/Delphi-приложение, запустить его в CI нельзя,
поэтому паритет проверяется двумя независимыми реализациями одного алгоритма
(эта и C++) на общем корпусе.

Повторяются: Cmp/FindInStringsUL, ParseWord, SelectWord, FindReplacement,
LookInRegExps/MatchRegExp, CheckWord/ChangeLetter, статистика.
GUI-часть (AskPanel) заменена политикой: skip | ye | yo.

Использование:
    python3 tools/golden.py --dic yox.dat --regex yo_regexpr.dat \\
        --policy skip corpus/*.txt --out-dir tests/golden
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path

SPACE = set(" \t\n\r\f\xa0")
YE = set("еёЕЁ")


def is_lower(c: str) -> bool:
	return "а" <= c <= "я" or c == "ё"


def is_upper(c: str) -> bool:
	return "А" <= c <= "Я" or c == "Ё"


def is_alpha(c: str) -> bool:
	return is_lower(c) or is_upper(c)


def is_ye(c: str) -> bool:
	return c in "еЕ"


def is_yo(c: str) -> bool:
	return c in "ёЁ"


def is_ye_yo(c: str) -> bool:
	return c in YE


def is_word_char(c: str) -> bool:
	return c.isascii() and c.isalnum() or is_alpha(c)


def lc(s: str) -> str:
	out = []
	for c in s:
		if "A" <= c <= "Z":
			out.append(chr(ord(c) + 32))
		elif "А" <= c <= "Я":
			out.append(chr(ord(c) + 32))
		elif c == "Ё":
			out.append("ё")
		else:
			out.append(c)
	return "".join(out)


def uc(c: str) -> str:
	if "a" <= c <= "z":
		return chr(ord(c) - 32)
	if "а" <= c <= "я":
		return chr(ord(c) - 32)
	if c == "ё":
		return "Ё"
	return c


def to_ye(s: str) -> str:
	return "".join("е" if c == "ё" else "Е" if c == "Ё" else c for c in s)


def is_yo_in(s: str) -> bool:
	return any(is_yo(c) for c in s)


def is_ye_yo_in(s: str) -> bool:
	return any(is_ye_yo(c) for c in s)


def is_ye_yo_if(s: str, check_yo: bool) -> bool:
	for c in s:
		if is_ye(c):
			return True
		if check_yo and is_yo(c):
			return True
	return False


def copy_case(w: str, w_case: str) -> str:
	out = list(w)
	for i in range(min(len(w), len(w_case))):
		if is_upper(w_case[i]):
			out[i] = uc(out[i])
	return "".join(out)


def rest_lower(s: str) -> str:
	return s[:1] + lc(s[1:]) if s else ""


def cmp_words(a: str, b: str) -> int:
	"""Cmp: LC-сравнение, затем байтовый тай-брейк по исходным строкам."""
	la, lb = lc(a), lc(b)
	if la < lb:
		return -1
	if la > lb:
		return 1
	for i in range(len(a)):
		ca = a[i] if i < len(a) else "\0"
		cb = b[i] if i < len(b) else "\0"
		if ca != cb:
			return 1 if ca > cb else -1
	return 0


def find_in_strings(items: list[str], s: str) -> int:
	"""FindInStrings: последовательность проб точная, как в оригинале."""
	n = len(items)
	if n == 0:
		return -1
	i1, i2 = 0, n - 1
	if cmp_words(items[i1], s) == 0:
		return i1
	if cmp_words(items[i2], s) == 0:
		return i2
	while i2 - i1 > 1:
		i = (i1 + i2) // 2
		c = cmp_words(items[i], s)
		if c == 0:
			return i
		if c > 0:
			i2 = i
		else:
			i1 = i
	return -1


@dataclass
class Dictionary:
	ye: list[str] = field(default_factory=list)
	yo: list[str] = field(default_factory=list)
	ask: list[int] = field(default_factory=list)
	user_sel: list[int] = field(default_factory=list)
	words_answer: list[int] = field(default_factory=list)
	version: str = ""

	def find(self, word: str) -> int:
		idx = find_in_strings(self.ye, rest_lower(word))
		if idx < 0:
			idx = find_in_strings(self.ye, lc(word))
		return idx


def load_dictionary(path: Path) -> Dictionary:
	d = Dictionary()
	for raw in path.read_bytes().decode("cp1251").splitlines():
		line = raw.rstrip("\r")
		if line == "" or line.startswith("#"):
			if not d.version and "Версия:" in line:
				d.version = line.split("Версия:", 1)[1].strip()
			continue
		to_ask = 0
		if line.endswith("?"):
			line = line[:-1]
			to_ask = 1
		d.yo.append(line)
		d.ye.append(to_ye(line))
		d.ask.append(to_ask)
		d.user_sel.append(0)
		d.words_answer.append(0)
	return d


@dataclass
class Rule:
	raw: str
	valid: str
	ignore_case: bool
	lead_boundary: bool
	trail_boundary: bool
	ye_yo: list[int]
	regex: re.Pattern[str] | None
	was: int = 0


def make_valid_regexp(s: str) -> str:
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
		ss += r"\b"
	return ss


WORD_CLASS = "0-9A-Za-zА-Яа-яЁё"
SPACE_CLASS = " \\t\\n\\r\\f\\xa0"


def translate(valid: str) -> tuple[str, bool, bool, list[str]]:
	warnings: list[str] = []
	lead = trail = False
	if valid.startswith(r"\b"):
		lead = True
		valid = valid[2:]
	if valid.endswith(r"\b"):
		trail = True
		valid = valid[:-2]
	out: list[str] = []
	i = 0
	in_class = False
	while i < len(valid):
		ch = valid[i]
		if ch == "\\" and i + 1 < len(valid):
			esc = valid[i + 1]
			i += 2
			if esc == "w":
				out.append(WORD_CLASS if in_class else f"[{WORD_CLASS}]")
			elif esc == "W":
				warnings.append("\\W")
				out.append("[^" + WORD_CLASS + "]")
			elif esc == "s":
				out.append(SPACE_CLASS if in_class else f"[{SPACE_CLASS}]")
			elif esc == "S":
				warnings.append("\\S")
				out.append("[^" + SPACE_CLASS + "]")
			elif esc == "d":
				out.append("0-9" if in_class else "[0-9]")
			elif esc == "D":
				warnings.append("\\D")
				out.append("[^0-9]")
			elif esc in "bB":
				warnings.append("\\b")
				out.append("\\" + esc)
			else:
				out.append("\\" + esc)
			continue
		if ch == "[":
			in_class = True
		elif ch == "]":
			in_class = False
		out.append(ch)
		i += 1
	return "".join(out), lead, trail, warnings


def load_rules(path: Path) -> tuple[list[Rule], str]:
	rules: list[Rule] = []
	version = ""
	for raw in path.read_bytes().decode("cp1251").splitlines():
		line = raw.rstrip("\r")
		if line == "" or line.startswith("#"):
			if not version and "Версия:" in line:
				version = line.split("Версия:", 1)[1].strip()
			continue
		if "\\0" in line:
			continue
		valid = make_valid_regexp(line)
		pattern, lead, trail, _ = translate(valid)
		flags = re.MULTILINE | re.DOTALL
		if not any(is_upper(c) for c in line):
			flags |= re.IGNORECASE
		rx = re.compile(pattern, flags)
		rules.append(Rule(line, valid, not any(is_upper(c) for c in line), lead, trail,
						  [i for i, c in enumerate(line) if is_ye_yo(c)], rx))
	return rules, version


@dataclass
class Options:
	check_yo: bool = True
	no_var_only: bool = False
	var_only: bool = False
	always_ask: bool = False
	propose_last: bool = True
	regexs: bool = True
	fb_format: bool = False
	to_confirm_abbr: bool = True
	to_confirm_ellipsis: bool = True
	to_confirm_cap: bool = False
	words_buffer_length: int = 5


@dataclass
class Stats:
	checked_a: int = 0
	checked_changed: int = 0
	checked_not_changed: int = 0
	asked_a: int = 0
	asked_changed: int = 0
	asked_not_changed: int = 0
	re_a: int = 0
	re_changed: int = 0
	re_not_changed: int = 0
	no_re: int = 0
	wrong_yo: int = 0
	ambiguous: int = 0

	def as_dict(self) -> dict[str, int]:
		return dict(
			checked_a=self.checked_a, checked_changed=self.checked_changed,
			checked_not_changed=self.checked_not_changed, asked_a=self.asked_a,
			asked_changed=self.asked_changed, asked_not_changed=self.asked_not_changed,
			re_a=self.re_a, re_changed=self.re_changed, re_not_changed=self.re_not_changed,
			no_re=self.no_re, wrong_yo=self.wrong_yo, ambiguous=self.ambiguous)


class Yoficator:
	def __init__(self, dic: Dictionary, rules: list[Rule], opts: Options, policy: str):
		self.dic = dic
		self.rules = rules
		self.opts = opts
		self.policy = policy
		self.stats = Stats()
		self.lines: list[str] = []
		self.was_ask = False
		self.to_make_undo = False
		self.to_check_abbr = False
		self.to_check_ellipsis = False
		self.cancelled = False
		self.lines_changed: dict[int, list[tuple[int, str, str, bool]]] = {}

	# --- контекст ---------------------------------------------------------
	def get_context(self, line_no: int, pos: int, word: str) -> tuple[str, int]:
		back = 2
		fwd = 2
		first = max(0, line_no - back)
		for ln in range(first, line_no):
			self.lines[ln]  # доступ
		joined = ""
		word_start = 0
		for ln in range(first, min(len(self.lines), line_no + fwd + 1)):
			if ln != first:
				joined += "\r\n"
			if ln == line_no:
				word_start = len(joined) + min(pos, len(self.lines[ln]))
			joined += self.lines[ln]
		half = max(1, self.opts.words_buffer_length // 2)
		start = word_start
		cut = False
		for _ in range(half):
			while start > 0:
				start -= 1
				if is_alpha(joined[start]):
					break
			while start > 0:
				start -= 1
				if not is_alpha(joined[start]):
					cut = _ == half - 1
					break
		if cut and start < len(joined):
			start += 1
		end = min(word_start + len(word), len(joined))
		cut = False
		for _ in range(half):
			while end < len(joined):
				c = joined[end]
				end += 1
				if is_alpha(c):
					break
			while end < len(joined):
				c = joined[end]
				end += 1
				if not is_alpha(c):
					cut = _ == half - 1
					break
		if cut and end > start:
			end -= 1
		return joined[start:end], word_start - start

	def is_abbreviation(self, c: str, line_no: int, pos: int) -> bool:
		if c != ".":
			return False
		ln = line_no
		s = self.lines[ln]
		p = pos + 1
		while True:
			if p >= len(s):
				found = False
				while ln + 1 < len(self.lines):
					ln += 1
					if self.lines[ln]:
						s = self.lines[ln]
						found = True
						break
				if not found:
					return False
				p = 0
				if p >= len(s):
					return False
			ch = s[p]
			if is_lower(ch):
				return True
			if ch not in SPACE:
				return False
			p += 1

	def is_ellipsis(self, c: str, line_no: int, pos: int) -> bool:
		s = self.lines[line_no]
		tail = s[pos:pos + 3]
		return tail.startswith("...") or tail.startswith("…")

	# --- правила РВ -------------------------------------------------------
	def match_rule(self, rule: Rule, context: str, di: int, w1: str, w2: str) -> int:
		orig = context
		ctx = context
		for m in rule.regex.finditer(context):
			if rule.lead_boundary and m.start() > 0 and is_word_char(context[m.start() - 1]):
				continue
			if rule.trail_boundary and m.end() < len(context) and is_word_char(context[m.end()]):
				continue
			new_flag = False
			j = 0
			for g in range(1, (rule.regex.groups or 0) + 1):
				gs, ge = m.span(g)
				if gs < 0 or ge - gs != 1:
					continue
				if not is_ye_yo(ctx[gs]):
					continue
				if j < len(rule.ye_yo):
					ctx = ctx[:gs] + rule.raw[rule.ye_yo[j]] + ctx[gs + 1:]
				j += 1
				if di <= gs < di + len(w1):
					new_flag = True
			ctx = copy_case(ctx, orig)
			if not new_flag:
				continue
			nw = ctx[di:di + len(w1)]
			if nw == w1:
				return 1
			if nw == w2:
				return 2
		return 0

	def look_in_regexps(self, cword: str, line_no: int, pos: int, w1: str, w2: str) -> int:
		if not self.rules:
			self.stats.no_re += 1
			return 0
		context, di = self.get_context(line_no, pos, cword)
		for i, rule in enumerate(self.rules):
			n = self.match_rule(rule, context, di, w1, w2)
			if n != 0:
				rule.was += 1
				self.stats.re_a += 1
				if (n == 1 and cword == w2) or (n == 2 and cword == w1):
					self.stats.re_changed += 1
				else:
					self.stats.re_not_changed += 1
				return n
		self.stats.no_re += 1
		return 0

	# --- выбор слова ------------------------------------------------------
	def find_replacement(self, word_ye: str) -> tuple[str, bool, int, int, bool]:
		idx = self.dic.find(word_ye)
		if idx < 0:
			return "", False, -1, 1, False
		word2 = self.dic.yo[idx]
		always = self.dic.user_sel[idx] != 0
		if not always:
			to_ask = self.dic.ask[idx] != 0
			focus = self.dic.words_answer[idx] if to_ask else 2
		else:
			to_ask = False
			focus = self.dic.user_sel[idx]
		return word2, to_ask, idx, focus, always

	def select_word(self, cword: str, line_no: int, pos: int):
		"""-> (new_word, cancel, dic_index, focus, wrong_yo)"""
		self.to_make_undo = False
		self.was_ask = False
		focus = 0
		w1 = to_ye(cword)
		w2, to_ask, dic_index, focus, always = self.find_replacement(w1)
		to_ask = (to_ask or (w2 != "" and self.to_check_abbr) or
				  (w2 != "" and self.to_check_ellipsis) or
				  (w2 != "" and self.opts.to_confirm_cap and is_upper(cword[0]) and not always))
		is_yo = is_yo_in(cword)
		wrong_yo = is_yo and (lc(w2) != lc(cword))
		replaced_yo = wrong_yo and w2 != ""
		wrong_ye = (not is_yo and lc(w1) == lc(cword) and not to_ask and focus == 2)
		if replaced_yo:
			w1 = copy_case(w2, cword)
		if (w2 == "" and is_yo) or replaced_yo:
			w2 = cword
		w2 = copy_case(w2, cword)

		if not to_ask and self.opts.var_only and not wrong_yo:
			return "", False, dic_index, focus, wrong_yo

		if self.opts.regexs and to_ask:
			n = self.look_in_regexps(cword, line_no, pos, w1, w2)
			if n:
				to_ask = False
				focus = n

		if to_ask and self.opts.no_var_only:
			return "", False, dic_index, focus, wrong_yo

		if wrong_yo:
			if self.opts.no_var_only:
				return "", False, dic_index, focus, wrong_yo
			to_ask = True

		if not to_ask and self.opts.always_ask and w2 != "":
			to_ask = True

		skip = False
		if to_ask:
			self.stats.ambiguous += 1
			self.was_ask = True
			if self.policy == "ye":
				focus = 1
			elif self.policy == "yo":
				focus = 2
			else:
				skip = True
				focus = 0
			if dic_index >= 0 and focus > 0:
				self.dic.words_answer[dic_index] = focus

		if skip:
			return "", False, dic_index, focus, wrong_yo
		cancel = False
		if focus == 1:
			result = w1
		elif focus == 2:
			result = w2
		else:
			cancel = True
			return "", cancel, dic_index, focus, wrong_yo
		if result == cword:
			result = ""
		return result, cancel, dic_index, focus, wrong_yo

	# --- обработка строки -------------------------------------------------
	def process_line(self, line_no: int) -> None:
		text = self.lines[line_no]
		limit = len(text)
		if self.opts.fb_format:
			p = text.upper().find("<BINARY")
			if p >= 0:
				limit = p
		word = ""
		last_alpha = False
		changes: list[tuple[int, str, str, bool]] = []
		j = 0
		while j < limit:
			c = text[j]
			if is_alpha(c):
				word += c
				last_alpha = True
			else:
				if last_alpha:
					self.check_word_ex(word, j - len(word), j, c, line_no, changes)
					word = ""
				last_alpha = False
			j += 1
		if word:
			c = text[limit - 1] if limit > 0 else "\0"
			self.check_word_ex(word, limit - len(word), limit, c, line_no, changes)
		self.lines_changed.setdefault(line_no, []).extend(changes)

	def check_word_ex(self, word: str, start: int, term: int, cur: str, line_no: int,
					  changes: list) -> None:
		if not is_ye_yo_if(word, self.opts.check_yo):
			return
		self.to_check_abbr = self.opts.to_confirm_abbr and self.is_abbreviation(cur, line_no, term)
		self.to_check_ellipsis = (self.opts.to_confirm_ellipsis and
								  self.is_ellipsis(cur, line_no, term))
		self.check_word(word, start, line_no, changes)

	def check_word(self, word: str, start: int, line_no: int, changes: list) -> None:
		if not (len(word) > 1 and is_ye_yo_in(word)):
			return
		if is_yo_in(word) and not self.opts.check_yo:
			return
		new_word, cancel, dic_index, focus, wrong_yo = self.select_word(word, line_no, start)
		if cancel:
			self.cancelled = True
			return
		self.stats.checked_a += 1
		if new_word:
			self.stats.checked_changed += 1
		else:
			self.stats.checked_not_changed += 1
		if self.was_ask:
			self.stats.asked_a += 1
			if new_word:
				self.stats.asked_changed += 1
			else:
				self.stats.asked_not_changed += 1
		if wrong_yo:
			self.stats.wrong_yo += 1
		if new_word:
			old_chars = list(self.lines[line_no])
			for k in range(min(len(word), len(new_word))):
				if word[k] != new_word[k]:
					changes.append((start + k, word[k], new_word[k], self.was_ask))
					old_chars[start + k] = new_word[k]
			self.lines[line_no] = "".join(old_chars)

	def run(self) -> None:
		for ln in range(len(self.lines)):
			if self.cancelled:
				break
			self.process_line(ln)


def process_text(text: str, dic: Dictionary, rules: list[Rule], opts: Options,
				 policy: str) -> tuple[str, Stats, list[Rule]]:
	"""Сохраняет переводы строк и кодировку (вызывающий код работает с cp1251)."""
	lines = text.split("\n")
	# отделить завершающие \r
	eols = []
	clean = []
	for raw_line in lines:
		if raw_line.endswith("\r"):
			clean.append(raw_line[:-1])
			eols.append("\r")
		else:
			clean.append(raw_line)
			eols.append("")
	y = Yoficator(dic, rules, opts, policy)
	y.lines = clean
	y.run()
	out = []
	for i, line in enumerate(clean):
		if i == len(clean) - 1 and eols[i] == "" and not lines[i].endswith("\r"):
			out.append(line)
		else:
			out.append(line + eols[i] + "\n")
	return "".join(out), y.stats, rules


def main() -> int:
	ap = argparse.ArgumentParser()
	ap.add_argument("--dic", required=True)
	ap.add_argument("--regex", required=True)
	ap.add_argument("--policy", default="skip", choices=["skip", "ye", "yo"])
	ap.add_argument("--no-regex", action="store_true")
	ap.add_argument("--out-dir", required=True)
	ap.add_argument("--encoding", default="utf-8")
	ap.add_argument("files", nargs="+")
	args = ap.parse_args()

	dic = load_dictionary(Path(args.dic))
	rules, _ = load_rules(Path(args.regex))
	opts = Options(regexs=not args.no_regex)
	out_dir = Path(args.out_dir)
	out_dir.mkdir(parents=True, exist_ok=True)
	summary = {}
	for name in args.files:
		path = Path(name)
		raw = path.read_bytes()
		text = raw.decode(args.encoding)
		processed, stats, _ = process_text(text, dic, rules, opts, args.policy)
		target = out_dir / (path.stem + ".expected")
		target.write_bytes(processed.encode(args.encoding))
		summary[path.name] = stats.as_dict()
		print(f"{path.name}: проверено {stats.checked_a}, заменено {stats.checked_changed}, "
			  f"неоднозначных {stats.ambiguous}, РВ {stats.re_a}")
	(out_dir / "stats.json").write_text(json.dumps(summary, ensure_ascii=False, indent=1),
										encoding="utf-8")
	return 0


if __name__ == "__main__":
	sys.exit(main())
