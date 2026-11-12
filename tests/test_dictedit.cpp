// Редактор словарей: разбор/запись yox.dat и yo_regexpr.dat, правки,
// проверки и состояние редактора. Тесты не зависят от Windows.

#include "yo/dictedit.hpp"
#include "yo/cp1251.hpp"
#include "yo/regexdict.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>

using namespace yo;

namespace {

// Временный каталог на один тест: файлы удаляются в деструкторе.
class TempDir
{
public:
	TempDir()
	{
		dir_ = std::filesystem::temp_directory_path() /
			   ("yo-dictedit-" + std::to_string(++counter_));
		std::filesystem::create_directories(dir_);
	}
	~TempDir()
	{
		std::error_code ec;
		std::filesystem::remove_all(dir_, ec);
	}
	TempDir(const TempDir&) = delete;
	TempDir& operator=(const TempDir&) = delete;

	[[nodiscard]] std::string write(const std::string& name, const std::string& bytes) const
	{
		const std::filesystem::path p = dir_ / name;
		std::ofstream out(p, std::ios::binary | std::ios::trunc);
		out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
		return p.string();
	}
	[[nodiscard]] std::string read(const std::string& name) const
	{
		const std::filesystem::path p = dir_ / name;
		std::ifstream in(p, std::ios::binary);
		return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
	}
	[[nodiscard]] std::string path(const std::string& name) const
	{
		return (dir_ / name).string();
	}

private:
	std::filesystem::path dir_;
	static inline int counter_ = 0;
};

std::string cp(std::u32string_view text)
{
	return yo::cp1251::encode(text);
}

// Простой словарь: шапка + три слова, из них одно «?».
std::string sample_dict()
{
	std::string out;
	out += cp(U"# Версия: 9.99\r\n");
	out += cp(U"# Длина: 3\r\n");
	out += cp(U"ёж\r\n");
	out += cp(U"ёлка?\r\n");
	out += cp(U"тёмный\r\n");
	return out;
}

std::string sample_rules()
{
	std::string out;
	out += cp(U"# Версия: 1.09\r\n");
	out += cp(U"#всё ещё\r\n");
	out += cp(U"всё \\w+ло\r\n");
	return out;
}

} // namespace

TEST_CASE("словарь: проверка словоформы", "[dictedit]")
{
	CHECK(check_dict_word(U"ёлка").ok());
	CHECK(check_dict_word(U"Тёма").ok());
	CHECK(check_dict_word(U"").problem == WordProblem::empty);
	CHECK(check_dict_word(U"елка").problem == WordProblem::no_yo);
	CHECK(check_dict_word(U"abc").problem == WordProblem::bad_chars);
	CHECK(check_dict_word(U"ё-лка").problem == WordProblem::bad_chars);
	CHECK(check_dict_word(U"ёлка1").problem == WordProblem::bad_chars);
	// Сообщение непустое — оно показывается в строке статуса редактора.
	CHECK_FALSE(check_dict_word(U"елка").message.empty());
}

TEST_CASE("словарь: разбор и запись без потерь", "[dictedit]")
{
	const std::string text = sample_dict();
	const DictFile file = DictFile::parse(text);
	CHECK(file.version == "9.99");
	REQUIRE(file.words.size() == 3);
	CHECK(file.words[0].yo == U"ёж");
	CHECK_FALSE(file.words[0].ask);
	CHECK(file.words[1].yo == U"ёлка");
	CHECK(file.words[1].ask);
	CHECK(file.words[2].ye() == U"темный");
	// Обратная запись совпадает с исходником байт в байт (CP1251, CRLF).
	CHECK(file.format() == text);
}

TEST_CASE("словарь: проверка порядка CmpYeEx", "[dictedit]")
{
	DictFile ok = DictFile::parse(sample_dict());
	CHECK(check_dict_order(ok).sorted);

	DictFile bad = DictFile::parse(cp(U"ёлка\r\nёж\r\n"));
	REQUIRE_FALSE(check_dict_order(bad).sorted);
	CHECK(check_dict_order(bad).index == 1);
	CHECK(check_dict_order(bad).word == "ёж");

	// Порядок сравнивает формы с «е»: «ёлка» и «елка» — одно и то же слово.
	DictFile same = DictFile::parse(cp(U"елка\r\nёлка\r\n"));
	CHECK(check_dict_order(same).sorted);
	CHECK(dict_find_word(same, U"ёлка") == 0);
}

