// Бенчмарки ядра: загрузка словарей, поиск словоформы, ёфикация 1/10 МБ.
//
// Целевые показатели ТЗ: ёфикация >= 30 МБ/с на ядро на типичной русской прозе.

#include "yo/codec.hpp"
#include "yo/cp1251.hpp"
#include "yo/dict.hpp"
#include "yo/editor.hpp"
#include "yo/mapped.hpp"
#include "yo/regexdict.hpp"
#include "yo/textbuf.hpp"
#include "yo/utf.hpp"
#include "yo/yofy.hpp"

#include <benchmark/benchmark.h>

#include <filesystem>
#include <memory>
#include <sstream>
#include <fstream>
#include <string>
#include <vector>

using namespace yo;

namespace {

const auto kDataDir = std::filesystem::path(YO_BENCH_DATA_DIR);

const Dictionary& dict()
{
	static Dictionary d = []
	{
		std::vector<LoadIssue> issues;
		auto loaded = Dictionary::load(kDataDir / "yox.dat", &issues);
		return std::move(*loaded);
	}();
	return d;
}

RegexDictionary& regex_dic()
{
	static RegexDictionary rd = []
	{
		std::vector<LoadIssue> issues;
		auto loaded = RegexDictionary::load(kDataDir / "yo_regexpr.dat", &issues);
		return std::move(*loaded);
	}();
	return rd;
}

UString u(std::string_view utf8)
{
	return decode_line({ reinterpret_cast<const std::uint8_t*>(utf8.data()), utf8.size() },
					   Encoding::utf8);
}

const std::vector<UString>& prose_lines()
{
	static const std::vector<UString> lines = []
	{
		std::vector<UString> v;
		v.push_back(u("Все это время мы шли по лесу и пели песни, и никто не знал, что будет дальше."));
		v.push_back(u("Он еще не знал, что она принесет ему письмо, и молча смотрел в окно."));
		v.push_back(u("Черный кот сидел на желтой крыше и ждал, пока пчелы улетят с цветов."));
		v.push_back(u("Мы берем с собой елки и щелкаем орехи, а потом идем домой по берегу реки."));
		v.push_back(u("Пчелы летали над озером, а мы смотрели на них и думали о чем-то своем."));
		v.push_back(u("Тысяча лет прошла с тех пор, но никто не забыл тот день и тот разговор."));
		return v;
	}();
	return lines;
}

// Строки с текстом объёмом ~символы/80.
struct Doc : LineSource
{
	std::vector<UString> lines;
	std::size_t line_count() const override { return lines.size(); }
	const UString* line(std::size_t i) const override
	{
		return i < lines.size() ? &lines[i] : nullptr;
	}
};

Doc make_doc(std::size_t target_bytes)
{
	Doc doc;
	std::size_t bytes = 0;
	std::size_t i = 0;
	while (bytes < target_bytes)
	{
		doc.lines.push_back(prose_lines()[i % prose_lines().size()]);
		bytes += cp1251::encode(doc.lines.back()).size() + 2;
		++i;
	}
	return doc;
}

void bench_yoficate(benchmark::State& state, std::size_t target_bytes)
{
	Doc doc = make_doc(target_bytes);
	Options opts;
	for ([[maybe_unused]] auto _ : state)
	{
		// Копия строк: ёфикация меняет текст, повторяем на исходном.
		Doc work;
		work.lines = doc.lines;	// копия: ёфикация меняет текст
		Yoficator yofy(const_cast<Dictionary&>(dict()), &regex_dic(), opts);
		yofy.set_ask([](const AskRequest&) { return AskAnswer::skip; });
		for (std::size_t i = 0; i < work.lines.size(); ++i)
			yofy.process_line(work.lines[i], i, work, nullptr);
		state.SetBytesProcessed(static_cast<std::int64_t>(target_bytes) *
								static_cast<std::int64_t>(state.iterations()));
	}
	state.SetLabel(std::to_string(target_bytes / (1024 * 1024)) + " МБ");
}

void bench_yoficate_no_regex(benchmark::State& state, std::size_t target_bytes)
{
	Doc doc = make_doc(target_bytes);
	Options opts;
	opts.regexs = false;
	for ([[maybe_unused]] auto _ : state)
	{
		Doc work;
		work.lines = doc.lines;
		Yoficator yofy(const_cast<Dictionary&>(dict()), &regex_dic(), opts);
		yofy.set_ask([](const AskRequest&) { return AskAnswer::skip; });
		for (std::size_t i = 0; i < work.lines.size(); ++i)
			yofy.process_line(work.lines[i], i, work, nullptr);
		state.SetBytesProcessed(static_cast<std::int64_t>(target_bytes) *
								static_cast<std::int64_t>(state.iterations()));
	}
	state.SetLabel(std::to_string(target_bytes / (1024 * 1024)) + " МБ, без РВ");
}

void bench_lookup(benchmark::State& state)
{
	const auto& d = dict();
	const std::vector<std::string> keys = { "все", "шел", "принес", "еще", "черный",
											"пчелы", "берет", "ждет" };
	std::size_t i = 0;
	for ([[maybe_unused]] auto _ : state)
	{
		benchmark::DoNotOptimize(d.find_key(cp1251::word_key(u(keys[i % keys.size()]))));
		++i;
	}
}

// Только разбор на слова (без словаря): стоимость токенизации.
void bench_tokenize(benchmark::State& state, std::size_t target_bytes)
{
	Doc doc = make_doc(target_bytes);
	std::size_t words = 0;
	for ([[maybe_unused]] auto _ : state)
	{
		for (const auto& line : doc.lines)
		{
			UString word;
			for (const char32_t c : line)
			{
				if (cyr::is_alpha(c))
					word.push_back(c);
				else if (!word.empty())
				{
					++words;
					word.clear();
				}
			}
		}
	}
	benchmark::DoNotOptimize(words);
	state.SetLabel(std::to_string(target_bytes / (1024 * 1024)) + " МБ");
}

// Кодек CLI: чтение CP1251-строк и запись обратно (без ёфикации).
void bench_codec(benchmark::State& state, std::size_t target_bytes)
{
	// Исходник в CP1251: строки по ~80 символов.
	std::string bytes;
	while (bytes.size() < target_bytes)
		bytes += cp1251::encode(prose_lines()[bytes.size() % 1024 % prose_lines().size()]);
	bytes.resize(target_bytes);
	std::string out;

	for ([[maybe_unused]] auto _ : state)
	{
		std::string src = bytes; // свежий поток на каждую итерацию
		auto stream = std::make_unique<std::istringstream>(src);
		LineReader reader(std::move(stream), Encoding::cp1251);
		std::ostringstream sink;
		LineWriter writer(sink, Encoding::cp1251, false);
		UString line;
		std::string eol;
		std::size_t n = 0;
		while (reader.next(line, eol))
		{
			writer.write_line(line, eol);
			++n;
		}
		out = sink.str();
		benchmark::DoNotOptimize(out);
		benchmark::DoNotOptimize(n);
		state.SetBytesProcessed(static_cast<std::int64_t>(target_bytes) *
								static_cast<std::int64_t>(state.iterations()));
	}
	state.SetLabel(std::to_string(target_bytes / (1024 * 1024)) + " МБ");
}

// Чистый поиск словоформы: ключи собраны заранее, без кодирования в цикле.
void bench_find_pure(benchmark::State& state)
{
	const auto& d = dict();
	const std::vector<std::string> keys = { "все", "шел", "принес", "еще", "черный",
											"пчелы", "берет", "ждет" };
	std::vector<std::string> ck;
	ck.reserve(keys.size());
	for (const auto& k : keys)
		ck.push_back(cp1251::word_key(u(k)));
	std::size_t i = 0;
	for ([[maybe_unused]] auto _ : state)
	{
		benchmark::DoNotOptimize(d.find_key(ck[i % ck.size()]));
		++i;
	}
}

// Полный путь решения по слову: кодирование + поиск (как в движке).
void bench_find_word(benchmark::State& state)
{
	const auto& d = dict();
	std::vector<UString> words;
	for (const auto k : { "все", "шел", "принес", "еще", "черный", "пчелы", "берет", "ждет" })
		words.push_back(u(k));
	std::size_t i = 0;
	for ([[maybe_unused]] auto _ : state)
	{
		benchmark::DoNotOptimize(d.find(words[i % words.size()]));
		++i;
	}
}

void bench_regex_match(benchmark::State& state)
{
	const UString ctx = u("Он все равно придет домой, и мы пойдем гулять.");
	const UString w1 = u("все");
	const UString w2 = u("всё");
	for ([[maybe_unused]] auto _ : state)
	{
		int word_n = 0;
		std::size_t rule = 0;
		benchmark::DoNotOptimize(regex_dic().lookup(ctx, 3, w1, w2, word_n, rule));
	}
}

void bench_dict_load(benchmark::State& state)
{
	for ([[maybe_unused]] auto _ : state)
	{
		auto d = Dictionary::load(kDataDir / "yox.dat", nullptr);
		benchmark::DoNotOptimize(d);
	}
}


// ---- Документное ядро GUI: mmap + piece table ----

std::string make_text(std::size_t target_bytes)
{
	std::string s;
	s.reserve(target_bytes + 64);
	for (std::size_t i = 0; s.size() < target_bytes; ++i)
	{
		s += "Строка номер ";
		s += std::to_string(i);
		s += (i % 7 == 0) ? "\r\n" : "\n";
	}
	return s;
}

std::shared_ptr<yo::TextBuffer> textbuf_of(std::size_t target_bytes)
{
	static std::shared_ptr<yo::TextBuffer> buf = [target_bytes]
	{
		auto src = std::make_shared<MemorySource>(make_text(target_bytes));
		return std::make_shared<TextBuffer>(src);
	}();
	return buf;
}

// Открытие текста: построение индекса начал строк по неизменяемому источнику.
void bench_textbuf_load(benchmark::State& state, std::size_t target_bytes)
{
	const std::string text = make_text(target_bytes);
	for ([[maybe_unused]] auto _ : state)
	{
		auto src = std::make_shared<MemorySource>(text);
		TextBuffer buf(src);
		benchmark::DoNotOptimize(buf.line_count());
		state.SetBytesProcessed(state.iterations() * static_cast<std::int64_t>(text.size()));
		state.counters["lines"] = static_cast<double>(buf.line_count());
	}
}

// Произвольный доступ к строке: поиск по индексу + сборка текста строки.
void bench_textbuf_line(benchmark::State& state)
{
	const auto buf = textbuf_of(std::size_t{ 4 } << 20);
	const std::size_t lines = buf->line_count();
	std::size_t i = 0;
	for ([[maybe_unused]] auto _ : state)
	{
		const std::size_t line = (i++ * 7919) % lines;
		const std::string_view v = buf->line_text(line);
		benchmark::DoNotOptimize(v.size());
		state.SetBytesProcessed(state.iterations());
	}
}

// Смещение -> номер строки (двоичный поиск по starts_).
void bench_textbuf_line_of(benchmark::State& state)
{
	const auto buf = textbuf_of(std::size_t{ 4 } << 20);
	const std::size_t total = buf->size();
	std::size_t i = 0;
	for ([[maybe_unused]] auto _ : state)
	{
		const std::size_t off = (i++ * 65521) % total;
		benchmark::DoNotOptimize(buf->line_of(off));
	}
}

// Правки: 1000 вставок в случайные места мегабайтного текста, затем отмена
// и повтор всех правок.
void bench_textbuf_edit(benchmark::State& state)
{
	const auto base = textbuf_of(std::size_t{ 1 } << 20);
	const std::string base_text = base->text();
	for ([[maybe_unused]] auto _ : state)
	{
		TextBuffer buf(std::make_shared<MemorySource>(base_text));
		for (std::size_t i = 0; i < 1000; ++i)
			buf.insert((i * 65537) % buf.size(), "слово ");
		benchmark::DoNotOptimize(buf.line_count());
		state.SetItemsProcessed(state.iterations() * 1000);
	}
}

void bench_textbuf_undo(benchmark::State& state)
{
	const std::string text = make_text(std::size_t{ 1 } << 20);
	TextBuffer buf(std::make_shared<MemorySource>(text));
	for (std::size_t i = 0; i < 1000; ++i)
		buf.insert((i * 65537) % buf.size(), "x");
	for ([[maybe_unused]] auto _ : state)
	{
		while (buf.can_undo())
			buf.undo();
		while (buf.can_redo())
			buf.redo();
		benchmark::DoNotOptimize(buf.line_count());
		state.SetItemsProcessed(state.iterations() * 2000);
	}
}

// Записать файл нужного размера, не держа его целиком в памяти.
void ensure_text_file(const std::filesystem::path& path, std::size_t target_bytes)
{
	if (std::filesystem::exists(path) && std::filesystem::file_size(path) == target_bytes)
		return;
	std::ofstream out(path, std::ios::binary | std::ios::trunc);
	std::size_t written = 0;
	std::size_t line = 0;
	while (written < target_bytes)
	{
		std::string block;
		block.reserve(1U << 20);
		while (block.size() < (1U << 20))
		{
			block += "Строка номер ";
			block += std::to_string(line++);
			block += (line % 7 == 0) ? "\r\n" : "\n";
		}
		const std::size_t take = std::min(block.size(), target_bytes - written);
		out.write(block.data(), static_cast<std::streamsize>(take));
		written += take;
	}
}

// Пара правок (вставка + отмена) в 20 МБ тексте: цена сдвига индекса строк.
// Документ строится один раз и возвращается в исходное состояние каждую
// итерацию, поэтому в замер попадают только сами правки.
void bench_textbuf_edit_at(benchmark::State& state, bool at_end)
{
	const std::string text = make_text(std::size_t{ 20 } << 20);
	auto buf = std::make_shared<TextBuffer>(std::make_shared<MemorySource>(text));
	const std::size_t at = at_end ? buf->size() : buf->size() / 2;
	for ([[maybe_unused]] auto _ : state)
	{
		buf->insert(at, "ё");
		buf->undo();
	}
	// В итерации две правки (вставка + отмена): время одной — половина.
	state.counters["lines"] = static_cast<double>(buf->line_count());
}

// Кадр отрисовки без Direct2D: сборка текста 50 видимых строк в UTF-16.
void bench_paint_frame(benchmark::State& state, std::size_t target_bytes)
{
	const auto buf = textbuf_of(target_bytes);
	auto ed = std::make_shared<Editor>(buf);
	Metrics m;
	ed->set_metrics(m);
	ed->set_viewport(1000.0F, 50.0F * m.line_height);
	const std::size_t lines = buf->line_count();
	std::size_t offset = 0;
	for ([[maybe_unused]] auto _ : state)
	{
		const std::size_t line = (offset++ * 7919) % std::max<std::size_t>(1, lines - 60);
		ed->scroll_by(0.0F, ed->line_top(line) - ed->scroll_y());
		const std::size_t first = ed->first_visible_line();
		const std::size_t count = ed->visible_line_count();
		std::size_t bytes = 0;
		for (std::size_t i = first; i < first + count && i < lines; ++i)
		{
			const std::string_view v = buf->line_text(i);
			const std::u16string wide = utf8_to_utf16(v);
			benchmark::DoNotOptimize(wide.size());
			bytes += v.size();
		}
		state.SetBytesProcessed(state.iterations() * static_cast<std::int64_t>(bytes));
	}
}

// mmap: открытие файла без копирования + индекс строк.
void bench_mapped_load(benchmark::State& state, std::size_t target_bytes)
{
	const auto path = std::filesystem::temp_directory_path() /
					  ("yo-bench-text-" + std::to_string(target_bytes) + ".txt");
	ensure_text_file(path, target_bytes);
	for ([[maybe_unused]] auto _ : state)
	{
		auto mapped = MappedFile::open(path.string());
		TextBuffer buf(mapped);
		benchmark::DoNotOptimize(buf.line_count());
		state.SetBytesProcessed(state.iterations() * static_cast<std::int64_t>(target_bytes));
		state.counters["lines"] = static_cast<double>(buf.line_count());
	}
	if (target_bytes <= (std::size_t{ 200 } << 20))
		std::filesystem::remove(path);
}

} // namespace

