// yo-cli — пакетная ёфикация без GUI.
//
// Семантика unix-фильтра: текст — в stdout, статистика и диагностика — в stderr.
// Коды возврата: 0 — успех, 1 — ошибка, 2 — остались неразрешённые
// неоднозначности (политика skip/fail), 3 — прервано пользователем.

#include "yo/codec.hpp"
#include "yo/cp1251.hpp"
#include "yo/dict.hpp"
#include "yo/regexdict.hpp"
#include "yo/yofy.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <print>
#include <string>
#include <string_view>
#include <vector>

#if defined(_WIN32)
#include <io.h>
#define YO_ISATTY(f) _isatty(_fileno(f))
#else
#include <unistd.h>
#define YO_ISATTY(f) isatty(fileno(f))
#endif

namespace fs = std::filesystem;
using namespace yo;

namespace {

constexpr int kExitOk = 0;
constexpr int kExitError = 1;
constexpr int kExitAmbiguous = 2;
constexpr int kExitInterrupted = 3;

enum class AmbiguousPolicy { skip, ye, yo, fail, ask };

struct CliOptions
{
	std::optional<fs::path> dic;
	std::optional<fs::path> regex;
	bool use_regex = true;
	std::optional<fs::path> regex_log;
	std::optional<fs::path> out;	  // -o (один файл)
	std::optional<fs::path> out_dir;  // --out-dir
	bool in_place = false;
	bool backup = false;
	std::optional<AmbiguousPolicy> policy;
	std::optional<Encoding> from;
	std::optional<Encoding> to;
	bool stats = false;
	bool json = false;
	bool quiet = false;
	std::vector<fs::path> inputs;
};

void usage(std::FILE* to)
{
	std::print(to,
			   "yo-cli {} — ёфикация текста (Ё лайт, GPL-3.0)\n"
			   "\n"
			   "Использование: yo-cli [параметры] <файл>...\n"
			   "\n"
			   "  --dic <путь>              словарь yox.dat\n"
			   "  --regex <путь>            словарь РВ yo_regexpr.dat\n"
			   "  --no-regex                не использовать словарь РВ\n"
			   "  --on-ambiguous=POLICY     skip|ye|yo|fail|ask (по умолчанию skip вне TTY)\n"
			   "  --in-place                перезаписать входные файлы (атомарно)\n"
			   "  --backup                  сохранить .bak перед перезаписью\n"
			   "  -o, --out <файл>          результат в файл (один вход)\n"
			   "  --out-dir <каталог>       результат в каталог\n"
			   "  --encoding=E              кодировка входа и выхода: auto|cp1251|utf-8|utf-16le|utf-16be\n"
			   "  --from=E, --to=E          кодировка входа/выхода по отдельности\n"
			   "  --regex-log <файл>        журнал срабатываний правил РВ\n"
			   "  --report=text|json        формат отчёта в stderr\n"
			   "  --stats                   печатать статистику\n"
			   "  -q, --quiet               без отчёта\n"
			   "  -h, --help, --version\n"
			   "\n"
			   "Коды возврата: 0 — успех, 1 — ошибка, 2 — есть неоднозначности,\n"
			   "3 — прервано.\n",
			   "0.1.0");
}

[[nodiscard]] std::optional<Encoding> parse_encoding(std::string_view s)
{
	if (s == "cp1251" || s == "ansi" || s == "1251")
		return Encoding::cp1251;
	if (s == "utf-8" || s == "utf8")
		return Encoding::utf8;
	if (s == "utf-16le" || s == "utf16le")
		return Encoding::utf16le;
	if (s == "utf-16be" || s == "utf16be")
		return Encoding::utf16be;
	return std::nullopt;
}

// Окно строк: держит предыдущие и читает вперёд, отдаёт процессору
// изменяемые строки (как Editor.Lines в оригинале).
class StreamSource : public LineSource
{
public:
	explicit StreamSource(LineReader& reader) : reader_(reader) { pump(); }

	void pump()
	{
		while (lines_.size() < lookahead_ + 1 && !eof_)
		{
			UString line;
			std::string eol;
			if (reader_.next(line, eol))
			{
				lines_.push_back(std::move(line));
				eols_.push_back(std::move(eol));
			}
			else
			{
				eof_ = true;
				break;
			}
		}
	}