TEST_CASE("словарь: вставка, замена и удаление сохраняют порядок", "[dictedit]")
{
	DictFile file = DictFile::parse(sample_dict());

	const DictEdit added = dict_insert(file, U"ёжик", false);
	REQUIRE(added.ok);
	CHECK(added.index == 1); // между «ёж» и «ёлка»
	CHECK(file.words[1].yo == U"ёжик");
	CHECK(check_dict_order(file).sorted);

	// Дубликат (та же форма с «е» и тот же регистр) — отказ.
	const std::size_t was = file.words.size();
	CHECK_FALSE(dict_insert(file, U"ёжик", true).ok);
	CHECK(file.words.size() == was);
	// Другая форма: «Ёжик» с заглавной — отдельная строка, как «Все»/«все»
	// в словаре оригинала (Cmp различает регистр при равных LC-ключах).
	REQUIRE(dict_insert(file, U"Ёжик", false).ok);
	REQUIRE(dict_erase(file, dict_find_word(file, U"Ёжик")));

	// Неверное слово — отказ с причиной.
	const DictEdit bad = dict_insert(file, U"елка", false);
	CHECK_FALSE(bad.ok);
	CHECK(bad.message.find("ё") != std::string::npos);

	// Замена: «ёж» → «ёжовый» встаёт на своё место по порядку (после «ёжик»).
	const DictEdit moved = dict_replace(file, 0, U"ёжовый", true);
	REQUIRE(moved.ok);
	CHECK(moved.index == 1);
	CHECK(file.words[moved.index].yo == U"ёжовый");
	CHECK(file.words[moved.index].ask);
	CHECK(check_dict_order(file).sorted);

	// Удаление.
	REQUIRE(dict_erase(file, moved.index));
	CHECK(dict_find_word(file, U"ёжовый") == std::string::npos);
	CHECK(dict_erase(file, 999) == false);
}

TEST_CASE("словарь: фильтр таблицы (е/ё и регистр)", "[dictedit]")
{
	const DictFile file = DictFile::parse(sample_dict());
	CHECK(dict_filter(file, { U"", false }).size() == 3);
	CHECK(dict_filter(file, { U"ЕЛК", false }).size() == 1); // «ёлка» находится по «ел»
	CHECK(dict_filter(file, { U"ёж", false }).size() == 1);	 // только «ёж»
	CHECK(dict_filter(file, { U"", true }).size() == 1);	 // только «?»
	CHECK(dict_filter(file, { U"нет", false }).empty());
}

TEST_CASE("правила РВ: разбор и запись без потерь", "[dictedit]")
{
	const std::string text = sample_rules();
	const RegexFile file = RegexFile::parse(text);
	CHECK(file.version == "1.09");
	REQUIRE(file.lines.size() == 3);
	CHECK_FALSE(file.lines[0].rule); // комментарий
	CHECK_FALSE(file.lines[1].rule); // закомментированное правило
	CHECK(file.lines[2].rule);
	CHECK(file.rule_count() == 1);
	CHECK(file.rule_lines() == std::vector<std::size_t>{ 2 });
	CHECK(file.format() == text);
}

TEST_CASE("правила РВ: проверка компиляции", "[dictedit]")
{
	const RuleCheck good = check_regex_rule(U"всё \\w+ло");
	CHECK(good.ok);
	CHECK_FALSE(good.pattern.empty());
	CHECK(good.valid.find("([") != std::string::npos); // «ё» стала группой ([её])

	const RuleCheck bad = check_regex_rule(U"(");
	CHECK_FALSE(bad.ok);
	CHECK_FALSE(bad.error.empty());
	// «всё» без «е» — всё равно правило, проверка не требует «ё».
	CHECK(check_regex_rule(U"темным-темно").ok);
}