BENCHMARK(bench_dict_load)->Unit(benchmark::kMillisecond);
BENCHMARK(bench_lookup)->Unit(benchmark::kNanosecond);
BENCHMARK_CAPTURE(bench_tokenize, 1MB, std::size_t{ 1 } << 20)->Unit(benchmark::kMillisecond);
BENCHMARK_CAPTURE(bench_codec, 1MB, std::size_t{ 1 } << 20)->Unit(benchmark::kMillisecond);
BENCHMARK(bench_find_pure)->Unit(benchmark::kNanosecond);
BENCHMARK(bench_find_word)->Unit(benchmark::kNanosecond);
BENCHMARK(bench_regex_match)->Unit(benchmark::kMicrosecond);
BENCHMARK_CAPTURE(bench_yoficate, 1MB, std::size_t{ 1 } << 20)->Unit(benchmark::kMillisecond);
BENCHMARK_CAPTURE(bench_yoficate, 10MB, std::size_t{ 10 } << 20)->Unit(benchmark::kMillisecond);
	BENCHMARK_CAPTURE(bench_yoficate_no_regex, 1MB, std::size_t{ 1 } << 20)
		->Unit(benchmark::kMillisecond);
BENCHMARK_CAPTURE(bench_textbuf_load, 1MB, std::size_t{ 1 } << 20)->Unit(benchmark::kMillisecond);
BENCHMARK_CAPTURE(bench_textbuf_load, 10MB, std::size_t{ 10 } << 20)->Unit(benchmark::kMillisecond);
BENCHMARK_CAPTURE(bench_mapped_load, 10MB, std::size_t{ 10 } << 20)->Unit(benchmark::kMillisecond);
BENCHMARK_CAPTURE(bench_mapped_load, 100MB, std::size_t{ 100 } << 20)->Unit(benchmark::kMillisecond);
BENCHMARK_CAPTURE(bench_mapped_load, 1GB, std::size_t{ 1 } << 30)->Unit(benchmark::kMillisecond);
BENCHMARK_CAPTURE(bench_textbuf_edit_at, middle, false)->Unit(benchmark::kMicrosecond);
BENCHMARK_CAPTURE(bench_textbuf_edit_at, end, true)->Unit(benchmark::kMicrosecond);
BENCHMARK_CAPTURE(bench_paint_frame, 10MB, std::size_t{ 10 } << 20)->Unit(benchmark::kMicrosecond);
BENCHMARK(bench_textbuf_line)->Unit(benchmark::kNanosecond);
BENCHMARK(bench_textbuf_line_of)->Unit(benchmark::kNanosecond);
BENCHMARK(bench_textbuf_edit)->Unit(benchmark::kMillisecond);
BENCHMARK(bench_textbuf_undo)->Unit(benchmark::kMillisecond);

BENCHMARK_MAIN();