	[[nodiscard]] bool has(std::size_t index) const
	{
		return index >= base_ && index - base_ < lines_.size();
	}

	UString& mutable_line(std::size_t index) { return lines_[index - base_]; }
	[[nodiscard]] const std::string& eol(std::size_t index) const { return eols_[index - base_]; }

	// Оставить строки начиная с keep_from (окно истории для контекста РВ).
	void drop(std::size_t keep_from)
	{
		while (base_ < keep_from && base_ + lines_.size() > keep_from)
		{
			lines_.pop_front();
			eols_.pop_front();
			++base_;
		}
	}

	[[nodiscard]] std::size_t line_count() const override { return base_ + lines_.size(); }

	[[nodiscard]] const UString* line(std::size_t index) const override
	{
		if (index < base_ || index - base_ >= lines_.size())
			return nullptr;
		return &lines_[index - base_];
	}

private:
	LineReader& reader_;
	std::deque<UString> lines_;
	std::deque<std::string> eols_;
	std::size_t base_ = 0;
	std::size_t lookahead_ = 2;
	bool eof_ = false;
};

struct FileResult
{
	fs::path path;
	std::uint64_t bytes_in = 0;
	std::uint64_t bytes_out = 0;
	std::uint64_t words_changed = 0;
	std::uint64_t ambiguous = 0;
	std::uint64_t unmapped = 0;
	bool interrupted = false;
	bool ambiguous_left = false;
};

struct Totals
{
	Stats stats;
	std::uint64_t files = 0;
	std::uint64_t bytes_in = 0;
	std::uint64_t ambiguous = 0;
	std::uint64_t unmapped = 0;
	double seconds = 0.0;
};

void print_stats(std::FILE* to, const Totals& t)
{
	const auto& s = t.stats;
	const auto re_quality = (s.regex_resolved.a + s.no_regex) == 0
								? 0.0
								: static_cast<double>(s.regex_resolved.a) /
									  static_cast<double>(s.regex_resolved.a + s.no_regex) * 100.0;
	const auto rate = t.seconds > 0.0
						  ? static_cast<long>(static_cast<double>(s.checked.changed) / t.seconds)
						  : 0L;
	// Формат статусбара оригинала: A=A+B  A:...  RE:...  REQ:..%  E:n  T:sec  S:rate
	std::print(to, "{}={}+{}  A:{}={}+{}  RE:{}={}+{}  REQ:{:.1f}%  E:{}  T:{:.2f}  S:{}\n",
			   s.checked.a, s.checked.changed, s.checked.not_changed, s.asked.a, s.asked.changed,
			   s.asked.not_changed, s.regex_resolved.a, s.regex_resolved.changed,
			   s.regex_resolved.not_changed, re_quality, s.wrong_yo, t.seconds, rate);
}

void print_json(std::FILE* to, const Totals& t)
{
	const auto& s = t.stats;
	std::print(to,
			   "{{\n"
			   "  \"files\": {},\n"
			   "  \"bytes_in\": {},\n"
			   "  \"checked\": {},\n"
			   "  \"changed\": {},\n"
			   "  \"not_changed\": {},\n"
			   "  \"asked\": {},\n"
			   "  \"regex_resolved\": {},\n"
			   "  \"no_regex\": {},\n"
			   "  \"wrong_yo\": {},\n"
			   "  \"ambiguous\": {},\n"
			   "  \"unmapped\": {},\n"
			   "  \"seconds\": {:.3f}\n"
			   "}}\n",
			   t.files, t.bytes_in, s.checked.a, s.checked.changed, s.checked.not_changed,
			   s.asked.a, s.regex_resolved.a, s.no_regex, s.wrong_yo, t.ambiguous, t.unmapped,
			   t.seconds);
}

[[nodiscard]] std::string default_dic_path()
{
	const char* env = std::getenv("YO_DIC");
	if (env != nullptr && *env != '\0')
		return env;
	if (fs::exists("yox.dat"))
		return "yox.dat";
	return {};
}

[[nodiscard]] bool is_rtf(const fs::path& p) { return is_rtf_path(p); }

// Атомарная запись: временный файл рядом, затем rename.
bool commit_file(const fs::path& tmp, const fs::path& target, bool backup)
{
	std::error_code ec;
	if (backup && fs::exists(target))
	{
		fs::path bak = target;
		bak += ".bak";
		fs::copy_file(target, bak, fs::copy_options::overwrite_existing, ec);
		if (ec)
			return false;
	}
#if defined(_WIN32)
	fs::remove(target, ec);
#endif
	fs::rename(tmp, target, ec);
	if (ec)
	{
		fs::remove(tmp, ec);
		return false;
	}
	return true;
}

struct RunContext
{
	const CliOptions& opts;
	Dictionary& dict;
	RegexDictionary* regex_dic;
	Totals& totals;
	AmbiguousPolicy policy;
	bool interactive = false;
	std::FILE* regex_log = nullptr;
};

} // namespace