TEST_CASE("правила РВ: тестовое поле (контекст → ответ)", "[dictedit]")
{
	RegexFile file;
	file.lines.push_back(RegexLine{ U"всё", true });

	// Слово с «е» в контексте — правило подходит к форме с «е» (word_n = 1).
	const RegexProbe one = probe_regex(file, U"все равно", 0, U"все");
	REQUIRE(one.ok);
	CHECK(one.matched);
	CHECK(one.word_n == 2); // 2 — правило советует форму с «ё»
	CHECK(one.rule_no == 0);
	CHECK(one.line == 0);
	CHECK(one.word_ye == "все");

	// Слово с «ё» — форма с «ё» (word_n = 2).
	const RegexProbe two = probe_regex(file, U"всё равно", 0, U"всё");
	REQUIRE(two.ok);
	CHECK(two.matched);
	CHECK(two.word_n == 2);
	CHECK(two.word_yo == "всё");

	// Правило не подходит — matched = false.
	const RegexProbe none = probe_regex(file, U"лес шумит", 0, U"лес");
	REQUIRE(none.ok);
	CHECK_FALSE(none.matched);

	// Некомпилируемое правило в словаре — ошибка, а не падение.
	RegexFile broken;
	broken.lines.push_back(RegexLine{ U"(", true });
	const RegexProbe failed = probe_regex(broken, U"лес", 0, U"лес");
	CHECK_FALSE(failed.ok);
	CHECK_FALSE(failed.error.empty());
}

TEST_CASE("состояние: загрузка, выбор, правка и сохранение", "[dictedit]")
{
	TempDir dir;
	const std::string dict_path = dir.write("yox.dat", sample_dict());
	const std::string rules_path = dir.write("yo_regexpr.dat", sample_rules());

	DictEditorState state;
	dict_editor_load(state, dict_path, rules_path);
	REQUIRE(state.words.size() == 3);
	CHECK(state.rules.rule_count() == 1);
	CHECK(state.visible.size() == 3);
	CHECK_FALSE(state.dirty);
	CHECK(state.message.find("3") != std::string::npos);

	// Выбор строки кладёт слово в поле ввода.
	dict_editor_move(state, 1);
	CHECK(state.cursor == 1);
	dict_editor_pick(state);
	CHECK(state.edit == U"ёлка");
	CHECK(state.edit_ask);
	CHECK(state.field == DictField::text);

	// Правка: «ёлка?» → «ёлочка» (без «?»).
	state.edit = U"ёлочка";
	state.edit_ask = false;
	REQUIRE(dict_editor_apply(state));
	CHECK(state.dirty);
	CHECK(dict_find_word(state.words, U"ёлочка") != std::string::npos);
	CHECK(dict_find_word(state.words, U"ёлка") == std::string::npos);

	// Добавление нового слова — отдельная команда («Добавить»).
	state.edit = U"ёжик";
	REQUIRE(dict_editor_add(state));
	CHECK(state.words.size() == 4);
	CHECK(state.words.words[state.visible[state.cursor]].yo == U"ёжик");

	// «Изменить» без выбора строки — понятная ошибка.
	DictEditorState empty_list;
	empty_list.edit = U"ёж";
	CHECK_FALSE(dict_editor_apply(empty_list));
	CHECK(empty_list.message_error);

	// Неверное слово — сообщение об ошибке, состояние не меняется.
	state.edit = U"елка";
	CHECK_FALSE(dict_editor_add(state));
	CHECK(state.message_error);
	CHECK(state.words.size() == 4);

	// Удаление выбранной строки.
	const std::size_t before = state.words.size();
	state.cursor = 0;
	REQUIRE(dict_editor_erase(state));
	CHECK(state.words.size() == before - 1);

	// Сохранение: файл перечитывается и совпадает с состоянием.
	REQUIRE(dict_editor_save(state));
	CHECK_FALSE(state.dirty);
	const DictFile reread = DictFile::parse(dir.read("yox.dat"));
	CHECK(reread.size() == state.words.size());
	CHECK(reread.version == "9.99"); // шапка сохранена
	CHECK(check_dict_order(reread).sorted);
}

TEST_CASE("состояние: фильтр по подстроке и только «?»", "[dictedit]")
{
	TempDir dir;
	const std::string dict_path = dir.write("yox.dat", sample_dict());
	DictEditorState state;
	dict_editor_load(state, dict_path, dir.path("none.dat"));

	state.filter.query = U"елк";
	dict_editor_refilter(state);
	REQUIRE(state.visible.size() == 1);
	CHECK(dict_row_text(state, 0).find("?") != std::string::npos);
	CHECK(dict_row_text(state, 0).find("елка") != std::string::npos);

	state.filter = { U"", true };
	dict_editor_refilter(state);
	CHECK(state.visible.size() == 1);

	// Курсор подрезается под новый список.
	state.cursor = 10;
	dict_editor_refilter(state);
	CHECK(state.cursor == 0);
}

TEST_CASE("состояние: правила РВ — добавление, ошибка компиляции, сохранение", "[dictedit]")
{
	TempDir dir;
	const std::string dict_path = dir.write("yox.dat", sample_dict());
	const std::string rules_path = dir.write("yo_regexpr.dat", sample_rules());

	DictEditorState state;
	dict_editor_load(state, dict_path, rules_path);
	state.mode = DictMode::rules;
	dict_editor_refilter(state);
	REQUIRE(state.visible.size() == 1);

	// Некомпилируемое правило не сохраняется в списке.
	state.edit = U"(";
	CHECK_FALSE(dict_editor_apply(state));
	CHECK(state.message_error);
	CHECK(state.rules.rule_count() == 1);

	// Хорошее правило дописывается в конец файла командой «Добавить».
	state.cursor = 1; // за пределами списка — «новое»
	state.edit = U"\\d+ лет";
	REQUIRE(dict_editor_add(state));
	CHECK(state.rules.rule_count() == 2);
	CHECK(state.dirty);

	// Тестовое поле есть только у словаря РВ.
	DictEditorState words_only;
	const RegexProbe wrong_mode = dict_editor_probe(words_only, U"пять лет", U"лет");
	CHECK(wrong_mode.error.find("тестовое поле") != std::string::npos);
	// Тестовое поле работает на правленом словаре.
	const RegexProbe probe2 = dict_editor_probe(state, U"20 лет назад", U"лет");
	CHECK(probe2.ok);
	CHECK(probe2.matched);

	REQUIRE(dict_editor_save(state));
	const RegexFile reread = RegexFile::parse(dir.read("yo_regexpr.dat"));
	CHECK(reread.rule_count() == 2);
	CHECK(reread.version == "1.09");
	CHECK(reread.lines[0].text == U"# Версия: 1.09");
}

TEST_CASE("словарь РВ: parse по тексту совпадает с загрузкой файла", "[dictedit]")
{
	TempDir dir;
	const std::string rules_path = dir.write("yo_regexpr.dat", sample_rules());
	auto from_file = RegexDictionary::load(rules_path);
	auto from_text = RegexDictionary::parse(sample_rules());
	REQUIRE(from_file.has_value());
	REQUIRE(from_text.has_value());
	CHECK(from_file->size() == from_text->size());
	CHECK(from_file->version() == from_text->version());
}

TEST_CASE("состояние: строка правила с ошибкой помечается", "[dictedit]")
{
	TempDir dir;
	DictEditorState state;
	dict_editor_load(state, dir.write("yox.dat", sample_dict()), dir.path("none.dat"));
	state.mode = DictMode::rules;
	state.rules.lines.push_back(RegexLine{ U"всё \\w+ло", true });
	state.rules.lines.push_back(RegexLine{ U"(", true });
	dict_editor_refilter(state);
	REQUIRE(state.visible.size() == 2);
	CHECK_FALSE(dict_row_error(state, 0));
	CHECK(dict_row_error(state, 1));
	CHECK(dict_row_error(state, 99) == false);
	// В тексте строки видно причину — редактор показывает её рядом.
	CHECK(dict_row_text(state, 1).find("ошибка") != std::string::npos);
	// Словарь: ошибок компиляции не бывает.
	state.mode = DictMode::words;
	dict_editor_refilter(state);
	CHECK_FALSE(dict_row_error(state, 0));
}