int main(int argc, char** argv)
{
	CliOptions opts;
	for (int i = 1; i < argc; ++i)
	{
		const std::string_view a = argv[i];
		auto next_arg = [&](std::string_view name) -> std::optional<std::string_view>
		{
			if (i + 1 >= argc)
			{
				std::print(stderr, "yo-cli: параметр {} требует значения\n", name);
				return std::nullopt;
			}
			return std::string_view(argv[++i]);
		};

		if (a == "-h" || a == "--help")
		{
			usage(stdout);
			return kExitOk;
		}
		if (a == "--version")
		{
			std::println("yo-cli 0.1.0");
			return kExitOk;
		}
		if (a == "--dic")
		{
			auto v = next_arg(a);
			if (!v)
				return kExitError;
			opts.dic = fs::path(*v);
		}
		else if (a == "--regex")
		{
			auto v = next_arg(a);
			if (!v)
				return kExitError;
			opts.regex = fs::path(*v);
		}
		else if (a == "--no-regex")
			opts.use_regex = false;
		else if (a == "--regex-log")
		{
			auto v = next_arg(a);
			if (!v)
				return kExitError;
			opts.regex_log = fs::path(*v);
		}
		else if (a.starts_with("--on-ambiguous="))
		{
			const auto v = a.substr(std::string_view("--on-ambiguous=").size());
			if (v == "skip")
				opts.policy = AmbiguousPolicy::skip;
			else if (v == "ye")
				opts.policy = AmbiguousPolicy::ye;
			else if (v == "yo")
				opts.policy = AmbiguousPolicy::yo;
			else if (v == "fail")
				opts.policy = AmbiguousPolicy::fail;
			else if (v == "ask")
				opts.policy = AmbiguousPolicy::ask;
			else
			{
				std::print(stderr, "yo-cli: неизвестная политика {}\n", v);
				return kExitError;
			}
		}
		else if (a == "--in-place")
			opts.in_place = true;
		else if (a == "--backup")
			opts.backup = true;
		else if (a == "-o" || a == "--out")
		{
			auto v = next_arg(a);
			if (!v)
				return kExitError;
			opts.out = fs::path(*v);
		}
		else if (a == "--out-dir")
		{
			auto v = next_arg(a);
			if (!v)
				return kExitError;
			opts.out_dir = fs::path(*v);
		}
		else if (a == "--encoding")
		{
			auto v = next_arg(a);
			if (!v)
				return kExitError;
			if (*v != "auto")
			{
				auto e = parse_encoding(*v);
				if (!e)
				{
					std::print(stderr, "yo-cli: неизвестная кодировка {}\n", *v);
					return kExitError;
				}
				opts.from = *e;
				opts.to = *e;
			}
		}
		else if (a == "--from")
		{
			auto v = next_arg(a);
			if (!v)
				return kExitError;
			auto e = parse_encoding(*v);
			if (!e)
			{
				std::print(stderr, "yo-cli: неизвестная кодировка {}\n", *v);
				return kExitError;
			}
			opts.from = *e;
		}
		else if (a == "--to")
		{
			auto v = next_arg(a);
			if (!v)
				return kExitError;
			auto e = parse_encoding(*v);
			if (!e)
			{
				std::print(stderr, "yo-cli: неизвестная кодировка {}\n", *v);
				return kExitError;
			}
			opts.to = *e;
		}
		else if (a == "--report=json")
			opts.json = true;
		else if (a == "--report=text")
			opts.json = false;
		else if (a == "--stats")
			opts.stats = true;
		else if (a == "-q" || a == "--quiet")
			opts.quiet = true;
		else if (a.starts_with("-") && a != "-")
		{
			std::print(stderr, "yo-cli: неизвестный параметр {}\n", a);
			return kExitError;
		}
		else
			opts.inputs.emplace_back(a);
	}

	if (opts.inputs.empty())
	{
		usage(stderr);
		return kExitError;
	}

	// Словарь.
	std::string dic_path = opts.dic ? opts.dic->string() : default_dic_path();
	if (dic_path.empty())
	{
		std::print(stderr, "yo-cli: словарь не найден (укажите --dic)\n");
		return kExitError;
	}
	std::vector<LoadIssue> dict_issues;
	auto dict = Dictionary::load(dic_path, &dict_issues);
	if (!dict)
	{
		std::print(stderr, "yo-cli: {}\n", dict.error());
		return kExitError;
	}
	for (const auto& issue : dict_issues)
	{
		if (issue.kind == "disorder")
			std::print(stderr, "yo-cli: словарь: нарушен порядок у '{}'\n",
					   cp1251::encode(cp1251::decode(issue.detail)));
	}

	std::unique_ptr<RegexDictionary> regex_dic;
	std::vector<LoadIssue> re_issues;
	if (opts.use_regex)
	{
		fs::path re_path;
		if (opts.regex)
			re_path = *opts.regex;
		else if (fs::exists("yo_regexpr.dat"))
			re_path = "yo_regexpr.dat";
		if (!re_path.empty())
		{
			auto rd = RegexDictionary::load(re_path, &re_issues);
			if (!rd)
			{
				std::print(stderr, "yo-cli: {}\n", rd.error());
				return kExitError;
			}
			regex_dic = std::make_unique<RegexDictionary>(std::move(*rd));
			for (const auto& issue : re_issues)
				std::print(stderr, "yo-cli: словарь РВ: {} ({})\n", issue.kind,
						   cp1251::encode(cp1251::decode(issue.detail)));
		}
	}

	const bool tty = YO_ISATTY(stderr) != 0;
	AmbiguousPolicy policy = opts.policy.value_or(AmbiguousPolicy::skip);
	if (policy == AmbiguousPolicy::ask && !tty)
	{
		std::print(stderr, "yo-cli: --on-ambiguous=ask требует терминала\n");
		return kExitError;
	}

	std::FILE* log_file = nullptr;
	if (opts.regex_log)
	{
		log_file = std::fopen(opts.regex_log->string().c_str(), "wb");
		if (log_file == nullptr)
		{
			std::print(stderr, "yo-cli: не удалось открыть журнал {}\n",
					   opts.regex_log->string());
			return kExitError;
		}
	}

	Totals totals;
	int exit_code = kExitOk;
	const bool stdout_mode = opts.inputs.size() == 1 && !opts.in_place && !opts.out;

	for (const auto& input : opts.inputs)
	{
		if (is_rtf(input))
		{
			std::print(stderr, "yo-cli: {} — RTF не поддерживается на этом этапе\n", input.string());
			exit_code = kExitError;
			continue;
		}

		const std::string head = read_head(input, 4096);
		DetectOptions dopts;
		EncodingInfo info = detect_encoding(
			{ reinterpret_cast<const std::uint8_t*>(head.data()), head.size() }, dopts);
		if (opts.from)
			info.enc = *opts.from;
		const Encoding out_enc = opts.to.value_or(info.enc);

		auto in = open_binary(input);
		if (!in)
		{
			std::print(stderr, "yo-cli: не удалось открыть {}\n", input.string());
			exit_code = kExitError;
			continue;
		}
		// Пропустить BOM.
		if (info.bom)
		{
			const auto skip = is_utf16(info.enc) ? 2 : 3;
			in->seekg(skip);
		}

		LineReader reader(std::move(in), info.enc);

		std::optional<std::ofstream> out_file;
		std::ostream* out = nullptr;
		fs::path target;
		fs::path tmp;
		if (stdout_mode)
			out = &std::cout;
		else
		{
			if (opts.in_place)
				target = input;
			else if (opts.out)
				target = *opts.out;
			else
			{
				if (!opts.out_dir)
				{
					std::print(stderr, "yo-cli: для нескольких файлов нужен --out-dir или --in-place\n");
					return kExitError;
				}
				fs::create_directories(*opts.out_dir);
				target = *opts.out_dir / input.filename();
			}
			tmp = target;
			tmp += ".yo-tmp";
			out_file.emplace(tmp, std::ios::binary);
			if (!*out_file)
			{
				std::print(stderr, "yo-cli: не удалось создать {}\n", tmp.string());
				exit_code = kExitError;
				continue;
			}
			out = &*out_file;
		}

		LineWriter writer(*out, out_enc, info.bom);

		Options yo_opts;
		Yoficator yofy(*dict, regex_dic.get(), yo_opts);
		FileResult result;
		result.path = input;

		if (policy == AmbiguousPolicy::ask)
		{
			yofy.set_ask(
				[&](const AskRequest& req) -> AskAnswer
				{
					++result.ambiguous;
					std::print(stderr, "\n'{}' -> '{}' [е/ё/у=всегда е/о=всегда ё/п=пропустить]: ",
							   cp1251::encode(req.word1), cp1251::encode(req.word2));
					std::fflush(stderr);
					char buf[16] = {};
					if (std::fgets(buf, sizeof buf, stdin) == nullptr)
						return AskAnswer::cancel;
					const std::string_view answer(buf);
					if (answer.starts_with("e") || answer.starts_with("е") || answer.starts_with("1"))
						return AskAnswer::ye;
					if (answer.starts_with("y") || answer.starts_with("ё") || answer.starts_with("2"))
						return AskAnswer::yo;
					if (answer.starts_with("u") || answer.starts_with("у") || answer.starts_with("3"))
						return AskAnswer::ye_all;
					if (answer.starts_with("o") || answer.starts_with("о") || answer.starts_with("4"))
						return AskAnswer::yo_all;
					return AskAnswer::skip;
				});
		}
		else
		{
			const AmbiguousPolicy p = policy;
			yofy.set_ask(
				[&, p](const AskRequest&) -> AskAnswer
				{
					++result.ambiguous;
					switch (p)
					{
					case AmbiguousPolicy::ye:
						return AskAnswer::ye;
					case AmbiguousPolicy::yo:
						return AskAnswer::yo;
					case AmbiguousPolicy::fail:
						result.ambiguous_left = true;
						return AskAnswer::skip;
					default:
						return AskAnswer::skip;
					}
				});
		}

		if (log_file != nullptr)
			yofy.set_regex_log([&](std::string_view line)
							   {
								   std::fwrite(line.data(), 1, line.size(), log_file);
								   std::fputc('\n', log_file);
							   });

		StreamSource source(reader);
		std::size_t ln = 0;
		while (source.has(ln))
		{
			source.pump();
			std::vector<Change> changes;
			if (!yofy.process_line(source.mutable_line(ln), ln, source, &changes))
			{
				result.interrupted = true;
				break;
			}
			for (const auto& ch : changes)
			{
				(void)ch;
				++result.words_changed;
			}
			writer.write_line(source.mutable_line(ln), source.eol(ln));
			// Держим две предыдущие строки: они нужны контексту РВ.
			source.drop(ln >= 2 ? ln - 1 : 0);
			++ln;
			source.pump();
		}
		out->flush();

		if (!stdout_mode)
		{
			out_file->close();
			if (result.interrupted)
			{
				std::error_code ec;
				fs::remove(tmp, ec);
			}
			else if (!commit_file(tmp, target, opts.backup))
			{
				std::print(stderr, "yo-cli: не удалось записать {}\n", target.string());
				exit_code = kExitError;
				continue;
			}
		}

		result.bytes_in = static_cast<std::uint64_t>(fs::file_size(input));
		totals.files += 1;
		totals.bytes_in += result.bytes_in;
		totals.ambiguous += result.ambiguous;
		totals.stats = yofy.stats();

		if (result.interrupted)
			exit_code = kExitInterrupted;
		else if (result.ambiguous_left && exit_code == kExitOk)
			exit_code = kExitAmbiguous;

		if (!opts.quiet)
		{
			std::print(stderr, "{}: проверено {}, заменено {}, неоднозначных {}{}\n",
					   input.string(), yofy.stats().checked.a, yofy.stats().checked.changed,
					   result.ambiguous, result.interrupted ? " [прервано]" : "");
		}
	}

	if (log_file != nullptr)
		std::fclose(log_file);

	if (opts.stats && !opts.quiet)
	{
		if (opts.json)
			print_json(stderr, totals);
		else
			print_stats(stderr, totals);
	}
	return exit_code;
}
