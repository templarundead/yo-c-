#include "app.hpp"

#include "yo/codec.hpp"
#include "yo/ini.hpp"
#include "yo/env.hpp"
#include "yo/utf.hpp"
#include "yo/mapped.hpp"

#include <commctrl.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <uxtheme.h>
#include <windowsx.h>
#include <cstring>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <functional>
#include <string_view>
#include <vector>

// commdlg.h требует windows.h раньше; App.hpp его уже включил.
#include <commdlg.h>

namespace yo::gui {

namespace {

constexpr int kCmdOpen = 1001;
constexpr int kCmdNew = 1005;
constexpr int kCmdReload = 1006;
constexpr int kCmdSave = 1002;
constexpr int kCmdSaveAs = 1003;
constexpr int kCmdExit = 1004;
constexpr int kCmdUndo = 1010;
constexpr int kCmdRedo = 1011;
constexpr int kCmdCut = 1012;
constexpr int kCmdCopy = 1013;
constexpr int kCmdPaste = 1014;
constexpr int kCmdSelectAll = 1015;
constexpr int kCmdFind = 1016;
constexpr int kCmdFindNext = 1017;
constexpr int kCmdYofify = 1030;
constexpr int kCmdAlwaysAsk = 1031;
constexpr int kCmdAskWord = 1032;
constexpr int kCmdSilentYofy = 1033;

// Кодировки и переводы строк: пункты-переключатели в меню.
constexpr int kCmdEncCp1251 = 1040;
constexpr int kCmdEncUtf8 = 1041;
constexpr int kCmdEncUtf8Bom = 1042;
constexpr int kCmdEncUtf16le = 1043;
constexpr int kCmdEncUtf16be = 1044;
constexpr int kCmdEolLf = 1050;
constexpr int kCmdEolCrlf = 1051;
constexpr int kCmdEolCr = 1052;
constexpr int kCmdFontBigger = 1020;
constexpr int kCmdFontSmaller = 1021;
constexpr int kCmdFontReset = 1022;
constexpr int kCmdFindPrev = 1018;
constexpr int kCmdReplace = 1019;
constexpr int kCmdFindRegex = 1023;
constexpr int kCmdFileProp = 1024;
constexpr int kCmdLineNumbers = 1025;

constexpr int kCmdAbout = 1026;
constexpr int kCmdYofyNext = 1034;
constexpr int kCmdYofyPrev = 1035;
constexpr int kCmdClearMarks = 1036;
constexpr int kCmdMru1 = 1100;   // kCmdMru1..kCmdMru9 — последние файлы
constexpr int kCmdClearMru = 1110;
constexpr int kCmdOptions = 1111;
constexpr int kCmdOpenDict = 1112;
constexpr int kCmdOpenRegexDict = 1113;
constexpr int kCmdClearUserSel = 1114;
constexpr int kCmdWordWrap = 1115;
constexpr int kCmdDictEditor = 1116;
// Пункты оригинала: «Ёфицировать с начала текста» (Ctrl+F9) и «Ещё о
// программе» (открывает HelpURL в браузере).
constexpr int kCmdYofyFromStart = 1117;
constexpr int kCmdHelpMore = 1118;

constexpr UINT_PTR kCaretTimer = 1;
constexpr int kDefaultWidth = 1000;
constexpr int kDefaultHeight = 720;

std::wstring widen(std::string_view utf8) { return utf8_to_wide(utf8); }
std::string narrow(const std::wstring& wide) { return wide_to_utf8(wide); }

// Поиск файла данных рядом с exe/data — определён ниже; объявление нужно,
// чтобы App::dictionary_path() мог им пользоваться.
std::filesystem::path find_data_file(const std::filesystem::path& name);

// Каталог программы: рядом с ним лежат yo.ini и словари по умолчанию.
std::filesystem::path program_dir()
{
	wchar_t buf[32768] = L"";
	const DWORD len = GetModuleFileNameW(nullptr, buf, static_cast<DWORD>(std::size(buf)));
	if (len == 0)
		return {};
	return std::filesystem::path(std::wstring(buf, buf + len)).parent_path();
}

// Путь в UTF-8 для ядра (MappedFile ожидает именно UTF-8).
std::string path_utf8(const std::filesystem::path& path) { return wide_to_utf8(path.wstring()); }

// Масштаб DPI окна (per-monitor v2, с откатом на системный DPI).
float query_dpi(HWND hwnd)
{
	using GetDpiFn = UINT(WINAPI*)(HWND);
	if (HMODULE user32 = GetModuleHandleW(L"user32"))
	{
		const auto fn = reinterpret_cast<GetDpiFn>(
			reinterpret_cast<void*>(GetProcAddress(user32, "GetDpiForWindow")));
		if (fn != nullptr)
		{
			const UINT dpi = fn(hwnd);
			if (dpi != 0)
				return static_cast<float>(dpi) / 96.0F;
		}
	}
	HDC dc = GetDC(hwnd);
	const int dpi = dc != nullptr ? GetDeviceCaps(dc, LOGPIXELSX) : 96;
	if (dc != nullptr)
		ReleaseDC(hwnd, dc);
	return static_cast<float>(dpi > 0 ? dpi : 96) / 96.0F;
}

// Доминирующий стиль перевода строки по первым строкам документа.
Eol dominant_eol(const TextBuffer& buf)
{
	std::array<std::size_t, 4> count{}; // none, lf, crlf, cr
	const std::size_t lines = std::min<std::size_t>(buf.line_count(), 2000);
	for (std::size_t i = 0; i < lines; ++i)
		++count[static_cast<std::size_t>(buf.line_eol(i))];
	std::size_t best = 1;
	for (std::size_t i = 1; i < count.size(); ++i)
	{
		if (count[i] > count[best])
			best = i;
	}
	return static_cast<Eol>(best);
}

std::wstring format_size(std::uint64_t bytes)
{
	wchar_t buf[64];
	if (bytes >= 1024ULL * 1024ULL)
		std::swprintf(buf, std::size(buf), L"%.1f МБ", static_cast<double>(bytes) / (1024.0 * 1024.0));
	else if (bytes >= 1024ULL)
		std::swprintf(buf, std::size(buf), L"%.1f КБ", static_cast<double>(bytes) / 1024.0);
	else
		std::swprintf(buf, std::size(buf), L"%llu Б", static_cast<unsigned long long>(bytes));
	return buf;
}

const wchar_t* encoding_name(Encoding enc, bool bom)
{
	switch (enc)
	{
	case Encoding::cp1251:
		return L"CP1251";
	case Encoding::utf8:
		return bom ? L"UTF-8 (BOM)" : L"UTF-8";
	case Encoding::utf16le:
		return L"UTF-16LE";
	case Encoding::utf16be:
		return L"UTF-16BE";
	}
	return L"?";
}

// Диагностика: при YO_GUI_TRACE=1 в %TEMP%\yo-gui-trace.log пишутся
// длительности операций. GUI нельзя отладить удалённо, а «зависание» проще
// всего ловить по времени: видно, что именно заняло секунды.
bool trace_enabled()
{
	static const bool on = env_var("YO_GUI_TRACE").has_value();
	return on;
}

void trace(const char* what, double ms, std::size_t bytes = 0)
{
	if (!trace_enabled())
		return;
	std::error_code ec;
	static const std::filesystem::path path =
		std::filesystem::temp_directory_path(ec) / L"yo-gui-trace.log";
	std::ofstream out(path, std::ios::app);
	out << what << " " << ms << " мс";
	if (bytes != 0)
		out << " " << bytes << " Б";
	out << "\n";
}

// Сообщение без длительности (настройки, состояния).
void trace(const char* what) { trace(what, 0.0); }

// Граница слова для пункта «спросить про слово под курсором»: кириллица
// (байты >= 0x80) — часть слова, дефис тоже, остальная пунктуация и пробелы
// разделяют.
bool is_word_break(unsigned char c)
{
	if (c < 0x80U)
	{
		const bool alnum = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
		return !alnum && c != '-' && c != '_';
	}
	return false; // байт многобайтового символа
}

// Есть ли в строке предложение целиком (терминатор в конце) — нужно, чтобы
// понять, где заканчивается фраза вокруг слова.
bool ends_sentence(std::string_view text)
{
	for (std::size_t i = text.size(); i > 0; --i)
	{
		const char c = text[i - 1];
		if (c == ' ' || c == '\t' || c == '\r')
			continue;
		if ((c & 0xC0) == 0x80U)
			continue; // продолжение многобайтового символа
		if (c == '.' || c == '!' || c == '?')
			return true;
		// многоточие «…» (U+2026) — три байта E2 80 A6
		if (i >= 3 && text[i - 3] == '\xE2' && text[i - 2] == '\x80' && text[i - 1] == '\xA6')
			return true;
		return false;
	}
	return false;
}

std::string trimmed_line(const TextBuffer& buf, std::size_t line)
{
	if (line >= buf.line_count())
		return {};
	std::string_view text = buf.line_text(line);
	std::size_t begin = 0;
	while (begin < text.size() && (text[begin] == '\t' || text[begin] == ' '))
		++begin;
	std::size_t end = text.size();
	while (end > begin && (text[end - 1] == '\r' || text[end - 1] == '\n' || text[end - 1] == ' ' ||
						   text[end - 1] == '\t'))
		--end;
	return std::string(text.substr(begin, end - begin));
}

// Предложение с обрабатываемым словом: соседние строки, пока фраза не
// закончилась, но не больше четырёх строк и ~800 символов (как в AskPanel
// оригинала, где видно контекст слова).
std::string sentence_around(const TextBuffer& buf, std::size_t line)
{
	constexpr std::size_t kMaxLines = 4;
	constexpr std::size_t kMaxBytes = 800;

	std::size_t first = line;
	std::size_t last = line;
	std::size_t total = trimmed_line(buf, line).size();

	while (first > 0 && last - first + 1 < kMaxLines && total < kMaxBytes)
	{
		const std::string prev = trimmed_line(buf, first - 1);
		if (prev.empty())
			break; // пустая строка — граница абзаца
		if (ends_sentence(prev))
			break; // предыдущая строка закончила фразу — фраза начинается здесь
		total += prev.size() + 1;
		--first;
	}
	while (last + 1 < buf.line_count() && last - first + 1 < kMaxLines && total < kMaxBytes)
	{
		const std::string next = trimmed_line(buf, last + 1);
		if (next.empty())
			break;
		total += next.size() + 1;
		++last;
		if (ends_sentence(next))
			break;
	}

	std::string text;
	for (std::size_t i = first; i <= last; ++i)
	{
		const std::string part = trimmed_line(buf, i);
		if (part.empty())
			continue;
		if (!text.empty())
			text += ' ';
		text += part;
	}
	return text;
}

// Тёмное оформление системных элементов окна.
//
// Полосы прокрутки, рамка и заголовок рисуются системой, поэтому цветом
// клиентской области их не перекрасить. Официального API нет: приложения
// используют точки входа uxtheme по ординалам (135 — SetPreferredAppMode,
// 133 — AllowDarkModeForWindow), они живут в Windows 10 1809+ и Windows 11.
// Если ординалов нет, окно просто останется светлым — не фатально.
void enable_dark_mode(HWND hwnd)
{
	if (HMODULE uxtheme = LoadLibraryExW(L"uxtheme.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32))
	{
		using SetPreferredAppModeFn = int(WINAPI*)(int);
		using AllowDarkModeForWindowFn = bool(WINAPI*)(HWND, bool);
		using FlushMenuThemesFn = void(WINAPI*)();

		const auto ord = [uxtheme](int n) { return GetProcAddress(uxtheme, MAKEINTRESOURCEA(n)); };
		const auto set_mode =
			reinterpret_cast<SetPreferredAppModeFn>(reinterpret_cast<void*>(ord(135)));
		const auto allow =
			reinterpret_cast<AllowDarkModeForWindowFn>(reinterpret_cast<void*>(ord(133)));
		const auto flush = reinterpret_cast<FlushMenuThemesFn>(reinterpret_cast<void*>(ord(136)));
		if (set_mode != nullptr)
			set_mode(2); // ForceDark
		if (allow != nullptr)
			allow(hwnd, true);
		if (flush != nullptr)
			flush();
	}
	// «Тёмный» вариант оформления Explorer: полосы прокрутки без белых
	// желобов и кнопок.
	SetWindowTheme(hwnd, L"DarkMode_Explorer", nullptr);
}

const wchar_t* eol_name(Eol eol)
{
	switch (eol)
	{
	case Eol::lf:
		return L"LF";
	case Eol::crlf:
		return L"CRLF";
	case Eol::cr:
		return L"CR";
	case Eol::none:
		break;
	}
	return L"—";
}

} // namespace

App::App()
{
	buf_ = std::make_shared<TextBuffer>();
	ed_ = std::make_unique<Editor>(buf_);
}

App::~App()
{
	if (accel_ != nullptr)
		DestroyAcceleratorTable(accel_);
}

// ---------------------------------------------------------------- создание

namespace {

} // namespace

void App::load_settings()
{
	ini_path_ = settings_path();
	std::ifstream in(ini_path_, std::ios::binary);
	if (!in)
	{
		trace("настройки: yo.ini не найден, берём значения по умолчанию");
		return;
	}
	const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
	Ini ini;
	ini.parse(text);

	// Имена ключей — как в yo.ini оригинала (FORMATS.md §3), чтобы подходил
	// и файл от старого ёфикатора.
	constexpr std::string_view kYo = "Yo";
	font_size_ = std::clamp(ini.get_float(kYo, "FontSize", font_size_), 6.0F, 96.0F);
	// Порядок и умолчания — как в ReadIni оригинала.
	always_ask_ = ini.get_bool(kYo, "AlwaysAsk", always_ask_);
	mark_ = ini.get_bool(kYo, "Mark", mark_);
	fast_scroll_ = ini.get_bool(kYo, "FastScroll", fast_scroll_);
	check_yo_ = ini.get_bool(kYo, "CheckYo", check_yo_);
	no_var_only_ = ini.get_bool(kYo, "NoVarOnly", no_var_only_);
	var_only_ = ini.get_bool(kYo, "VarOnly", var_only_);
	word_wrap_ = ini.get_bool(kYo, "WordWrap", word_wrap_);
	propose_last_ = ini.get_bool(kYo, "ProposeLast", propose_last_);
	fb_format_ = ini.get_bool(kYo, "FBFormat", fb_format_);
	auto_unicode_ = ini.get_bool(kYo, "AutoUnicode", auto_unicode_);
	regexs_ = ini.get_bool(kYo, "RegExprs", regexs_);
	recall_last_file_ = ini.get_bool(kYo, "RecallLastFile", recall_last_file_);
	to_confirm_close_ = ini.get_bool(kYo, "ToConfirmClose", to_confirm_close_);
	to_confirm_abbr_ = ini.get_bool(kYo, "ToConfirmAbbr", to_confirm_abbr_);
	to_confirm_ellipsis_ = ini.get_bool(kYo, "ToConfirmEllipsis", to_confirm_ellipsis_);
	to_confirm_cap_ = ini.get_bool(kYo, "ToConfirmCap", to_confirm_cap_);
	lines_below_ = static_cast<int>(std::clamp<long long>(ini.get_int(kYo, "LinesBelow", lines_below_), 0, 40));
	// Наше дополнение (в оригинале такой опции нет).
	silent_yofy_ = ini.get_bool(kYo, "SilentYofy", silent_yofy_);
	// [Colors] — COLORREF, как в оригинале; индексы старых сборок yo-c-
	// тоже понимаются (options_color_from_ini).
	mark_color_ = yo::options_color_from_ini(
		ini.get_int("Colors", "ColorMark", static_cast<long long>(mark_color_)));
	back_mark_color_ = yo::options_color_from_ini(
		ini.get_int("Colors", "ColorBackMark", static_cast<long long>(back_mark_color_)));
	// Шрифт редактора: имя и charset, как FontName/FontCharset оригинала.
	{
		const std::wstring name = widen(ini.get(kYo, "FontName"));
		if (!name.empty())
			font_name_ = name;
		font_charset_ = static_cast<int>(std::clamp<long long>(ini.get_int(kYo, "FontCharset", font_charset_), 0, 255));
	}
	// Взаимоисключение VarOnly/AlwaysAsk: в старом файле могли остаться оба.
	if (always_ask_)
		var_only_ = false;
	// [Internal] — ключи оригинала; у нас не используются, но должны
	// переживать перезапись файла (см. save_settings).
	show_toolbar_ = ini.get_bool("ToolBar", "ShowToolBar", show_toolbar_);

	const auto get_w = [&](std::string_view key)
	{ return widen(ini.get(kYo, key)); };
	dict_setting_ = get_w("Dictionary");
	regex_setting_ = get_w("RegExprDictionary");

	// Геометрия окна — как в оригинале: четыре отдельных ключа.
	const long long left = ini.get_int(kYo, "WindowLeft", 0);
	const long long top = ini.get_int(kYo, "WindowTop", 0);
	const long long width = ini.get_int(kYo, "WindowWidth", 0);
	const long long height = ini.get_int(kYo, "WindowHeight", 0);
	if (width > 200 && height > 200)
	{
		saved_rect_ = RECT{ static_cast<LONG>(left), static_cast<LONG>(top),
							static_cast<LONG>(left + width), static_cast<LONG>(top + height) };
		has_saved_rect_ = true;
	}
	saved_maximized_ = ini.get_bool(kYo, "Maximized", false);
	recall_last_file_ = ini.get_bool(kYo, "RecallLastFile", recall_last_file_);

	// Последние файлы: [Files] Name1..9 (строки «путь,позиция,флаги») и
	// [Files] LastFileN — индекс последнего открытого.
	std::vector<MruEntry> entries;
	for (std::size_t i = 1; i <= kMruMax; ++i)
	{
		const std::string value = ini.get("Files", "Name" + std::to_string(i));
		if (value.empty())
			continue;
		entries.push_back(mru_parse(value));
	}
	mru_.set(entries);
	const long long last = ini.get_int("Files", "LastFileN", 0);
	if (last >= 0 && static_cast<std::size_t>(last) < mru_.size())
		last_file_index_ = static_cast<std::size_t>(last);
	trace("настройки: загружены из yo.ini");
}

void App::save_settings()
{
	if (ini_path_.empty())
		ini_path_ = settings_path();
	mru_remember_current(); // позиция курсора в текущем файле — в MRU

	// Файл перезаписывается целиком, поэтому сначала читаем его: чужие
	// ключи (MRU, цвета, панель инструментов) не должны теряться.
	Ini ini;
	{
		std::ifstream in(ini_path_, std::ios::binary);
		if (in)
		{
			const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
			ini.parse(text);
		}
	}
	constexpr std::string_view kYo = "Yo";
	// Набор ключей — как в WriteIni оригинала (плюс наши SilentYofy/[Colors]).
	ini.set_bool(kYo, "AlwaysAsk", always_ask_);
	ini.set_bool(kYo, "Mark", mark_);
	ini.set_bool(kYo, "FastScroll", fast_scroll_);
	ini.set_bool(kYo, "CheckYo", check_yo_);
	ini.set_bool(kYo, "NoVarOnly", no_var_only_);
	ini.set_bool(kYo, "VarOnly", var_only_);
	ini.set_bool(kYo, "WordWrap", word_wrap_);
	ini.set_bool(kYo, "ProposeLast", propose_last_);
	ini.set_bool(kYo, "FBFormat", fb_format_);
	ini.set_bool(kYo, "AutoUnicode", auto_unicode_);
	ini.set_bool(kYo, "RegExprs", regexs_);
	ini.set_bool(kYo, "ToConfirmClose", to_confirm_close_);
	ini.set_bool(kYo, "ToConfirmAbbr", to_confirm_abbr_);
	ini.set_bool(kYo, "ToConfirmEllipsis", to_confirm_ellipsis_);
	ini.set_bool(kYo, "ToConfirmCap", to_confirm_cap_);
	ini.set_int(kYo, "LinesBelow", lines_below_);
	ini.set_bool(kYo, "SilentYofy", silent_yofy_);
	ini.set_int("Colors", "ColorMark", static_cast<long long>(mark_color_));
	ini.set_int("Colors", "ColorBackMark", static_cast<long long>(back_mark_color_));
	ini.set_bool("ToolBar", "ShowToolBar", show_toolbar_);
	ini.set("Yo", "ButtonHotKeys", "1234"); // порядок кнопок AskPanel, как в оригинале
	ini.set_int("Yo", "WordsBufferLength", 5);
	ini.set_int("Yo", "FontCharset", 1);
	if (!ini.has("Internal", "UTF8Warning"))
		ini.set_bool("Internal", "UTF8Warning", true);
	if (!ini.has("Internal", "UTFLength"))
		ini.set_int("Internal", "UTFLength", 512);
	if (!font_name_.empty())
		ini.set(kYo, "FontName", narrow(font_name_));
	ini.set_int(kYo, "FontCharset", font_charset_);
	ini.set_float(kYo, "FontSize", font_size_);
	if (!dict_setting_.empty())
		ini.set(kYo, "Dictionary", narrow(dict_setting_));
	if (!regex_setting_.empty())
		ini.set(kYo, "RegExprDictionary", narrow(regex_setting_));

	if (hwnd_ != nullptr)
	{
		WINDOWPLACEMENT wp{};
		wp.length = sizeof(wp);
		RECT r{};
		if (GetWindowPlacement(hwnd_, &wp))
			r = wp.rcNormalPosition; // обычные (не развёрнутые) габариты
		else
			GetWindowRect(hwnd_, &r);
		ini.set_int(kYo, "WindowLeft", r.left);
		ini.set_int(kYo, "WindowTop", r.top);
		ini.set_int(kYo, "WindowWidth", r.right - r.left);
		ini.set_int(kYo, "WindowHeight", r.bottom - r.top);
		ini.set_bool(kYo, "Maximized", IsZoomed(hwnd_) != 0);
	}

	// Последние файлы: список пишется целиком, лишние ключи NameN удаляются,
	// чтобы после «Очистить список» в файле не оставалось мусора.
	for (std::size_t i = 1; i <= kMruMax; ++i)
	{
		const std::string key = "Name" + std::to_string(i);
		const MruEntry* e = mru_.at(i - 1);
		if (e != nullptr)
			ini.set("Files", key, mru_format(*e));
		else if (ini.has("Files", key))
			ini.set("Files", key, std::string{});
	}
	ini.set_int("Files", "LastFileN",
				last_file_index_ < mru_.size() ? static_cast<long long>(last_file_index_) : 0);
	ini.set_bool(kYo, "RecallLastFile", recall_last_file_);

	const std::string text = "; Ё (yo-c-) — настройки редактора. Файл можно править руками.\n" + ini.dump();
	std::ofstream out(ini_path_, std::ios::binary | std::ios::trunc);
	if (!out)
	{
		trace("настройки: не удалось записать yo.ini");
		return;
	}
	out.write(text.data(), static_cast<std::streamsize>(text.size()));
	trace("настройки: сохранены в yo.ini");
}

std::filesystem::path App::settings_path() const
{
	wchar_t buf[32768] = L"";
	const DWORD len = GetModuleFileNameW(nullptr, buf, static_cast<DWORD>(std::size(buf)));
	if (len == 0)
		return std::filesystem::path(L"yo.ini");
	std::filesystem::path path(std::wstring(buf, buf + len));
	path.replace_filename(L"yo.ini");
	return path;
}

bool App::create(HINSTANCE hinst, int cmd_show, const std::wstring& file)
{
	hinst_ = hinst;
	load_settings();

	WNDCLASSEXW wc{};
	wc.cbSize = sizeof(wc);
	wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
	wc.lpfnWndProc = &App::wnd_proc;
	wc.hInstance = hinst;
	wc.hCursor = LoadCursorW(nullptr, IDC_IBEAM);
	wc.hbrBackground = nullptr; // рисуем сами
	wc.lpszClassName = L"YoCEditorWindow";
	wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
	if (RegisterClassExW(&wc) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
		return false;

	const int wx = has_saved_rect_ ? saved_rect_.left : CW_USEDEFAULT;
	const int wy = has_saved_rect_ ? saved_rect_.top : CW_USEDEFAULT;
	const int ww = has_saved_rect_ ? static_cast<int>(saved_rect_.right - saved_rect_.left) : kDefaultWidth;
	const int wh = has_saved_rect_ ? static_cast<int>(saved_rect_.bottom - saved_rect_.top) : kDefaultHeight;
	hwnd_ = CreateWindowExW(0, L"YoCEditorWindow", L"Ё (yo-c-)", WS_OVERLAPPEDWINDOW | WS_VSCROLL | WS_HSCROLL,
							wx, wy, ww, wh, nullptr, nullptr, hinst, this);
	if (hwnd_ == nullptr)
		return false;

	ShowWindow(hwnd_, cmd_show);
	if (saved_maximized_)
		ShowWindow(hwnd_, SW_SHOWMAXIMIZED);
	UpdateWindow(hwnd_);

	if (!file.empty())
	{
		load_from_path(std::filesystem::path(file));
		refresh_view();
	}
	else if (recall_last_file_ && !mru_.empty())
	{
		// Как RecallLastFile оригинала: последний файл открывается сам,
		// но только если файл не задан в командной строке.
		open_mru(last_file_index_ < mru_.size() ? last_file_index_ : mru_.size() - 1);
	}
	return true;
}

int App::run()
{
	MSG msg{};
	while (GetMessageW(&msg, nullptr, 0, 0) > 0)
	{
		if (accel_ != nullptr && TranslateAcceleratorW(hwnd_, accel_, &msg) != 0)
			continue;
		TranslateMessage(&msg);
		DispatchMessageW(&msg);
	}
	return static_cast<int>(msg.wParam);
}

LRESULT CALLBACK App::wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
	if (msg == WM_NCCREATE)
	{
		auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
		auto* self = static_cast<App*>(cs->lpCreateParams);
		self->hwnd_ = hwnd;
		SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
		SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&App::wnd_proc));
		return DefWindowProcW(hwnd, msg, wp, lp);
	}
	auto* self = reinterpret_cast<App*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
	if (self == nullptr)
		return DefWindowProcW(hwnd, msg, wp, lp);
	return self->on_message(hwnd, msg, wp, lp);
}

void App::on_create()
{
	DragAcceptFiles(hwnd_, TRUE);
	enable_dark_mode(hwnd_);

	// Тёмный заголовок окна (Windows 10 1903+ / 11). Атрибут 20 для новых
	// сборок, 19 — для более старых.
	BOOL dark = TRUE;
	if (FAILED(DwmSetWindowAttribute(hwnd_, 20, &dark, sizeof(dark))))
		DwmSetWindowAttribute(hwnd_, 19, &dark, sizeof(dark));

	dpi_scale_ = query_dpi(hwnd_);
	if (!renderer_.init(hwnd_, font_size_))
		MessageBoxW(hwnd_, L"Не удалось инициализировать Direct2D/DirectWrite.", L"Ё", MB_ICONERROR);
	renderer_.set_dpi(dpi_scale_);
	// Шрифт из yo.ini (FontName/FontSize) — до первой раскладки.
	if (!font_name_.empty())
		renderer_.set_editor_font(font_name_, font_size_, hwnd_);
	apply_mark_colors(); // [Colors] ColorMark / ColorBackMark
	sync_metrics();
	apply_word_wrap(); // [Yo] WordWrap (в оригинале по умолчанию включён)
	// Словарь читаем сразу, а не при первом проходе: в оригинале Dic загружен
	// в FormCreate, поэтому «Ёфицировать» из меню доступна с первого клика
	// (раньше пункт включался только после прохода из контекстного меню).
	(void)ensure_yoficator(/*quiet=*/true);

	build_menus();
	setup_accels();

	RECT rc{};
	GetClientRect(hwnd_, &rc);
	renderer_.resize(static_cast<UINT>(rc.right - rc.left), static_cast<UINT>(rc.bottom - rc.top));
	update_scrollbars();
	update_title();

	const UINT blink = GetCaretBlinkTime();
	SetTimer(hwnd_, kCaretTimer, (blink == 0 || blink == INFINITE) ? 530U : blink, nullptr);
}

void App::build_menus()
{
	// Заголовки и пункты хранятся данными: рисуем их сами, потому что
	// системная строка меню на тёмной теме остаётся белой и выглядит
	// чужеродно.
	//
	// Состав, порядок, подписи и сочетания клавиш — один в один с MainMenu1
	// оригинала (src/Main.dfm): «Файл / Редактировать / Ёфикация / Настройки /
	// Помощь». Наши собственные пункты (редактор словаря, перечитать с диска,
	// замена, кодировки, перевод строк, вид) собраны отдельными группами;
	// что именно добавлено — в DEVIATIONS.md.
	menus_.clear();

	MenuDef file;
	file.title = L"Файл";
	file.entries = {
		{ L"Загрузить текст\tCtrl+L", kCmdOpen },
		{ L"Сохранить текст\tCtrl+Alt+S", kCmdSave },
		{ L"Сохранить текст как\tCtrl+S", kCmdSaveAs },
		{ L"Очистить текстовое окно", kCmdNew },
		{ L"", 0, true },
		{ L"Загрузить основной словарь", kCmdOpenDict },
		{ L"Загрузить словарь регулярных выражений", kCmdOpenRegexDict },
		{ L"Очистить список последних файлов", kCmdClearMru },
		{ L"", 0, true },
		// Наши пункты: в оригинале редактора словаря и перечитывания файла нет.
		{ L"Редактор словаря…\tCtrl+Shift+D", kCmdDictEditor },
		{ L"Перечитать с диска\tCtrl+Shift+R", kCmdReload },
		{ L"", 0, true },
		{ L"Свойства файла\tCtrl+P", kCmdFileProp },
		{ L"Выход", kCmdExit },
	};
	// Список последних файлов — как скрытый шаблон Openfile0 оригинала: он
	// стоит последним в меню «Файл» и раскрывается в имена файлов.
	if (!mru_.empty())
	{
		file.entries.push_back(MenuEntry{ L"", 0, true });
		for (std::size_t i = 0; i < mru_.size(); ++i)
		{
			const std::filesystem::path p(widen(mru_.entries()[i].path));
			std::wstring label = p.filename().wstring();
			if (label.empty())
				label = p.wstring();
			label += L"\tCtrl+" + std::to_wstring(i + 1);
			file.entries.push_back(
				MenuEntry{ label, kCmdMru1 + static_cast<int>(i) });
		}
	}
	menus_.push_back(std::move(file));

	MenuDef edit;
	edit.title = L"Редактировать";
	edit.entries = {
		{ L"Вырезать\tCtrl+X", kCmdCut },
		{ L"Копировать\tCtrl+C", kCmdCopy },
		{ L"Вставить\tCtrl+V", kCmdPaste },
		{ L"Выделить всё\tCtrl+A", kCmdSelectAll },
		{ L"", 0, true },
		{ L"Откат\tCtrl+Z", kCmdUndo },
		{ L"", 0, true },
		{ L"Найти\tCtrl+F", kCmdFind },
		{ L"Найти регулярное выражение\tCtrl+R", kCmdFindRegex },
		{ L"Найти снова\tF3", kCmdFindNext },
		{ L"Искать замену вперёд\tCtrl+W", kCmdYofyNext },
		{ L"Искать замену назад\tCtrl+Q", kCmdYofyPrev },
		{ L"", 0, true },
		// Наши пункты: в оригинале нет замены по образцу и поиска назад.
		{ L"Заменить…\tCtrl+H", kCmdReplace },
		{ L"Найти назад\tShift+F3", kCmdFindPrev },
		{ L"Повторить\tCtrl+Y", kCmdRedo },
	};
	menus_.push_back(std::move(edit));

	MenuDef yofy;
	yofy.title = L"Ёфикация";
	yofy.entries = {
		{ L"Ёфицировать с текущей позиции\tF9", kCmdYofify },
		{ L"Ёфицировать с начала текста\tCtrl+F9", kCmdYofyFromStart },
		{ L"Убрать выделение\tShift+F10", kCmdClearMarks },
		{ L"Сбросить \"Всегда Е/Ё\"", kCmdClearUserSel },
		{ L"", 0, true },
		// Наши пункты (в оригинале это только кнопки панели вопросов).
		{ L"Спросить про слово под курсором\tCtrl+Shift+E", kCmdAskWord },
		{ L"Без вопросов", kCmdSilentYofy },
		{ L"Спрашивать про все слова", kCmdAlwaysAsk },
	};
	menus_.push_back(std::move(yofy));

	MenuDef view;
	view.title = L"Вид";
	view.entries = {
		{ L"Крупнее\tCtrl++", kCmdFontBigger },
		{ L"Мельче\tCtrl+-", kCmdFontSmaller },
		{ L"Обычный размер\tCtrl+0", kCmdFontReset },
		{ L"", 0, true },
		{ L"Номера строк\tCtrl+Shift+L", kCmdLineNumbers },
		{ L"Перенос по словам\tCtrl+Shift+W", kCmdWordWrap },
	};
	menus_.push_back(std::move(view));

	MenuDef enc;
	enc.title = L"Кодировка";
	enc.entries = {
		{ L"CP1251 (Windows)", kCmdEncCp1251 },
		{ L"UTF-8", kCmdEncUtf8 },
		{ L"UTF-8 с BOM", kCmdEncUtf8Bom },
		{ L"UTF-16LE", kCmdEncUtf16le },
		{ L"UTF-16BE", kCmdEncUtf16be },
	};
	menus_.push_back(std::move(enc));

	MenuDef eol;
	eol.title = L"Перевод строк";
	eol.entries = {
		{ L"LF (Unix)", kCmdEolLf },
		{ L"CRLF (Windows)", kCmdEolCrlf },
		{ L"CR (Mac)", kCmdEolCr },
	};
	menus_.push_back(std::move(eol));

	MenuDef settings;
	settings.title = L"Настройки";
	settings.entries = {
		{ L"Опции\tCtrl+Alt+O", kCmdOptions },
	};
	menus_.push_back(std::move(settings));

	MenuDef help;
	help.title = L"Помощь";
	help.entries = {
		{ L"О программе\tF1", kCmdAbout },
		{ L"Ещё о программе", kCmdHelpMore },
	};
	menus_.push_back(std::move(help));

	// Начальные состояния disabled/checked — по SetControls/EnableMenu.
	update_menu_checks();

	layout_menus();
}

void App::layout_menus()
{
	menu_title_rects_.assign(menus_.size(), D2D1_RECT_F{});
	float x = 4.0F;
	const float h = renderer_.bar_height();
	for (std::size_t i = 0; i < menus_.size(); ++i)
	{
		const float w = renderer_.measure_ui(menus_[i].title) + 20.0F;
		menu_title_rects_[i] = D2D1::RectF(x, 0.0F, x + w, h);
		x += w;
	}
	// Габариты пунктов открытого меню.
	menu_item_rects_.clear();
	if (open_menu_ >= 0 && static_cast<std::size_t>(open_menu_) < menus_.size())
	{
		const MenuDef& def = menus_[static_cast<std::size_t>(open_menu_)];
		// Высота строки меню — от шрифта интерфейса: Ctrl+± не должен
		// растягивать выпадающие списки.
		const float row_h = std::max(20.0F, renderer_.ui_line_height() + 8.0F);
		// Ширина пункта: подпись + сочетание клавиш справа (в метке они
		// разделены табуляцией, и таб не должен считаться нулевой шириной).
		const auto label_width = [this](const std::wstring& label)
		{
			const std::size_t tab = label.find(L'\t');
			if (tab == std::wstring::npos)
				return renderer_.measure_ui(label);
			const std::wstring name = label.substr(0, tab);
			const std::wstring accel = label.substr(tab + 1);
			return renderer_.measure_ui(name) + 24.0F + renderer_.measure_ui(accel);
		};
		float width = 0.0F;
		for (const MenuEntry& e : def.entries)
			width = std::max(width, label_width(e.label) + 28.0F);
		const D2D1_RECT_F& title = menu_title_rects_[static_cast<std::size_t>(open_menu_)];
		const float top = h;
		float y = top + 4.0F;
		menu_item_rects_.clear();
		for (std::size_t i = 0; i < def.entries.size(); ++i)
		{
			menu_item_rects_.push_back(D2D1::RectF(title.left, y, title.left + width, y + row_h));
			y += row_h;
		}
		menu_panel_ = D2D1::RectF(title.left, top, title.left + width, y + 4.0F);
	}
}

MenuBarView App::menu_bar_view()
{
	MenuBarView view;
	for (const MenuDef& m : menus_)
		view.titles.push_back(m.title);
	view.title_rects = menu_title_rects_;
	view.active = open_menu_;
	view.hot_title = hot_title_;
	if (open_menu_ < 0 || static_cast<std::size_t>(open_menu_) >= menus_.size())
		return view;

	const MenuDef& def = menus_[static_cast<std::size_t>(open_menu_)];
	view.dropdown = true;
	view.panel = menu_panel_;
	for (std::size_t i = 0; i < def.entries.size() && i < menu_item_rects_.size(); ++i)
	{
		const MenuEntry& e = def.entries[i];
		MenuItemView item;
		item.rect = menu_item_rects_[i];
		item.separator = e.separator;
		item.checked = e.checked;
		item.enabled = e.enabled;
		item.hot = static_cast<int>(i) == hot_item_;
		item.label = e.label;
		view.items.push_back(std::move(item));
	}
	return view;
}

void App::menu_close()
{
	open_menu_ = -1;
	hot_item_ = -1;
	invalidate();
}

bool App::menu_mouse_move(float x, float y)
{
	if (open_menu_ >= 0)
	{
		// Наведение на другой заголовок переключает меню (как в обычных меню).
		for (std::size_t i = 0; i < menu_title_rects_.size(); ++i)
		{
			const D2D1_RECT_F& r = menu_title_rects_[i];
			if (y >= r.top && y < r.bottom && x >= r.left && x < r.right)
			{
				if (static_cast<int>(i) != open_menu_)
				{
					open_menu_ = static_cast<int>(i);
					hot_item_ = -1;
					layout_menus();
				}
				hot_title_ = static_cast<int>(i);
				invalidate();
				return true;
			}
		}
		for (std::size_t i = 0; i < menu_item_rects_.size(); ++i)
		{
			const D2D1_RECT_F& r = menu_item_rects_[i];
			if (x >= r.left && x < r.right && y >= r.top && y < r.bottom)
			{
				hot_item_ = static_cast<int>(i);
				invalidate();
				return true;
			}
		}
		if (hot_item_ != -1)
		{
			hot_item_ = -1; // мышь ушла с пункта
			invalidate();
		}
		if (y >= 0.0F && y < renderer_.bar_height())
			return true;
	}
	// Заголовки подсвечиваем и при закрытых меню — только когда мышь над ними.
	int hot = -1;
	for (std::size_t i = 0; i < menu_title_rects_.size(); ++i)
	{
		const D2D1_RECT_F& r = menu_title_rects_[i];
		if (y >= r.top && y < r.bottom && x >= r.left && x < r.right)
			hot = static_cast<int>(i);
	}
	if (hot != hot_title_)
	{
		hot_title_ = hot;
		invalidate();
	}
	return hot >= 0 || (open_menu_ >= 0 && y < renderer_.bar_height());
}

bool App::menu_click(float x, float y)
{
	const float bar_h = renderer_.bar_height();
	for (std::size_t i = 0; i < menu_title_rects_.size(); ++i)
	{
		const D2D1_RECT_F& r = menu_title_rects_[i];
		if (y >= r.top && y < r.bottom && x >= r.left && x < r.right)
		{
			open_menu_ = (open_menu_ == static_cast<int>(i)) ? -1 : static_cast<int>(i);
			hot_item_ = -1;
			layout_menus();
			invalidate();
			return true;
		}
	}

	if (open_menu_ < 0)
		return y < bar_h; // клик по пустому месту строки меню

	if (x >= menu_panel_.left && x < menu_panel_.right && y >= menu_panel_.top && y < menu_panel_.bottom)
	{
		const MenuDef& def = menus_[static_cast<std::size_t>(open_menu_)];
		for (std::size_t i = 0; i < menu_item_rects_.size(); ++i)
		{
			const D2D1_RECT_F& r = menu_item_rects_[i];
			if (x >= r.left && x < r.right && y >= r.top && y < r.bottom)
			{
				const MenuEntry& e = def.entries[i];
				menu_close();
				if (!e.separator && e.enabled && e.cmd != 0)
					on_command(e.cmd);
				return true;
			}
		}
		return true;
	}

	// Клик мимо — закрыть меню.
	menu_close();
	return true;
}

void App::menu_key(WPARAM vk)
{
	if (open_menu_ < 0)
	{
		if (vk == VK_F10)
		{
			open_menu_ = 0;
			hot_item_ = 0;
			layout_menus();
			invalidate();
		}
		return;
	}
	const MenuDef& def = menus_[static_cast<std::size_t>(open_menu_)];
	const auto count = static_cast<int>(def.entries.size());
	switch (vk)
	{
	case VK_ESCAPE:
		menu_close();
		break;
	case VK_LEFT:
	case VK_RIGHT:
	{
		const auto n = static_cast<int>(menus_.size());
		open_menu_ = (open_menu_ + (vk == VK_RIGHT ? 1 : n - 1)) % n;
		hot_item_ = -1;
		layout_menus();
		invalidate();
		break;
	}
	case VK_UP:
	case VK_DOWN:
	{
		if (count == 0)
			break;
		int next = hot_item_;
		do
		{
			next = (next + (vk == VK_DOWN ? 1 : count - 1) + count) % count;
		} while (def.entries[static_cast<std::size_t>(next)].separator ||
				 !def.entries[static_cast<std::size_t>(next)].enabled);
		hot_item_ = next;
		invalidate();
		break;
	}
	case VK_RETURN:
		if (hot_item_ >= 0 && hot_item_ < count)
		{
			const MenuEntry& e = def.entries[static_cast<std::size_t>(hot_item_)];
			menu_close();
			if (!e.separator && e.enabled && e.cmd != 0)
				on_command(e.cmd);
		}
		break;
	default:
		break;
	}
}

void App::setup_accels()
{
	// Сочетания — из Main.dfm оригинала. Дубли (Ctrl+O для «Загрузить текст»,
	// Ctrl+, для «Опции», Ctrl+Shift+S для «Сохранить как») оставлены как
	// наши удобные алиасы: все «родные» сочетания работают как в оригинале.
	ACCEL accels[] = {
		// Файл
		{ FCONTROL | FVIRTKEY, 'L', kCmdOpen },
		{ FCONTROL | FVIRTKEY, 'O', kCmdOpen },
		{ FCONTROL | FALT | FVIRTKEY, 'S', kCmdSave },
		{ FCONTROL | FVIRTKEY, 'S', kCmdSaveAs },
		{ FCONTROL | FSHIFT | FVIRTKEY, 'S', kCmdSaveAs },
		{ FCONTROL | FVIRTKEY, 'N', kCmdNew },
		{ FCONTROL | FSHIFT | FVIRTKEY, 'R', kCmdReload },
		{ FCONTROL | FSHIFT | FVIRTKEY, 'D', kCmdDictEditor },
		{ FCONTROL | FVIRTKEY, 'P', kCmdFileProp },
		// Редактировать
		{ FCONTROL | FVIRTKEY, 'X', kCmdCut },
		{ FCONTROL | FVIRTKEY, 'C', kCmdCopy },
		{ FCONTROL | FVIRTKEY, 'V', kCmdPaste },
		{ FCONTROL | FVIRTKEY, 'A', kCmdSelectAll },
		{ FCONTROL | FVIRTKEY, 'Z', kCmdUndo },
		{ FCONTROL | FVIRTKEY, 'Y', kCmdRedo },
		{ FCONTROL | FVIRTKEY, 'F', kCmdFind },
		{ FCONTROL | FVIRTKEY, 'R', kCmdFindRegex },
		{ FVIRTKEY, VK_F3, kCmdFindNext },
		{ FSHIFT | FVIRTKEY, VK_F3, kCmdFindPrev },
		{ FCONTROL | FVIRTKEY, 'H', kCmdReplace },
		{ FCONTROL | FVIRTKEY, 'W', kCmdYofyNext },
		{ FCONTROL | FVIRTKEY, 'Q', kCmdYofyPrev },
		// Ёфикация
		{ FVIRTKEY, VK_F9, kCmdYofify },
		{ FCONTROL | FVIRTKEY, VK_F9, kCmdYofyFromStart },
		{ FSHIFT | FVIRTKEY, VK_F10, kCmdClearMarks },
		{ FCONTROL | FSHIFT | FVIRTKEY, 'E', kCmdAskWord },
		// Вид
		{ FCONTROL | FVIRTKEY, '0', kCmdFontReset },
		{ FCONTROL | FSHIFT | FVIRTKEY, 'L', kCmdLineNumbers },
		{ FCONTROL | FSHIFT | FVIRTKEY, 'W', kCmdWordWrap },
		{ FCONTROL | FVIRTKEY, VK_OEM_PLUS, kCmdFontBigger },
		{ FCONTROL | FVIRTKEY, VK_OEM_MINUS, kCmdFontSmaller },
		// Настройки и помощь
		{ FCONTROL | FALT | FVIRTKEY, 'O', kCmdOptions },
		{ FCONTROL | FVIRTKEY, VK_OEM_COMMA, kCmdOptions },
		{ FVIRTKEY, VK_F1, kCmdAbout },
		// Последние файлы — Ctrl+1…9
		{ FCONTROL | FVIRTKEY, '1', kCmdMru1 },
		{ FCONTROL | FVIRTKEY, '2', kCmdMru1 + 1 },
		{ FCONTROL | FVIRTKEY, '3', kCmdMru1 + 2 },
		{ FCONTROL | FVIRTKEY, '4', kCmdMru1 + 3 },
		{ FCONTROL | FVIRTKEY, '5', kCmdMru1 + 4 },
		{ FCONTROL | FVIRTKEY, '6', kCmdMru1 + 5 },
		{ FCONTROL | FVIRTKEY, '7', kCmdMru1 + 6 },
		{ FCONTROL | FVIRTKEY, '8', kCmdMru1 + 7 },
		{ FCONTROL | FVIRTKEY, '9', kCmdMru1 + 8 },
	};
	if (accel_ != nullptr)
		DestroyAcceleratorTable(accel_);
	accel_ = CreateAcceleratorTableW(accels, static_cast<int>(std::size(accels)));
}

void App::update_menu_checks()
{
	// Доступность пунктов — по SetControls/EnableMenu оригинала: она зависит
	// от наличия текста, выделения, словаря, сделанных замен и ответов
	// «Всегда Е/Ё».
	const bool has_text = buf_ != nullptr && buf_->size() != 0;
	const bool has_sel = ed_ != nullptr && ed_->has_selection();
	const bool can_undo = buf_ != nullptr && buf_->can_undo();
	const bool can_redo = buf_ != nullptr && buf_->can_redo();
	// DicExist оригинала: словарь загружен (это делается в on_create, поэтому
	// «Ёфицировать» доступна сразу после старта, как в оригинале).
	const bool has_dict = dict_ != nullptr;
	const bool has_marks = !yofy_marks_.empty();
	const bool has_usersel = dict_ != nullptr && dict_->has_user_sel();

	for (MenuDef& def : menus_)
	{
		for (MenuEntry& e : def.entries)
		{
			switch (e.cmd)
			{
			case kCmdUndo:
				e.enabled = can_undo;
				break;
			case kCmdRedo:
				e.enabled = can_redo;
				break;
			case kCmdCut:
			case kCmdCopy:
				e.enabled = has_text && has_sel;
				break;
			case kCmdPaste:
				e.enabled = !readonly_;
				break;
			case kCmdSave:
				// Save1 оригинала: текст есть и у файла уже есть имя.
				e.enabled = has_text && !path_.empty();
				break;
			case kCmdSaveAs:
			case kCmdNew:
			case kCmdSelectAll:
			case kCmdFind:
			case kCmdFindRegex:
			case kCmdFileProp:
				e.enabled = has_text;
				break;
			case kCmdFindNext:
				e.enabled = find_index_ != 0;
				break;
			case kCmdYofify:
			case kCmdYofyFromStart:
				e.enabled = has_text && has_dict;
				break;
			case kCmdYofyNext:
			case kCmdYofyPrev:
			case kCmdClearMarks:
				e.enabled = has_marks;
				break;
			case kCmdClearMru:
				e.enabled = !mru_.empty();
				break;
			case kCmdClearUserSel:
				e.enabled = has_usersel;
				break;
			case kCmdEncCp1251:
				e.checked = enc_ == Encoding::cp1251;
				break;
			case kCmdEncUtf8:
				e.checked = enc_ == Encoding::utf8 && !bom_;
				break;
			case kCmdEncUtf8Bom:
				e.checked = enc_ == Encoding::utf8 && bom_;
				break;
			case kCmdEncUtf16le:
				e.checked = enc_ == Encoding::utf16le;
				break;
			case kCmdEncUtf16be:
				e.checked = enc_ == Encoding::utf16be;
				break;
			case kCmdEolLf:
				e.checked = eol_ == Eol::lf;
				break;
			case kCmdEolCrlf:
				e.checked = eol_ == Eol::crlf;
				break;
			case kCmdEolCr:
				e.checked = eol_ == Eol::cr;
				break;
			case kCmdAlwaysAsk:
				e.checked = always_ask_;
				break;
			case kCmdSilentYofy:
				e.checked = silent_yofy_;
				break;
			case kCmdLineNumbers:
				e.checked = show_numbers_;
				break;
			case kCmdWordWrap:
				e.checked = word_wrap_;
				break;
			default:
				break;
			}
		}
	}
}

void App::sync_metrics()
{
	Metrics m;
	m.char_width = renderer_.char_width();
	m.line_height = renderer_.line_height();
	m.padding_x = 0.0F;
	ed_->set_metrics(m);
	ed_->set_word_wrap(word_wrap_); // настройка переноса живёт в yo.ini
}

void App::invalidate()
{
	if (hwnd_ != nullptr)
		InvalidateRect(hwnd_, nullptr, FALSE);
}

void App::refresh_view()
{
	sync_metrics();
	// Курсор после смены шрифта/документа должен быть виден — делаем это
	// явно: set_viewport/set_metrics больше не трогают прокрутку.
	if (ed_)
		ed_->ensure_caret_visible();
	update_scrollbars();
	update_title();
	update_menu_checks();
	layout_menus();
	invalidate();
}

void App::update_title()
{
	std::wstring title = path_.empty() ? L"Без имени" : path_.filename().wstring();
	if (buf_ && ed_ && buf_->modified())
		title += L" *";
	title += L" — Ё (yo-c-)";
	SetWindowTextW(hwnd_, title.c_str());
}

void App::update_scrollbars(bool redraw)
{
	if (hwnd_ == nullptr || !ed_)
		return;
	SCROLLINFO sv{};
	sv.cbSize = sizeof(sv);
	sv.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
	sv.nMin = 0;
	// При переносе по словам полоса меряется визуальными рядами, а не
	// логическими строками (иначе бегунок «прыгал» бы).
	const std::size_t total_rows = ed_->word_wrap() ? ed_->row_count() : buf_->line_count();
	sv.nMax = static_cast<int>(std::max<std::size_t>(1, total_rows) - 1);
	sv.nPage = static_cast<UINT>(std::max<std::size_t>(1, ed_->visible_row_count()));
	sv.nPos = static_cast<int>(ed_->first_visible_row());
	SetScrollInfo(hwnd_, SB_VERT, &sv, redraw ? TRUE : FALSE);

	const float cw = std::max(1.0F, renderer_.char_width());
	SCROLLINFO sh{};
	sh.cbSize = sizeof(sh);
	sh.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
	sh.nMin = 0;
	sh.nMax = static_cast<int>(std::max(1.0F, ed_->text_width() / cw));
	sh.nPage = static_cast<UINT>(std::max(1.0F, renderer_.text_area_width() / cw));
	sh.nPos = static_cast<int>(ed_->scroll_x() / cw);
	SetScrollInfo(hwnd_, SB_HORZ, &sh, redraw ? TRUE : FALSE);
}

std::string App::insert_eol_bytes() const { return std::string(eol_bytes(eol_)); }

std::wstring App::status_text() const
{
	if (!ed_)
		return {};
	const CaretPos pos = ed_->caret_pos();
	std::wstring text;
	text += L"Стр. ";
	text += std::to_wstring(pos.line);
	text += L", Стлб. ";
	text += std::to_wstring(pos.column);
	text += L"   ";
	text += encoding_name(enc_, bom_);
	text += L"   ";
	text += eol_name(eol_);
	text += L"   ";
	text += buf_->modified() ? L"изменён" : L"сохранён";
	text += L"   ";
	text += format_size(buf_->size());
	if (readonly_)
		text += L"   [только чтение]";
	if (yofy_busy_)
	{
		text += L"   ёфикация: строка ";
		text += std::to_wstring(yofy_line_ + 1);
		if (yofy_lines_ > 0)
		{
			text += L" из ";
			text += std::to_wstring(yofy_lines_);
			text += L" (";
			text += std::to_wstring(std::min<std::size_t>(100, (yofy_line_ + 1) * 100 / yofy_lines_));
			text += L"%)";
		}
		if (ask_count_ > 0)
		{
			text += L", вопросов ";
			text += std::to_wstring(ask_count_);
		}
		text += L"   Esc — прервать";
	}
	if (last_yofy_.lines > 0)
	{
		text += L"   ёфикация: замен ";
		text += std::to_wstring(last_yofy_.replaced);
		text += L", строк ";
		text += std::to_wstring(last_yofy_.lines);
		if (last_yofy_.reverted > 0)
		{
			text += L", откатов ";
			text += std::to_wstring(last_yofy_.reverted);
		}
		if (last_yofy_.cancelled)
			text += L" (прервано)";
		// Счётчики прохода — как «t=… A=… RE=… REQ:% E:… T:… S:…» в ShowStatus
		// оригинала (§4.7). Хинт панели расшифровывает поля.
		if (last_stats_.checked.a > 0)
		{
			text += L"   t=";
			text += std::to_wstring(last_stats_.checked.a);
			text += L"  A=";
			text += std::to_wstring(last_stats_.asked.a);
			text += L"  RE=";
			text += std::to_wstring(last_stats_.regex_resolved.a);
			const std::uint64_t re_total = last_stats_.regex_resolved.a + last_stats_.no_regex;
			if (re_total > 0)
			{
				text += L"  REQ:";
				text += std::to_wstring(last_stats_.regex_resolved.a * 100 / re_total);
				text += L"%";
			}
			text += L"  E:";
			text += std::to_wstring(last_stats_.wrong_yo);
			if (last_yofy_ms_ > 0.5)
			{
				wchar_t buffer[64] = L"";
				const double seconds = last_yofy_ms_ / 1000.0;
				swprintf(buffer, std::size(buffer), L"  T:%.2f", seconds);
				text += buffer;
				if (seconds > 0.001 && last_yofy_.replaced > 0)
				{
					swprintf(buffer, std::size(buffer), L"  S:%.0f",
							 static_cast<double>(last_yofy_.replaced) / seconds);
					text += buffer;
				}
			}
		}
	}
	if (show_find_)
	{
		text += L"   поиск";
		if (find_regex_)
			text += L" (РВ)";
		text += L": ";
		if (!find_error_.empty())
			text += find_error_;
		else if (find_query_.empty())
			text += L"введите образец, Enter — найти, Esc — закрыть";
		else
		{
			text += std::to_wstring(find_index_);
			text += L" из ";
			text += std::to_wstring(find_total_);
		}
	}
	return text;
}

// ---------------------------------------------------------------- отрисовка

void App::on_paint()
{
	PAINTSTRUCT ps{};
	BeginPaint(hwnd_, &ps);
	RECT rc{};
	GetClientRect(hwnd_, &rc);
	renderer_.resize(static_cast<UINT>(std::max<LONG>(1, rc.right)), static_cast<UINT>(std::max<LONG>(1, rc.bottom)));
	sync_metrics();
	const AskView view = ask_view();
	MenuBarView bar_view = menu_bar_view();
	find_sync_view();
	const auto paint_started = std::chrono::steady_clock::now();
	options_sync_view();
	dict_sync_view();
	// Подсветка слова под вопросом живёт весь проход (между вопросами тоже),
	// поэтому она не мигает и не «пропадает» на глазах.
	const std::pair<std::size_t, std::size_t> word_hl =
		ask_.has_word ? std::pair<std::size_t, std::size_t>{ ask_.word_from, ask_.word_to }
					  : std::pair<std::size_t, std::size_t>{ 0, 0 };
	renderer_.paint(*ed_, status_text(), show_find_ ? &find_view_ : nullptr, caret_on_,
					ask_.active && ask_.has_panel ? &view : nullptr, &bar_view, find_marks_, yofy_marks_,
					options_open_ ? &options_view_ : nullptr, dict_open_ ? &dict_view_ : nullptr,
					word_hl);
	const double paint_ms =
		std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - paint_started).count();
	if (paint_ms > 16.0)
		trace("отрисовка", paint_ms); // дольше кадра — повод посмотреть, что дорого
	// Рендер мог изменить размеры видимой области — синхронизируем полосы,
	// не порождая новых перерисовок.
	update_scrollbars(false);
	EndPaint(hwnd_, &ps);
}

void App::on_size()
{
	RECT rc{};
	GetClientRect(hwnd_, &rc);
	renderer_.resize(static_cast<UINT>(std::max<LONG>(1, rc.right)), static_cast<UINT>(std::max<LONG>(1, rc.bottom)));
	sync_metrics();
	// Окно могло стать меньше — курсор должен остаться видимым (при прокрутке
	// колесом или полосой мы его, наоборот, не трогаем).
	if (ed_)
		ed_->ensure_caret_visible();
	if (ask_.active)
		ask_build_view(); // панель пересчитывается под новый размер окна
	update_scrollbars();
	invalidate();
}

void App::on_dpi_changed(WPARAM wp, LPARAM lp)
{
	const UINT dpi = HIWORD(wp);
	dpi_scale_ = dpi > 0 ? static_cast<float>(dpi) / 96.0F : 1.0F;
	renderer_.set_dpi(dpi_scale_);
	const auto* rect = reinterpret_cast<const RECT*>(lp);
	if (rect != nullptr)
		SetWindowPos(hwnd_, nullptr, rect->left, rect->top, rect->right - rect->left, rect->bottom - rect->top,
					 SWP_NOZORDER | SWP_NOACTIVATE);
	refresh_view();
}

void App::on_timer()
{
	caret_on_ = !caret_on_;
	invalidate();
}

// ---------------------------------------------------------------- ввод

void App::on_char(wchar_t ch)
{
	// Панель редактора словаря перехватывает ввод целиком — документ в это
	// время не редактируется (и readonly файла здесь ни при чём).
	if (dict_open_)
	{
		(void)dict_char(ch);
		return;
	}
	if (readonly_)
		return;

	// Суррогатные пары (эмодзи и прочее за пределами BMP).
	const auto code = static_cast<char32_t>(ch);
	if (code >= 0xD800U && code <= 0xDBFFU)
	{
		pending_high_ = code;
		return;
	}
	if (code >= 0xDC00U && code <= 0xDFFFU)
	{
		if (pending_high_ != 0)
		{
			const char32_t cp = 0x10000U + ((pending_high_ - 0xD800U) << 10) + (code - 0xDC00U);
			pending_high_ = 0;
			ed_->insert_char(cp);
		}
		return;
	}
	pending_high_ = 0;

	if (options_open_)
		return; // в окне настроек нет полей ввода
	if (find_char(ch))
		return;

	if (ch == L'\r' || ch == L'\n')
	{
		ed_->insert_text(insert_eol_bytes());
	}
	else if (ch == L'\t')
	{
		ed_->insert_text("\t");
	}
	else if (ch >= 0x20)
	{
		const char16_t u16[2] = { static_cast<char16_t>(ch), 0 };
		ed_->insert_text(utf16_to_utf8(std::u16string_view(u16, 1)));
	}
	else
	{
		return; // прочие управляющие — не текст
	}
	caret_on_ = true;
	update_scrollbars();
	update_title();
	invalidate();
}

void App::on_key_down(WPARAM vk, bool ctrl, bool shift)
{
	if (dict_key(vk, ctrl, shift))
		return;
	if (options_key(vk, ctrl, shift))
		return;
	if (find_key(vk, ctrl, shift))
		return;
	// Сочетания обрабатываем и здесь, а не только через таблицу акселераторов:
	// TranslateAccelerator легко «теряет» нажатия (например, когда фокус у
	// диалога или таблица не создалась), и тогда Ctrl+C/V/Z молча не работают.
	if (ctrl && !ask_.active)
	{
		// Раскладка — та же, что в setup_accels() (Main.dfm оригинала):
		// Ctrl+R — «Найти регулярное выражение», Ctrl+S — «Сохранить текст как»,
		// Ctrl+Shift+R — перечитать с диска.
		switch (vk)
		{
		case 'N':
			on_command(kCmdNew);
			return;
		case 'L':
		case 'O':
			on_command(kCmdOpen);
			return;
		case 'R':
			on_command(shift ? kCmdReload : kCmdFindRegex);
			return;
		case 'S':
			on_command(kCmdSaveAs);
			return;
		case 'Z':
			on_command(kCmdUndo);
			return;
		case 'Y':
			on_command(kCmdRedo);
			return;
		case 'X':
			on_command(kCmdCut);
			return;
		case 'C':
			on_command(kCmdCopy);
			return;
		case 'V':
			on_command(kCmdPaste);
			return;
		case 'A':
			on_command(kCmdSelectAll);
			return;
		case 'E':
			// Ctrl+Shift+E — «спросить про слово», Ctrl+E — наш алиас
			// ёфикации (в оригинале её запускают F9 / Ctrl+F9).
			on_command(shift ? kCmdAskWord : kCmdYofify);
			return;
		case 'D':
			if (shift)
			{
				on_command(kCmdDictEditor);
				return;
			}
			break;
		case 'P':
			on_command(kCmdFileProp);
			return;
		case 'W':
			on_command(shift ? kCmdWordWrap : kCmdYofyNext);
			return;
		case 'Q':
			on_command(kCmdYofyPrev);
			return;
		case 'H':
			on_command(kCmdReplace);
			return;
		case 'F':
			if (!shift)
			{
				on_command(kCmdFind);
				return;
			}
			break;
		case VK_OEM_PLUS:
		case VK_ADD:
			on_command(kCmdFontBigger);
			return;
		case VK_OEM_MINUS:
		case VK_SUBTRACT:
			on_command(kCmdFontSmaller);
			return;
		default:
			break;
		}
	}
	if (vk == VK_F3 && !ask_.active)
	{
		find_next();
		return;
	}
	if (vk == VK_F9 && !ask_.active)
	{
		// F9 / Ctrl+F9 — как в оригинале (Yopos1 / Yobegin1).
		run_yofification(ctrl);
		return;
	}

	const bool extend = shift;
	switch (vk)
	{
	case VK_LEFT:
		ed_->move_left(extend, ctrl);
		break;
	case VK_RIGHT:
		ed_->move_right(extend, ctrl);
		break;
	case VK_UP:
		ed_->move_up(extend);
		break;
	case VK_DOWN:
		ed_->move_down(extend);
		break;
	case VK_HOME:
		if (ctrl)
			ed_->move_doc_start(extend);
		else
			ed_->move_line_start(extend);
		break;
	case VK_END:
		if (ctrl)
			ed_->move_doc_end(extend);
		else
			ed_->move_line_end(extend);
		break;
	case VK_PRIOR:
		ed_->move_page(-1, extend);
		break;
	case VK_NEXT:
		ed_->move_page(1, extend);
		break;
	case VK_BACK:
		if (!readonly_)
			ed_->backspace();
		break;
	case VK_DELETE:
		if (!readonly_)
			ed_->delete_forward();
		break;
	default:
		return;
	}
	caret_on_ = true;
	update_scrollbars();
	update_title();
	invalidate();
}

// ---------------------------------------------------------------- мышь

std::size_t App::caret_offset_at(float x, float y) const
{
	// Текст начинается ниже строки меню: без этой поправки курсор вставал
	// выше клика на bar_h_ (полторы строки при обычном шрифте).
	return ed_->offset_at_client(x, y - renderer_.bar_height(), renderer_.padding());
}

void App::on_lbutton_down(float x, float y, bool extend)
{
	if (show_find_)
		find_focus_ = -1; // печать снова идёт в документ

	// Экран → документ живёт в ядре (offset_at_client): при прокрутке
	// прокрутку нужно ПРИБАВЛЯТЬ, иначе курсор встаёт выше клика на
	// 2*scroll_y/lh строк и выделение «съезжает» на несколько строк.
	ed_->set_caret(caret_offset_at(x, y), extend);
	dragging_ = true;
	SetCapture(hwnd_);
	caret_on_ = true;
	update_scrollbars();
	invalidate();
}

void App::on_mouse_move(float x, float y)
{
	if (!dragging_)
		return;
	ed_->set_caret(caret_offset_at(x, y), true);
	update_scrollbars();
	invalidate();
}

void App::on_mouse_up()
{
	dragging_ = false;
	ReleaseCapture();
}

void App::on_mouse_wheel(int delta)
{
	// Колёса с мелким шагом присылают дельты меньше WHEEL_DELTA (тачпады):
	// без накопления такие события терялись целиком, и прокрутка «не работала».
	wheel_accum_ += -delta;
	const int steps = wheel_accum_ / WHEEL_DELTA;
	if (steps == 0)
		return;
	wheel_accum_ -= steps * WHEEL_DELTA;

	if (GetKeyState(VK_CONTROL) < 0)
	{
		// Ctrl+колесо — масштаб, как в браузерах.
		on_command(steps > 0 ? kCmdFontBigger : kCmdFontSmaller);
		return;
	}
	if (GetKeyState(VK_SHIFT) < 0)
	{
		// Shift+колесо — горизонтальная прокрутка.
		const float cw = std::max(1.0F, renderer_.char_width());
		ed_->scroll_by(static_cast<float>(steps) * 3.0F * cw, 0.0F);
	}
	else
	{
		ed_->scroll_lines(static_cast<long>(steps) * 3L);
	}
	update_scrollbars();
	invalidate();
}

void App::on_vscroll(int code)
{
	SCROLLINFO si{};
	si.cbSize = sizeof(si);
	si.fMask = SIF_ALL;
	GetScrollInfo(hwnd_, SB_VERT, &si);
	long target = static_cast<long>(ed_->first_visible_row());
	const long page = std::max<long>(1, static_cast<long>(si.nPage));
	switch (code)
	{
	case SB_LINEUP:
		target -= 1;
		break;
	case SB_LINEDOWN:
		target += 1;
		break;
	case SB_PAGEUP:
		target -= page;
		break;
	case SB_PAGEDOWN:
		target += page;
		break;
	case SB_THUMBTRACK:
	case SB_THUMBPOSITION:
		target = si.nTrackPos;
		break;
	case SB_TOP:
		target = 0;
		break;
	case SB_BOTTOM:
		target = static_cast<long>(ed_->word_wrap() ? ed_->row_count() : buf_->line_count());
		break;
	default:
		return;
	}
	// Шаг — в рядах: без переноса ряд совпадает со строкой.
	ed_->scroll_by(0.0F, static_cast<float>(target - static_cast<long>(ed_->first_visible_row())) *
							 std::max(1.0F, ed_->metrics().line_height));
	update_scrollbars();
	invalidate();
}

void App::on_hscroll(int code)
{
	SCROLLINFO si{};
	si.cbSize = sizeof(si);
	si.fMask = SIF_ALL;
	GetScrollInfo(hwnd_, SB_HORZ, &si);
	const float cw = std::max(1.0F, renderer_.char_width());
	long target = si.nPos;
	const long page = std::max<long>(1, static_cast<long>(si.nPage));
	switch (code)
	{
	case SB_LINELEFT:
		target -= 1;
		break;
	case SB_LINERIGHT:
		target += 1;
		break;
	case SB_PAGELEFT:
		target -= page;
		break;
	case SB_PAGERIGHT:
		target += page;
		break;
	case SB_THUMBTRACK:
	case SB_THUMBPOSITION:
		target = si.nTrackPos;
		break;
	case SB_LEFT:
		target = 0;
		break;
	default:
		return;
	}
	ed_->scroll_by(static_cast<float>(target) * cw - ed_->scroll_x(), 0.0F);
	update_scrollbars();
	invalidate();
}

// ---------------------------------------------------------------- контекстное меню

void App::on_context_menu(int x, int y)
{
	if (ask_.active)
		return;

	// WM_CONTEXTMENU присылает ЭКРАННЫЕ координаты в физических пикселях:
	// масштабировать их ещё раз нельзя, иначе меню уезжает вниз-вправо.
	const float scale = std::max(1.0F, renderer_.dpi_scale());
	POINT pt{ static_cast<LONG>(x), static_cast<LONG>(y) };
	if (x == -1 && y == -1)
	{
		// Меню с клавиатуры (Shift+F10 / клавиша «меню»): ставим у курсора.
		const CaretPos pos = ed_->caret_pos();
		// Координаты каретки — в DIP, а ClientToScreen ждёт физические пиксели.
		POINT client{};
		client.x = static_cast<LONG>((renderer_.padding() + ed_->caret_x() - ed_->scroll_x()) * scale);
		client.y = static_cast<LONG>((renderer_.bar_height() + ed_->line_top(pos.line - 1) - ed_->scroll_y() +
									  renderer_.line_height()) *
									scale);
		pt = client;
		ClientToScreen(hwnd_, &pt);
	}

	const bool has_sel = ed_->has_selection();
	const bool can_undo = buf_->can_undo();

	const HMENU menu = CreatePopupMenu();
	AppendMenuW(menu, MF_STRING | (can_undo ? 0U : MF_GRAYED), kCmdUndo, L"Отменить\tCtrl+Z");
	AppendMenuW(menu, MF_STRING | (buf_->can_redo() ? 0U : MF_GRAYED), kCmdRedo, L"Повторить\tCtrl+Y");
	AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
	AppendMenuW(menu, MF_STRING | (has_sel ? 0U : MF_GRAYED), kCmdCut, L"Вырезать\tCtrl+X");
	AppendMenuW(menu, MF_STRING | (has_sel ? 0U : MF_GRAYED), kCmdCopy, L"Копировать\tCtrl+C");
	AppendMenuW(menu, MF_STRING | (readonly_ ? MF_GRAYED : 0U), kCmdPaste, L"Вставить\tCtrl+V");
	AppendMenuW(menu, MF_STRING, kCmdSelectAll, L"Выделить всё\tCtrl+A");
	AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
	AppendMenuW(menu, MF_STRING, kCmdYofify, L"Ёфицировать\tCtrl+E");
	AppendMenuW(menu, MF_STRING, kCmdAskWord, L"Спросить про слово под курсором");
	AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
	AppendMenuW(menu, MF_STRING, kCmdFind, L"Найти…\tCtrl+F");
	AppendMenuW(menu, MF_STRING, kCmdReplace, L"Заменить…\tCtrl+H");
	AppendMenuW(menu, MF_STRING, kCmdFindNext, L"Найти далее\tF3");

	SetForegroundWindow(hwnd_); // иначе меню не закрывается по клику мимо
	TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_LEFTALIGN | TPM_TOPALIGN, pt.x, pt.y, 0, hwnd_,
				   nullptr);
	PostMessageW(hwnd_, WM_NULL, 0, 0); // известная «заплатка» меню Win32
	DestroyMenu(menu);
}

// ---------------------------------------------------------------- команды

void App::on_command(int id)
{
	// Редактор словаря — модальное наложение: пока он открыт, команды
	// документа не выполняются (иначе Ctrl+S сохранил бы файл, а не словарь).
	if (dict_open_)
	{
		switch (id)
		{
		case kCmdDictEditor:
			dict_editor_close();
			break;
		case kCmdSave:
			dict_save();
			break;
		default:
			break;
		}
		return;
	}

	if (id >= kCmdMru1 && id < kCmdMru1 + static_cast<int>(kMruMax))
	{
		open_mru(static_cast<std::size_t>(id - kCmdMru1));
		return;
	}
	switch (id)
	{
	case kCmdOpen:
		open_dialog();
		break;
	case kCmdSave:
		save();
		break;
	case kCmdSaveAs:
		save_as();
		break;
	case kCmdExit:
		PostMessageW(hwnd_, WM_CLOSE, 0, 0);
		break;
	case kCmdUndo:
		if (ed_->buffer().undo())
			refresh_view();
		break;
	case kCmdRedo:
		if (ed_->buffer().redo())
			refresh_view();
		break;
	case kCmdCut:
		clipboard_copy(true);
		break;
	case kCmdCopy:
		clipboard_copy(false);
		break;
	case kCmdPaste:
		clipboard_paste();
		break;
	case kCmdSelectAll:
		ed_->select_all();
		invalidate();
		break;
	case kCmdFind:
		find_toggle(false);
		break;
	case kCmdReplace:
		find_toggle(true);
		break;
	case kCmdFindNext:
		find_next();
		break;
	case kCmdFindPrev:
		find_prev();
		break;
	case kCmdFindRegex:
		find_regex_ = true;
		if (!show_find_)
			find_toggle(false);
		find_sync_view();
		invalidate();
		break;
	case kCmdYofyNext:
		yofy_goto(true);
		break;
	case kCmdYofyPrev:
		yofy_goto(false);
		break;
	case kCmdClearMarks:
		clear_yofy_marks();
		break;
	case kCmdLineNumbers:
		toggle_line_numbers();
		break;
	case kCmdWordWrap:
		toggle_word_wrap();
		break;
	case kCmdDictEditor:
		dict_editor_open();
		break;
	case kCmdFileProp:
		show_file_props();
		break;
	case kCmdAbout:
		show_about();
		break;
	case kCmdHelpMore:
		show_help_more();
		break;
	case kCmdClearMru:
		clear_mru();
		break;
	case kCmdOptions:
		options_open_dialog();
		break;
	case kCmdOpenDict:
		if (pick_dictionary(false))
			reload_dictionaries();
		break;
	case kCmdOpenRegexDict:
		if (pick_dictionary(true))
			reload_dictionaries();
		break;
	case kCmdClearUserSel:
		clear_user_selection();
		break;
	case kCmdFontReset:
		if (renderer_.set_font_size(15.0F, hwnd_))
		{
			font_size_ = renderer_.font_size();
			save_settings();
			refresh_view();
		}
		break;
	case kCmdYofify:
		run_yofification(false); // F9 — с текущей позиции
		break;
	case kCmdYofyFromStart:
		run_yofification(true); // Ctrl+F9 — с начала текста
		break;
	case kCmdAlwaysAsk:
		set_always_ask(!always_ask_);
		save_settings(); // опция запоминается между запусками
		break;
	case kCmdFontBigger:
		if (renderer_.set_font_size(font_size_ + 1.0F, hwnd_))
		{
			font_size_ = renderer_.font_size();
			save_settings();
			refresh_view();
		}
		break;
	case kCmdFontSmaller:
		if (renderer_.set_font_size(font_size_ - 1.0F, hwnd_))
		{
			font_size_ = renderer_.font_size();
			save_settings();
			refresh_view();
		}
		break;
	case kCmdNew:
		if (ask_save_changes())
			new_document();
		break;
	case kCmdReload:
		reload_document();
		break;
	case kCmdEncCp1251:
		apply_encoding(Encoding::cp1251, false);
		break;
	case kCmdEncUtf8:
		apply_encoding(Encoding::utf8, false);
		break;
	case kCmdEncUtf8Bom:
		apply_encoding(Encoding::utf8, true);
		break;
	case kCmdEncUtf16le:
		apply_encoding(Encoding::utf16le, false);
		break;
	case kCmdEncUtf16be:
		apply_encoding(Encoding::utf16be, false);
		break;
	case kCmdEolLf:
		apply_eol(Eol::lf);
		break;
	case kCmdEolCrlf:
		apply_eol(Eol::crlf);
		break;
	case kCmdEolCr:
		apply_eol(Eol::cr);
		break;
	case kCmdAskWord:
		ask_current_word();
		break;
	case kCmdSilentYofy:
		set_silent_yofy(!silent_yofy_);
		save_settings();
		break;
	default:
		break;
	}
}

// ---------------------------------------------------------------- буфер обмена

void App::clipboard_copy(bool cut)
{
	std::string utf8;
	if (!(cut ? ed_->cut_selection(&utf8) : ed_->copy_selection(&utf8)))
		return;
	const std::wstring wide = widen(utf8);
	if (!OpenClipboard(hwnd_))
		return;
	EmptyClipboard();
	const SIZE_T bytes = (wide.size() + 1) * sizeof(wchar_t);
	HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes);
	if (mem != nullptr)
	{
		void* dst = GlobalLock(mem);
		if (dst != nullptr)
		{
			std::memcpy(dst, wide.c_str(), bytes);
			GlobalUnlock(mem);
			if (SetClipboardData(CF_UNICODETEXT, mem) == nullptr)
				GlobalFree(mem);
		}
		else
		{
			GlobalFree(mem);
		}
	}
	CloseClipboard();
	if (cut)
	{
		update_scrollbars();
		update_title();
		invalidate();
	}
}

void App::clipboard_paste()
{
	if (readonly_)
	{
		MessageBeep(MB_ICONASTERISK);
		return;
	}
	if (!OpenClipboard(hwnd_))
		return;
	std::string utf8;
	if (HANDLE data = GetClipboardData(CF_UNICODETEXT))
	{
		if (const auto* wide = static_cast<const wchar_t*>(GlobalLock(data)))
		{
			utf8 = wide_to_utf8(wide);
			GlobalUnlock(data);
		}
	}
	else if (HANDLE ansi = GetClipboardData(CF_TEXT))
	{
		// Часть программ (и консоль) кладёт только 8-битный текст.
		if (const auto* narrow = static_cast<const char*>(GlobalLock(ansi)))
		{
			const int need = MultiByteToWideChar(CP_ACP, 0, narrow, -1, nullptr, 0);
			if (need > 1)
			{
				std::wstring wide(static_cast<std::size_t>(need), L'\0');
				const int got = MultiByteToWideChar(CP_ACP, 0, narrow, -1, wide.data(), need);
				if (got > 0)
					utf8 = wide_to_utf8(std::wstring_view(wide.c_str()));
			}
			GlobalUnlock(ansi);
		}
	}
	CloseClipboard();
	if (utf8.empty())
	{
		MessageBeep(MB_ICONASTERISK);
		return;
	}

	// Перерисовка одна на всю вставку: иначе на каждый вставленный фрагмент
	// приходится полный цикл раскладки, и на длинном тексте окно «замирает».
	const std::string normalized = normalize_newlines(utf8, eol_);
	SendMessageW(hwnd_, WM_SETREDRAW, FALSE, 0);
	const auto started = std::chrono::steady_clock::now();
	const bool ok = ed_->paste(normalized);
	trace("вставка", std::chrono::duration<double, std::milli>(
						 std::chrono::steady_clock::now() - started).count(),
		  normalized.size());
	SendMessageW(hwnd_, WM_SETREDRAW, TRUE, 0);
	if (ok)
	{
		caret_on_ = true;
		refresh_view();
	}
	else
	{
		RedrawWindow(hwnd_, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN);
	}
}

// ---------------------------------------------------------------- поиск

namespace {

// Идентификаторы кнопок полосы поиска (клик и отрисовка идут по ним).
constexpr int kFindBtnNext = 1;
constexpr int kFindBtnPrev = 2;
constexpr int kFindBtnCase = 3;
constexpr int kFindBtnWord = 4;
constexpr int kFindBtnClose = 5;
constexpr int kFindBtnReplace = 6;
constexpr int kFindBtnReplaceAll = 7;

constexpr std::size_t kFindCountLimit = 100000; // больше не считаем — незачем

} // namespace

void App::find_toggle(bool replace_row)
{
	if (show_find_ && (!replace_row || find_replace_row_))
	{
		find_close();
		return;
	}
	show_find_ = true;
	if (replace_row)
		find_replace_row_ = true;
	find_focus_ = replace_row ? 1 : 0;
	// Образец из выделения — привычно: выделили слово, нажали Ctrl+F.
	if (find_query_.empty() && ed_ != nullptr && ed_->has_selection())
	{
		const std::size_t len = ed_->sel_end() - ed_->sel_begin();
		if (len > 0 && len < 200)
		{
			std::string text;
			buf_->read(ed_->sel_begin(), len, text);
			if (text.find('\n') == std::string::npos)
				find_query_ = text;
		}
	}
	find_sync_view();
	invalidate();
}

void App::find_close()
{
	show_find_ = false;
	find_replace_row_ = false;
	find_focus_ = 0;
	find_marks_.clear();
	find_hits_.clear();
	find_error_.clear();
	find_view_ = {};
	find_sync_view();
	invalidate();
}

void App::find_toggle_case()
{
	find_opts_.case_sensitive = !find_opts_.case_sensitive;
	find_count_hits();
	find_sync_view();
	invalidate();
}

void App::find_toggle_word()
{
	find_opts_.whole_word = !find_opts_.whole_word;
	find_count_hits();
	find_sync_view();
	invalidate();
}

void App::find_add_text(const std::string& utf8)
{
	std::string& field = find_focus_ == 1 && find_replace_row_ ? find_replace_text_ : find_query_;
	field += utf8;
	find_count_hits();
	find_sync_view();
	invalidate();
}

void App::find_backspace()
{
	std::string& field = find_focus_ == 1 && find_replace_row_ ? find_replace_text_ : find_query_;
	// Удаляем последний символ UTF-8 (кириллица — два байта).
	std::size_t cut = field.size();
	while (cut > 0 && (static_cast<unsigned char>(field[cut - 1]) & 0xC0U) == 0x80U)
		--cut;
	if (cut > 0)
		--cut;
	field.erase(cut);
	find_count_hits();
	find_sync_view();
	invalidate();
}

// Все совпадения в документе — для строки «N из M». Полный проход только по
// запросу (Enter/F3), а не на каждый введённый символ.
void App::find_count_hits()
{
	find_hits_.clear();
	find_error_.clear();
	if (buf_ == nullptr || find_query_.empty())
		return;
	if (find_regex_)
	{
		const FindRegexResult probe =
			yo::find_regex(*buf_, 0, find_query_, find_opts_.case_sensitive);
		find_error_ = widen(probe.error);
		return;
	}
	find_hits_ = yo::find_all(*buf_, 0, buf_->size(), find_query_, find_opts_, kFindCountLimit);
}

void App::find_run(bool forward)
{
	find_count_hits();
	if (find_query_.empty() || buf_ == nullptr || ed_ == nullptr)
	{
		find_sync_view();
		invalidate();
		return;
	}
	if (!find_error_.empty())
	{
		MessageBeep(MB_ICONASTERISK);
		find_sync_view();
		invalidate();
		return;
	}

	const std::size_t from =
		ed_->has_selection() ? (forward ? ed_->sel_end() : ed_->sel_begin()) : ed_->caret();
	std::size_t begin = std::string::npos;
	std::size_t end = std::string::npos;

	if (find_regex_)
	{
		FindRegexResult r = forward
			? yo::find_regex(*buf_, from, find_query_, find_opts_.case_sensitive)
			: yo::find_regex_prev(*buf_, from, find_query_, find_opts_.case_sensitive);
		if (!r.found() && !r.error.empty())
		{
			find_error_ = widen(r.error);
			MessageBeep(MB_ICONASTERISK);
			find_sync_view();
			invalidate();
			return;
		}
		if (!r.found())
		{
			// Дошли до края — переходим на другой (как F3 в большинстве
			// редакторов; оригинал не зацикливал, но так удобнее).
			r = forward ? yo::find_regex(*buf_, 0, find_query_, find_opts_.case_sensitive)
						: yo::find_regex_prev(*buf_, buf_->size(), find_query_, find_opts_.case_sensitive);
		}
		begin = r.begin;
		end = r.end;
	}
	else
	{
		begin = forward ? yo::find_next(*buf_, from, find_query_, find_opts_)
						: yo::find_prev(*buf_, from, find_query_, find_opts_);
		if (begin == std::string::npos)
			begin = forward ? yo::find_next(*buf_, 0, find_query_, find_opts_)
							: yo::find_prev(*buf_, buf_->size(), find_query_, find_opts_);
		if (begin != std::string::npos)
			end = begin + find_query_.size();
	}

	if (begin == std::string::npos || begin >= buf_->size())
	{
		find_index_ = 0;
		find_total_ = find_hits_.size();
		MessageBeep(MB_ICONASTERISK);
		find_sync_view();
		invalidate();
		return;
	}

	find_total_ = find_hits_.size();
	find_index_ = 0;
	if (!find_hits_.empty())
	{
		const auto it = std::lower_bound(find_hits_.begin(), find_hits_.end(), begin);
		if (it != find_hits_.end() && *it == begin)
			find_index_ = static_cast<std::size_t>(it - find_hits_.begin()) + 1;
	}

	ed_->set_caret(begin);
	ed_->set_caret(end, true);
	caret_on_ = true;
	ed_->ensure_caret_visible();
	update_scrollbars();
	find_sync_view();
	invalidate();
}

void App::find_next() { find_run(true); }

void App::find_prev() { find_run(false); }

void App::find_replace()
{
	if (find_query_.empty())
		return;
	if (readonly_)
	{
		MessageBeep(MB_ICONASTERISK);
		return;
	}
	// Заменяем только если выделено ровно найденное, иначе сначала ищем.
	const std::size_t sel_a = ed_->sel_begin();
	const std::size_t sel_b = ed_->sel_end();
	bool matched = false;
	if (ed_->has_selection())
	{
		if (find_regex_)
		{
			const FindRegexResult r = yo::find_regex(*buf_, sel_a, find_query_, find_opts_.case_sensitive);
			matched = r.found() && r.begin == sel_a && r.end == sel_b;
		}
		else if (sel_b - sel_a == find_query_.size())
		{
			std::string text;
			buf_->read(sel_a, sel_b - sel_a, text);
			const std::vector<std::size_t> hits = yo::find_in_line(text, find_query_, find_opts_);
			matched = !hits.empty() && hits.front() == 0;
		}
	}
	if (!matched)
	{
		find_run(true);
		return;
	}

	ed_->begin_group("Заменить");
	ed_->delete_selection();
	if (!find_replace_text_.empty())
		ed_->insert_text(find_replace_text_);
	ed_->end_group();
	ed_->set_caret(sel_a + find_replace_text_.size());
	caret_on_ = true;
	find_count_hits();
	// Сразу переходим к следующему вхождению — как «Заменить» в диалогах.
	find_run(true);
}

void App::find_replace_all()
{
	if (find_query_.empty() || readonly_)
	{
		MessageBeep(MB_ICONASTERISK);
		return;
	}
	find_count_hits();
	if (!find_error_.empty())
	{
		MessageBeep(MB_ICONASTERISK);
		invalidate();
		return;
	}
	std::size_t done = 0;
	if (find_regex_)
	{
		// Совпадения собираем заранее, заменяем с конца — смещения не едут.
		std::vector<std::pair<std::size_t, std::size_t>> hits;
		std::size_t pos = 0;
		while (pos < buf_->size() && hits.size() < kFindCountLimit)
		{
			const FindRegexResult r = yo::find_regex(*buf_, pos, find_query_, find_opts_.case_sensitive);
			if (!r.found())
				break;
			hits.emplace_back(r.begin, r.end);
			pos = r.end > r.begin ? r.end : r.end + 1;
		}
		if (!hits.empty())
		{
			ed_->begin_group("Заменить всё");
			for (std::size_t i = hits.size(); i > 0; --i)
			{
				buf_->erase(hits[i - 1].first, hits[i - 1].second - hits[i - 1].first);
				if (!find_replace_text_.empty())
					buf_->insert(hits[i - 1].first, find_replace_text_);
			}
			ed_->end_group();
			done = hits.size();
		}
	}
	else
	{
		done = yo::replace_all(*buf_, 0, buf_->size(), find_query_, find_replace_text_, find_opts_);
	}
	ed_->set_caret(ed_->has_selection() ? ed_->sel_begin() : ed_->caret());
	find_hits_.clear();
	find_index_ = 0;
	find_total_ = 0;
	refresh_view();
	find_sync_view();
	invalidate();
	if (done == 0)
		MessageBeep(MB_ICONASTERISK);
}

// Геометрия полосы и подсветка совпадений в видимых строках.
void App::find_sync_view()
{
	find_view_ = {};
	// Поле номеров строк (меню «Вид»).
	float gutter = 0.0F;
	if (show_numbers_ && buf_ != nullptr)
	{
		const std::size_t digits = std::to_string(std::max<std::size_t>(1, buf_->line_count())).size();
		gutter = static_cast<float>(digits) * std::max(4.0F, renderer_.char_width() * 0.7F) + 10.0F;
	}
	renderer_.set_gutter_width(gutter);

	if (!show_find_ || ed_ == nullptr)
	{
		find_marks_.clear();
		return;
	}

	const float pad = renderer_.padding();
	const float w = std::max(0.0F, renderer_.width_dip());
	const float rh = renderer_.find_height();
	const float bar_h = find_replace_row_ ? rh * 2.0F + 2.0F : rh;
	const float bottom = renderer_.height_dip() - renderer_.status_height();
	const float top = std::max(0.0F, bottom - bar_h);

	find_view_.rect = D2D1::RectF(0.0F, top, w, bottom);
	find_view_.h = bar_h;
	find_view_.replace_mode = find_replace_row_;
	find_view_.query = widen(find_query_);
	find_view_.replace = widen(find_replace_text_);
	find_view_.focus = find_focus_;

	const auto add_button = [&](std::wstring label, int id, float right, float row_top, bool toggle,
								bool checked)
	{
		const float bw = std::max(26.0F, renderer_.measure_ui(label) + 18.0F);
		FindBarButtonView b;
		b.rect = D2D1::RectF(std::max(0.0F, right - bw), row_top + 2.0F, right, row_top + rh - 2.0F);
		b.label = std::move(label);
		b.id = id;
		b.toggle = toggle;
		b.checked = checked;
		find_view_.buttons.push_back(b);
		return std::max(0.0F, right - bw);
	};

	float right = w - pad;
	right = add_button(L"×", kFindBtnClose, right, top, false, false) - 2.0F;
	right = add_button(L"Слово", kFindBtnWord, right, top, true, find_opts_.whole_word) - 2.0F;
	right = add_button(L"Aa", kFindBtnCase, right, top, true, find_opts_.case_sensitive) - 2.0F;
	right = add_button(L"Назад", kFindBtnPrev, right, top, false, false) - 2.0F;
	right = add_button(L"Далее", kFindBtnNext, right, top, false, false) - 4.0F;
	find_view_.query_rect = D2D1::RectF(pad, top + 3.0F, std::max(pad + 30.0F, right), top + rh - 3.0F);

	if (find_replace_row_)
	{
		const float y2 = top + rh + 2.0F;
		float r2 = w - pad;
		r2 = add_button(L"Заменить всё", kFindBtnReplaceAll, r2, y2, false, false) - 2.0F;
		r2 = add_button(L"Заменить", kFindBtnReplace, r2, y2, false, false) - 4.0F;
		find_view_.replace_rect =
			D2D1::RectF(pad, y2 + 3.0F, std::max(pad + 30.0F, r2), y2 + rh - 3.0F);
	}

	find_collect_marks();
}

void App::find_collect_marks()
{
	find_marks_.clear();
	if (!show_find_ || find_query_.empty() || ed_ == nullptr || buf_ == nullptr || !find_error_.empty())
		return;
	const std::size_t first = ed_->first_visible_line();
	const std::size_t count = ed_->visible_line_count() + 1;
	const std::size_t lines = buf_->line_count();
	for (std::size_t i = first; i < first + count && i < lines; ++i)
	{
		if (find_regex_)
			continue; // РВ подсвечиваем только как найденное выделение
		const std::string_view text = buf_->line_text(i);
		const std::size_t base = buf_->line_start(i);
		for (const std::size_t at : yo::find_in_line(text, find_query_, find_opts_))
			find_marks_.emplace_back(base + at, base + at + find_query_.size());
	}
}

bool App::find_click(float x, float y)
{
	if (!show_find_)
		return false;
	find_sync_view(); // геометрия могла устареть после смены размера окна
	const D2D1_RECT_F& r = find_view_.rect;
	if (x < r.left || x > r.right || y < r.top || y > r.bottom)
	{
		// Клик по тексту закрывает полосу? Нет: она не модальна, но и не
		// должна «съедать» клик — возвращаем false, работаем с текстом.
		return false;
	}
	for (const FindBarButtonView& b : find_view_.buttons)
	{
		if (x < b.rect.left || x > b.rect.right || y < b.rect.top || y > b.rect.bottom)
			continue;
		switch (b.id)
		{
		case kFindBtnNext:
			find_run(true);
			break;
		case kFindBtnPrev:
			find_run(false);
			break;
		case kFindBtnCase:
			find_toggle_case();
			break;
		case kFindBtnWord:
			find_toggle_word();
			break;
		case kFindBtnClose:
			find_close();
			break;
		case kFindBtnReplace:
			find_replace();
			break;
		case kFindBtnReplaceAll:
			find_replace_all();
			break;
		default:
			break;
		}
		return true;
	}
	if (y >= find_view_.query_rect.top && y <= find_view_.query_rect.bottom)
	{
		find_focus_ = 0;
		invalidate();
		return true;
	}
	if (find_replace_row_ && y >= find_view_.replace_rect.top && y <= find_view_.replace_rect.bottom)
	{
		find_focus_ = 1;
		invalidate();
		return true;
	}
	return true; // клик по полосе в текст не проваливается
}

bool App::find_mouse_move(float x, float y)
{
	if (!show_find_)
		return false;
	const D2D1_RECT_F& r = find_view_.rect;
	if (x < r.left || x > r.right || y < r.top || y > r.bottom)
		return false;
	bool changed = false;
	for (FindBarButtonView& b : find_view_.buttons)
	{
		const bool hot = x >= b.rect.left && x <= b.rect.right && y >= b.rect.top &&
			y <= b.rect.bottom;
		if (hot != b.hot)
		{
			b.hot = hot;
			changed = true;
		}
	}
	if (changed)
		invalidate();
	return true;
}

bool App::find_char(wchar_t ch)
{
	if (!show_find_ || find_focus_ < 0)
		return false;
	if (ch < 0x20)
		return true; // управляющие символы в поле не вставляем
	find_add_text(utf16_to_utf8(std::u16string(1, static_cast<char16_t>(ch))));
	return true;
}

bool App::find_key(WPARAM vk, bool ctrl, bool shift)
{
	if (!show_find_)
		return false;
	if (find_focus_ < 0)
	{
		// Пользователь щёлкнул в тексте: полоса остаётся (по ней видно
		// найденное), но печать идёт в документ. Из клавиш забираем только
		// навигацию по поиску.
		if (vk == VK_F3)
		{
			find_run(!shift);
			return true;
		}
		if (vk == VK_ESCAPE)
		{
			find_close();
			return true;
		}
		if (ctrl && vk == 'F')
		{
			find_focus_ = 0;
			invalidate();
			return true;
		}
		if (ctrl && vk == 'H')
		{
			find_replace_row_ = true;
			find_focus_ = 1;
			find_sync_view();
			invalidate();
			return true;
		}
		return false;
	}
	switch (vk)
	{
	case VK_ESCAPE:
		find_close();
		return true;
	case VK_RETURN:
		if (find_focus_ == 1 && find_replace_row_)
			find_replace();
		else
			find_run(!shift);
		return true;
	case VK_F3:
		find_run(!shift);
		return true;
	case VK_TAB:
		if (find_replace_row_)
		{
			find_focus_ = find_focus_ == 1 ? 0 : 1;
			invalidate();
		}
		return true;
	case VK_BACK:
		find_backspace();
		return true;
	case VK_LEFT:
	case VK_RIGHT:
	case VK_UP:
	case VK_DOWN:
	case VK_HOME:
	case VK_END:
	case VK_PRIOR:
	case VK_NEXT:
	case VK_DELETE:
		return true; // курсор документа этими клавишами не двигаем
	default:
		break;
	}
	if (ctrl)
	{
		switch (vk)
		{
		case 'H':
			find_replace_row_ = !find_replace_row_;
			if (!find_replace_row_)
				find_focus_ = 0;
			find_sync_view();
			invalidate();
			return true;
		case 'F':
			if (shift)
			{
				find_regex_ = true;
				find_count_hits();
				find_sync_view();
				invalidate();
			}
			return true;
		default:
			break;
		}
	}
	return false;
}

bool App::options_click(float x, float y)
{
	if (!options_open_)
		return false;
	options_sync_view(); // размер окна мог измениться
	const int button = options_layout_.button_at(x, y);
	if (button >= 0)
	{
		const OptionChoice what = options_layout_.buttons[static_cast<std::size_t>(button)].what;
		switch (what)
		{
		case OptionChoice::apply:
			options_apply(false);
			break;
		case OptionChoice::apply_remember:
			options_apply(true);
			break;
		case OptionChoice::cancel:
			options_close();
			break;
		case OptionChoice::font_smaller:
			options_font_step(-1);
			break;
		case OptionChoice::font_bigger:
			options_font_step(1);
			break;
		case OptionChoice::lines_below_minus:
			options_lines_below_step(-1);
			break;
		case OptionChoice::lines_below_plus:
			options_lines_below_step(1);
			break;
		default:
			break;
		}
		return true;
	}
	const int row = options_layout_.row_at(x, y);
	if (row >= 0)
	{
		options_row_ = row;
		options_activate(row);
	}
	return true; // окно модально: клик мимо не проваливается в текст
}

bool App::options_mouse_move(float x, float y)
{
	if (!options_open_)
		return false;
	const int button = options_layout_.button_at(x, y);
	if (button != options_hot_)
	{
		options_hot_ = button;
		options_view_.hot = button;
		invalidate();
	}
	return true;
}

bool App::options_key(WPARAM vk, bool ctrl, bool shift)
{
	if (!options_open_)
		return false;
	switch (vk)
	{
	case VK_ESCAPE:
		options_close();
		return true;
	case VK_RETURN:
		if (ctrl)
			options_apply(true); // Ctrl+Enter — «применить и запомнить»
		else
			options_activate(options_row_); // Enter по строке — переключить
		return true;
	case VK_UP:
		options_move(-1);
		return true;
	case VK_DOWN:
		options_move(1);
		return true;
	case VK_HOME:
		options_row_ = 0;
		options_sync_view();
		invalidate();
		return true;
	case VK_END:
		if (!options_layout_.rows.empty())
			options_row_ = static_cast<int>(options_layout_.rows.size()) - 1;
		options_sync_view();
		invalidate();
		return true;
	case VK_TAB:
		options_move(shift ? -1 : 1);
		return true;
	case VK_LEFT:
	case VK_RIGHT:
	{
		// Стрелки по строке числа меняют значение; на вкладках — переключают.
		if (options_row_ < 0 ||
			static_cast<std::size_t>(options_row_) >= options_layout_.rows.size())
			return true;
		const OptionChoice what =
			options_layout_.rows[static_cast<std::size_t>(options_row_)].what;
		const int step = vk == VK_RIGHT ? 1 : -1;
		if (what == OptionChoice::lines_below)
			options_lines_below_step(step);
		else if (what == OptionChoice::font_size)
			options_font_step(step);
		else if (what == OptionChoice::mark_color || what == OptionChoice::back_mark_color)
		{
			// Стрелка по строке цвета — ColorDialog, как кнопка оригинала.
			pick_mark_color(what == OptionChoice::back_mark_color);
		}
		else if (what == OptionChoice::tab_yofy || what == OptionChoice::tab_editor ||
				 what == OptionChoice::tab_dicts)
		{
			options_state_.tab = (options_state_.tab + (step > 0 ? 1 : 2)) % 3;
			options_row_ = 0;
			options_sync_view();
			invalidate();
		}
		return true;
	}
	case VK_SPACE:
		options_activate(options_row_);
		return true;
	default:
		break;
	}
	if (ctrl && vk == VK_OEM_PLUS)
	{
		options_font_step(1);
		return true;
	}
	if (ctrl && vk == VK_OEM_MINUS)
	{
		options_font_step(-1);
		return true;
	}
	// Остальные клавиши окну настроек не нужны, но и документу не отдаём.
	return true;
}

// ---------------------------------------------------------------- настройки

std::filesystem::path App::dictionary_path(bool regex_dict) const
{
	const std::wstring& name = regex_dict ? regex_setting_ : dict_setting_;
	if (name.empty())
		return find_data_file(regex_dict ? L"yo_regexpr.dat" : L"yox.dat");
	std::filesystem::path path(name);
	if (path.is_relative())
		path = program_dir() / path;
	return path;
}

std::string App::dictionary_display(bool regex_dict) const
{
	const std::wstring& setting = regex_dict ? regex_setting_ : dict_setting_;
	if (!setting.empty())
		return narrow(setting);
	return regex_dict ? "yo_regexpr.dat" : "yox.dat";
}

void App::options_open_dialog()
{
	if (options_open_)
		return;
	// Значения — из текущих настроек (как Options1Click: NewYoOptions := YoOptions).
	options_state_.always_ask = always_ask_;
	options_state_.silent_yofy = silent_yofy_;
	options_state_.mark = mark_;
	options_state_.propose_last = propose_last_;
	options_state_.regexs = regexs_;
	options_state_.to_confirm_abbr = to_confirm_abbr_;
	options_state_.check_yo = check_yo_;
	options_state_.no_var_only = no_var_only_;
	options_state_.var_only = var_only_;
	options_state_.to_confirm_cap = to_confirm_cap_;
	options_state_.to_confirm_ellipsis = to_confirm_ellipsis_;
	options_state_.fb_format = fb_format_;
	options_state_.lines_below = lines_below_;
	options_state_.fast_scroll = fast_scroll_;
	options_state_.mark_color = mark_color_;
	options_state_.back_mark_color = back_mark_color_;
	// Правила блокировок — по текущему состоянию (OptionsPage.cbClick).
	yo::options_normalize(options_state_);
	options_state_.word_wrap = word_wrap_;
	options_state_.recall_last_file = recall_last_file_;
	options_state_.auto_unicode = auto_unicode_;
	options_state_.show_toolbar = show_toolbar_;
	options_state_.to_confirm_close = to_confirm_close_;
	options_state_.font_size = font_size_;
	options_state_.font_name = narrow(font_name_);
	options_state_.dictionary = dictionary_display(false);
	options_state_.regex_dictionary = dictionary_display(true);
	options_open_ = true;
	options_row_ = 0;
	options_hot_ = -1;
	options_sync_view();
	invalidate();
}

void App::options_close()
{
	options_open_ = false;
	options_view_ = {};
	options_layout_ = {};
	options_hot_ = -1;
	invalidate();
}

void App::options_font_step(int delta)
{
	options_state_.font_size =
		std::clamp(options_state_.font_size + static_cast<float>(delta), 8.0F, 48.0F);
	options_sync_view();
	invalidate();
}

void App::options_lines_below_step(int delta)
{
	options_state_.lines_below = std::clamp(options_state_.lines_below + delta, 0, 40);
	options_sync_view();
	invalidate();
}

void App::options_apply(bool remember)
{
	const bool yofy_changed = mark_ != options_state_.mark ||
		propose_last_ != options_state_.propose_last || regexs_ != options_state_.regexs ||
		to_confirm_abbr_ != options_state_.to_confirm_abbr ||
		check_yo_ != options_state_.check_yo || no_var_only_ != options_state_.no_var_only ||
		var_only_ != options_state_.var_only || to_confirm_cap_ != options_state_.to_confirm_cap ||
		to_confirm_ellipsis_ != options_state_.to_confirm_ellipsis ||
		fb_format_ != options_state_.fb_format || lines_below_ != options_state_.lines_below;

	always_ask_ = options_state_.always_ask;
	silent_yofy_ = options_state_.silent_yofy;
	mark_ = options_state_.mark;
	propose_last_ = options_state_.propose_last;
	regexs_ = options_state_.regexs;
	to_confirm_abbr_ = options_state_.to_confirm_abbr;
	check_yo_ = options_state_.check_yo;
	no_var_only_ = options_state_.no_var_only;
	var_only_ = options_state_.var_only;
	to_confirm_cap_ = options_state_.to_confirm_cap;
	to_confirm_ellipsis_ = options_state_.to_confirm_ellipsis;
	fb_format_ = options_state_.fb_format;
	lines_below_ = options_state_.lines_below;
	fast_scroll_ = options_state_.fast_scroll;
	mark_color_ = options_state_.mark_color;
	back_mark_color_ = options_state_.back_mark_color;
	recall_last_file_ = options_state_.recall_last_file;
	auto_unicode_ = options_state_.auto_unicode;
	show_toolbar_ = options_state_.show_toolbar;
	to_confirm_close_ = options_state_.to_confirm_close;

	// Перенос по словам применяется сразу (SetWordWrap оригинала), но
	// правка текста при этом не появляется.
	if (word_wrap_ != options_state_.word_wrap)
	{
		word_wrap_ = options_state_.word_wrap;
		apply_word_wrap();
	}

	if (yofy_)
	{
		set_always_ask(always_ask_);
		set_silent_yofy(silent_yofy_);
		if (yofy_changed)
			yofy_.reset(); // опции читаются при создании движка
	}

	// Шрифт применяется сразу, как в оригинале (Editor.Font := …).
	font_name_.assign(widen(options_state_.font_name));
	if ((std::abs(options_state_.font_size - font_size_) > 0.01F ||
		 font_name_ != renderer_.font_family()) &&
		renderer_.set_editor_font(font_name_, options_state_.font_size, hwnd_))
	{
		font_size_ = renderer_.font_size();
		sync_metrics();
	}

	apply_mark_colors(); // цвета пометки видны сразу, без перезапуска

	options_open_ = false;
	options_view_ = {};
	options_layout_ = {};
	if (remember)
		save_settings();
	update_menu_checks();
	refresh_view();
	find_sync_view();
	invalidate();
}

void App::options_sync_view()
{
	options_view_ = OptionsView{};
	options_layout_ = OptionsLayout{};
	if (!options_open_)
		return;
	options_layout_ = yo::layout_options(options_state_, renderer_.width_dip(), renderer_.height_dip(),
										 renderer_.ui_line_height());
	if (!options_layout_.visible)
		return;

	const yo::OptionsLayout& layout = options_layout_;
	options_view_.rect = D2D1::RectF(layout.x, layout.y, layout.x + layout.w, layout.y + layout.h);
	options_view_.title = widen(layout.title);
	for (const yo::OptionRowBox& row : layout.rows)
	{
		OptionsRowView view;
		view.rect = D2D1::RectF(row.x, row.y, row.x + row.w, row.y + row.h);
		view.label = widen(row.label);
		view.value = widen(row.value);
		view.value_inset = row.value_inset;
		view.check = row.check;
		view.checked = row.checked;
		view.path = row.path;
		view.tab = row.tab;
		view.tab_active = row.tab_active;
		options_view_.rows.push_back(std::move(view));
	}
	for (const yo::OptionButtonBox& button : layout.buttons)
	{
		OptionsButtonView view;
		view.rect = D2D1::RectF(button.x, button.y, button.x + button.w, button.y + button.h);
		view.label = widen(button.label);
		view.choice = static_cast<int>(button.what);
		view.primary = button.what == OptionChoice::apply;
		options_view_.buttons.push_back(std::move(view));
	}
	options_view_.focus = options_row_;
	options_view_.hot = options_hot_;
}

void App::options_move(int delta)
{
	if (options_layout_.rows.empty())
		return;
	const int count = static_cast<int>(options_layout_.rows.size());
	options_row_ = (options_row_ + delta) % count;
	if (options_row_ < 0)
		options_row_ += count;
	options_sync_view();
	invalidate();
}

void App::options_activate(int row)
{
	if (row < 0 || static_cast<std::size_t>(row) >= options_layout_.rows.size())
		return;
	const OptionRowBox& box = options_layout_.rows[static_cast<std::size_t>(row)];
	const OptionChoice what = box.what;
	// Недоступная строка (правила блокировок, как OptionsPage.cbClick)
	// не реагирует на мышь и Enter.
	if (!yo::option_enabled(options_state_, what))
		return;

	// Галочки переключает ядро: оно же снимает взаимоисключающие пункты
	// («Только интерактивные замены» ↔ «Всегда подтверждать замену»).
	if (box.check)
	{
		yo::options_toggle(options_state_, what);
		options_sync_view();
		invalidate();
		return;
	}

	switch (what)
	{
	// Вкладки
	case OptionChoice::tab_yofy:
		options_state_.tab = 0;
		options_row_ = 0;
		break;
	case OptionChoice::tab_editor:
		options_state_.tab = 1;
		options_row_ = 0;
		break;
	case OptionChoice::tab_dicts:
		options_state_.tab = 2;
		options_row_ = 0;
		break;
	case OptionChoice::lines_below: // Enter по строке числа — «+»
		options_lines_below_step(1);
		return;
	case OptionChoice::font_size: // Enter по строке «Шрифт» — диалог шрифта
		pick_editor_font();
		break;
	case OptionChoice::mark_color:
		pick_mark_color(false);
		break;
	case OptionChoice::back_mark_color:
		pick_mark_color(true);
		break;
	case OptionChoice::dictionary:
		pick_dictionary(false);
		break;
	case OptionChoice::regex_dictionary:
		pick_dictionary(true);
		break;
	default:
		break;
	}
	options_sync_view();
	invalidate();
}

bool App::pick_dictionary(bool regex_dict)
{
	std::vector<wchar_t> file(32768, L'\0');
	const std::wstring current = regex_dict ? regex_setting_ : dict_setting_;
	if (!current.empty())
	{
		const std::size_t n = std::min(current.size(), file.size() - 1);
		std::copy_n(current.begin(), static_cast<std::ptrdiff_t>(n), file.begin());
	}
	OPENFILENAMEW ofn{};
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = hwnd_;
	ofn.lpstrFilter = L"Словари (*.dat)\0*.dat\0Все файлы\0*.*\0";
	ofn.lpstrFile = file.data();
	ofn.nMaxFile = static_cast<DWORD>(file.size());
	ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER;
	if (GetOpenFileNameW(&ofn) == FALSE)
		return false;

	const std::filesystem::path path(file.data());
	// Файл рядом с программой храним коротким именем — как в yo.ini
	// оригинала, где стоит просто «yox.dat».
	std::wstring stored = path.wstring();
	std::error_code ec;
	const std::filesystem::path rel = std::filesystem::relative(path, program_dir(), ec);
	if (!ec && !rel.empty() && rel.native().rfind(L"..", 0) != 0)
		stored = rel.wstring();

	if (regex_dict)
	{
		regex_setting_ = stored;
		options_state_.regex_dictionary = narrow(stored);
	}
	else
	{
		dict_setting_ = stored;
		options_state_.dictionary = narrow(stored);
	}
	options_sync_view();
	invalidate();
	return true;
}

void App::reload_dictionaries()
{
	yofy_.reset(); // следующий вызов загрузит словари заново, уже по новым путям
	if (!ensure_yoficator())
		return;
	save_settings();
	refresh_view();
}

std::filesystem::path App::usersel_path() const
{
	const std::filesystem::path dir = ini_path_.empty() ? program_dir() : ini_path_.parent_path();
	return dir / L"yo_user.dat";
}

void App::load_user_selections()
{
	if (!dict_)
		return;
	std::ifstream in(usersel_path(), std::ios::binary);
	if (!in)
		return;
	const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
	const std::vector<UserSelEntry> entries = parse_user_selections(text);
	std::size_t applied = 0;
	for (const UserSelEntry& e : entries)
	{
		const UString word = decode_line({ reinterpret_cast<const std::uint8_t*>(e.word.data()),
										   e.word.size() },
										 Encoding::utf8);
		const int index = dict_->find(word);
		if (index < 0)
			continue; // слова нет в этом словаре — строка не наша
		dict_->set_user_sel(static_cast<std::size_t>(index), e.value);
		++applied;
	}
	trace("настройки: загружены запомненные ответы «Всегда Е/Ё»");
	if (applied != entries.size())
		trace("настройки: часть запомненных слов не найдена в словаре");
}

void App::save_user_selections()
{
	if (!dict_)
		return;
	std::vector<UserSelEntry> entries;
	for (std::size_t i = 0; i < dict_->size(); ++i)
	{
		const int value = dict_->user_sel(i);
		if (value != 1 && value != 2)
			continue;
		entries.push_back(UserSelEntry{ encode_line(dict_->ye_unicode(i), Encoding::utf8), value });
	}

	const std::filesystem::path path = usersel_path();
	std::error_code ec;
	if (entries.empty())
	{
		// Нечего запоминать — файла быть не должно (иначе он «воскрешает»
		// старые ответы после сброса).
		std::filesystem::remove(path, ec);
		return;
	}

	const std::string text = "; Ё (yo-c-) — запомненные ответы «Всегда Е/Ё» (1 — Е, 2 — Ё).\n" +
		format_user_selections(entries);
	const std::filesystem::path tmp = path.wstring() + L".yo-tmp";
	{
		std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
		if (!out)
			return;
		out.write(text.data(), static_cast<std::streamsize>(text.size()));
	}
	ec.clear();
	std::filesystem::rename(tmp, path, ec);
	if (ec)
	{
		std::filesystem::remove(path, ec);
		std::filesystem::rename(tmp, path, ec);
	}
}

void App::clear_user_selection()
{
	if (!dict_)
	{
		// Словарь ещё не загружен: подгрузим, чтобы сброс было к чему применить.
		if (!ensure_yoficator() || !dict_)
			return;
	}
	dict_->reset_user_sel();
	if (yofy_)
		yofy_.reset(); // сбросить кешированный разбор
	std::error_code ec;
	std::filesystem::remove(usersel_path(), ec); // запомненное тоже забываем
	invalidate();
}


// ---------------------------------------------------------------- редакторы словарей

namespace {

// Кнопки редактора словарей: id одинаковы для вида, кликов и клавиатуры.
enum DictButtonId
{
	kDictModeWords = 1,
	kDictModeRules,
	kDictAsk,
	kDictAdd,
	kDictChange,
	kDictErase,
	kDictProbe,
	kDictSave,
	kDictClose,
};

[[nodiscard]] std::wstring dict_utf8_to_wide(const std::string& utf8) { return widen(utf8); }

// UTF-32 (ядро) -> UTF-16 (Win32), с суррогатными парами.
[[nodiscard]] std::wstring dict_u32_to_wide(const yo::UString& text)
{
	std::wstring out;
	out.reserve(text.size());
	for (char32_t c : text)
	{
		if (c >= 0x10000)
		{
			const char32_t v = c - 0x10000;
			out.push_back(static_cast<wchar_t>(0xD800 + (v >> 10)));
			out.push_back(static_cast<wchar_t>(0xDC00 + (v & 0x3FF)));
		}
		else
		{
			out.push_back(static_cast<wchar_t>(c));
		}
	}
	return out;
}

} // namespace

void App::dict_editor_open()
{
	if (dict_open_)
	{
		dict_editor_close();
		return;
	}
	const std::filesystem::path words = dictionary_path(false);
	const std::filesystem::path rules = dictionary_path(true);
	yo::dict_editor_load(dict_state_, narrow(words.wstring()), narrow(rules.wstring()));
	dict_state_.mode = yo::DictMode::words;
	yo::dict_editor_refilter(dict_state_);
	dict_open_ = true;
	dict_hot_ = -1;
	dict_sync_view();
	invalidate();
}

void App::dict_editor_close()
{
	if (!dict_open_)
		return;
	dict_open_ = false;
	dict_view_ = {};
	dict_hot_ = -1;
	invalidate();
}

void App::dict_sync_view()
{
	dict_view_ = DictView{};
	if (!dict_open_)
		return;

	const float w = std::min(std::max(560.0F, renderer_.width_dip() - 80.0F), 1000.0F);
	const float h = std::min(std::max(380.0F, renderer_.height_dip() - 120.0F), 760.0F);
	const float x = std::max(0.0F, (renderer_.width_dip() - w) * 0.5F);
	const float y = renderer_.bar_height() + 20.0F;
	dict_view_.rect = D2D1::RectF(x, y, x + w, y + h);

	const float pad = 14.0F;
	const float field_h = std::max(22.0F, renderer_.ui_line_height() + 8.0F);
	const float row_h = std::max(16.0F, renderer_.ui_line_height() + 2.0F);

	dict_view_.title = L"Редактор словаря";
	dict_view_.subtitle =
		dict_state_.mode == yo::DictMode::words
			? L"yox.dat: " + std::to_wstring(dict_state_.words.size()) + L" слов"
			: L"yo_regexpr.dat: " + std::to_wstring(dict_state_.rules.rule_count()) + L" правил";

	// Поле фильтра — вся ширина панели.
	const float top = y + 50.0F;
	dict_view_.filter_rect = D2D1::RectF(x + pad, top, x + w - pad, top + field_h);
	dict_view_.filter = dict_u32_to_wide(dict_state_.filter.query);
	dict_view_.filter_focus = dict_state_.field == yo::DictField::filter;

	// Список: сколько строк влезает между полем фильтра и полем правки.
	const float list_top = top + field_h + 8.0F;
	const float edit_h = field_h + 22.0F; // подпись + поле
	const float buttons_h = 30.0F;
	const float message_h = 20.0F;
	const float hint_h = 22.0F;
	const float list_bottom = y + h - (edit_h + buttons_h + message_h + hint_h + pad);
	const auto rows_fit = static_cast<std::size_t>(
		std::max(3.0F, std::floor((list_bottom - list_top) / row_h)));
	dict_rows_ = rows_fit;

	// Прокрутка списка под курсор (top живёт в ядре).
	if (dict_state_.cursor < dict_state_.top)
		dict_state_.top = dict_state_.cursor;
	if (dict_state_.cursor >= dict_state_.top + dict_rows_)
		dict_state_.top = dict_state_.cursor - dict_rows_ + 1;

	for (std::size_t i = 0; i < dict_rows_; ++i)
	{
		const std::size_t list_row = dict_state_.top + i;
		D2D1_RECT_F rect = D2D1::RectF(x + pad, list_top + static_cast<float>(i) * row_h,
									   x + w - pad, list_top + static_cast<float>(i + 1) * row_h);
		dict_view_.row_rects.push_back(rect);
		if (list_row >= dict_state_.visible.size())
			continue;
		DictRowView view;
		view.rect = rect;
		view.text = dict_utf8_to_wide(yo::dict_row_text(dict_state_, list_row));
		view.current = list_row == dict_state_.cursor;
		view.error = yo::dict_row_error(dict_state_, list_row);
		dict_view_.rows.push_back(std::move(view));
	}

	// Поле правки: словоформа (слова) или шаблон (правила РВ).
	const float edit_label_y = list_bottom + 2.0F;
	dict_view_.edit_label = dict_state_.mode == yo::DictMode::words ? L"Словоформа" : L"Правило";
	const float ask_w = dict_state_.mode == yo::DictMode::words ? 40.0F : 0.0F;
	dict_view_.edit_rect = D2D1::RectF(x + pad, edit_label_y + 16.0F,
									   x + w - pad - ask_w, edit_label_y + 16.0F + field_h);
	dict_view_.edit = dict_u32_to_wide(dict_state_.edit);
	dict_view_.edit_focus = dict_state_.field == yo::DictField::text;

	// Кнопки: режим, «?», действия, проверка, сохранение и закрытие.
	const float buttons_y = y + h - (buttons_h + message_h + hint_h + pad) + 4.0F;
	float bx = x + pad;
	const auto add_button = [&](std::wstring label, int id, bool primary, bool toggle, bool checked)
	{
		const float bw = std::max(34.0F, renderer_.measure_ui(label) + 18.0F);
		DictButtonView b;
		b.rect = D2D1::RectF(bx, buttons_y, bx + bw, buttons_y + buttons_h - 6.0F);
		b.label = std::move(label);
		b.id = id;
		b.primary = primary;
		b.toggle = toggle;
		b.checked = checked;
		b.hot = dict_hot_ == id;
		bx += bw + 6.0F;
		dict_view_.buttons.push_back(std::move(b));
	};

	add_button(L"Слова", kDictModeWords, false, true, dict_state_.mode == yo::DictMode::words);
	add_button(L"Правила РВ", kDictModeRules, false, true, dict_state_.mode == yo::DictMode::rules);
	bx += 10.0F;
	add_button(L"Добавить", kDictAdd, false, false, false);
	add_button(L"Изменить", kDictChange, false, false, false);
	add_button(L"Удалить", kDictErase, false, false, false);
	if (dict_state_.mode == yo::DictMode::rules)
	{
		bx += 10.0F;
		add_button(L"Проверить", kDictProbe, false, false, false);
	}
	add_button(L"Сохранить", kDictSave, true, false, false);
	add_button(L"Закрыть", kDictClose, false, false, false);

	// «?» — переключатель у поля правки (только словарь).
	if (dict_state_.mode == yo::DictMode::words)
	{
		DictButtonView ask;
		ask.rect = D2D1::RectF(x + w - pad - ask_w + 4.0F, dict_view_.edit_rect.top,
							   x + w - pad, dict_view_.edit_rect.bottom);
		ask.label = L"?";
		ask.id = kDictAsk;
		ask.toggle = true;
		ask.checked = dict_state_.edit_ask;
		ask.hot = dict_hot_ == kDictAsk;
		dict_view_.buttons.push_back(std::move(ask));
	}

	dict_view_.message_rect = D2D1::RectF(x + pad, y + h - (message_h + hint_h + pad) + 2.0F,
										  x + w - pad, y + h - (hint_h + pad) + 2.0F);
	dict_view_.message = dict_utf8_to_wide(dict_state_.message);
	dict_view_.message_error = dict_state_.message_error;
	dict_view_.hint = dict_state_.mode == yo::DictMode::words
						  ? L"Tab — поле, ↑↓/PgUp/PgDn — выбор, Enter — изменить, Ctrl+Enter — "
							L"добавить, Del — удалить, Ctrl+S — сохранить, Esc — закрыть"
						  : L"Tab — поле, ↑↓/PgUp/PgDn — выбор, Enter — изменить, Ctrl+Enter — "
							L"добавить, Del — удалить, F5 — проверить контекст, Ctrl+S — сохранить";
}

void App::dict_set_mode(yo::DictMode mode)
{
	if (dict_state_.mode == mode)
		return;
	dict_state_.mode = mode;
	dict_state_.cursor = 0;
	dict_state_.top = 0;
	dict_state_.edit.clear();
	dict_state_.edit_ask = false;
	dict_state_.field = yo::DictField::filter;
	yo::dict_editor_refilter(dict_state_);
	dict_sync_view();
	invalidate();
}

void App::dict_move(long delta)
{
	yo::dict_editor_move(dict_state_, delta);
	dict_sync_view();
	invalidate();
}

void App::dict_pick_selected()
{
	yo::dict_editor_pick(dict_state_);
	dict_sync_view();
	invalidate();
}

void App::dict_apply()
{
	(void)yo::dict_editor_apply(dict_state_);
	dict_sync_view();
	invalidate();
}

void App::dict_add()
{
	(void)yo::dict_editor_add(dict_state_);
	dict_sync_view();
	invalidate();
}

void App::dict_erase()
{
	if (dict_state_.visible.empty())
		return;
	// Перед удалением спрашиваем: словарь — не документ, undo здесь нет.
	const std::wstring question =
		L"Удалить выбранную строку из словаря?\nИзменения попадут в файл только по «Сохранить».";
	if (MessageBoxW(hwnd_, question.c_str(), L"Редактор словаря",
					MB_ICONQUESTION | MB_YESNO | MB_DEFBUTTON2) != IDYES)
		return;
	(void)yo::dict_editor_erase(dict_state_);
	dict_sync_view();
	invalidate();
}

void App::dict_save()
{
	if (!yo::dict_editor_save(dict_state_))
	{
		dict_sync_view();
		invalidate();
		return;
	}
	// Словари перечитываются, чтобы ёфикация сразу работала по новым данным.
	// Ответы «Всегда Е/Ё» (yo_user.dat) при этом не сбрасываются.
	yofy_.reset();
	(void)ensure_yoficator();
	dict_sync_view();
	invalidate();
}

void App::dict_probe()
{
	if (dict_state_.mode != yo::DictMode::rules)
		return;
	// Контекст для проверки — текст фильтра; слово — первое слово контекста.
	const yo::UString& context = dict_state_.filter.query;
	if (context.empty())
	{
		dict_state_.message = "введите контекст в поле фильтра";
		dict_state_.message_error = true;
		dict_sync_view();
		invalidate();
		return;
	}
	std::size_t b = 0;
	while (b < context.size() && yo::cyr::is_blank(context[b]))
		++b;
	std::size_t e = b;
	while (e < context.size() && yo::cyr::is_alpha(context[e]))
		++e;
	if (e == b)
	{
		dict_state_.message = "в контексте нет слова из русских букв";
		dict_state_.message_error = true;
		dict_sync_view();
		invalidate();
		return;
	}
	const yo::UString word = context.substr(b, e - b);
	const yo::RegexProbe probe = yo::probe_regex(dict_state_.rules, context, b, word);
	if (!probe.ok)
	{
		dict_state_.message = probe.error;
		dict_state_.message_error = true;
	}
	else if (probe.matched)
	{
		std::string answer = std::string("правило №") + std::to_string(probe.rule_no + 1) + " (строка " +
							 std::to_string(probe.line + 1) + "): советует форму «" +
							 (probe.word_n == 2 ? probe.word_yo : probe.word_ye) + "»";
		dict_state_.message = answer;
		dict_state_.message_error = false;
	}
	else
	{
		dict_state_.message = "ни одно правило не сработало (словарь РВ не решил)";
		dict_state_.message_error = false;
	}
	dict_sync_view();
	invalidate();
}

void App::dict_focus(yo::DictField field)
{
	dict_state_.field = field;
	dict_sync_view();
	invalidate();
}

void App::dict_type(char32_t cp)
{
	if (!dict_open_)
		return;
	if (dict_state_.field == yo::DictField::filter)
	{
		dict_state_.filter.query.push_back(cp);
		yo::dict_editor_refilter(dict_state_);
		dict_state_.cursor = 0;
		dict_state_.top = 0;
	}
	else
	{
		dict_state_.edit.push_back(cp);
	}
	dict_sync_view();
	invalidate();
}

void App::dict_backspace()
{
	if (!dict_open_)
		return;
	if (dict_state_.field == yo::DictField::filter)
	{
		if (dict_state_.filter.query.empty())
			return;
		dict_state_.filter.query.pop_back();
		yo::dict_editor_refilter(dict_state_);
		dict_state_.cursor = 0;
		dict_state_.top = 0;
	}
	else
	{
		if (dict_state_.edit.empty())
			return;
		dict_state_.edit.pop_back();
	}
	dict_sync_view();
	invalidate();
}

void App::dict_activate(int id)
{
	switch (id)
	{
	case kDictModeWords:
		dict_set_mode(yo::DictMode::words);
		break;
	case kDictModeRules:
		dict_set_mode(yo::DictMode::rules);
		break;
	case kDictAsk:
		dict_state_.edit_ask = !dict_state_.edit_ask;
		dict_sync_view();
		invalidate();
		break;
	case kDictAdd:
		dict_add();
		break;
	case kDictChange:
		dict_apply();
		break;
	case kDictErase:
		dict_erase();
		break;
	case kDictProbe:
		dict_probe();
		break;
	case kDictSave:
		dict_save();
		break;
	case kDictClose:
		dict_editor_close();
		break;
	default:
		break;
	}
}

bool App::dict_key(WPARAM vk, bool ctrl, bool shift)
{
	if (!dict_open_)
		return false;
	switch (vk)
	{
	case VK_ESCAPE:
		dict_editor_close();
		return true;
	case VK_UP:
		dict_move(-1);
		return true;
	case VK_DOWN:
		dict_move(1);
		return true;
	case VK_PRIOR:
		dict_move(-static_cast<long>(std::max<std::size_t>(1, dict_rows_)));
		return true;
	case VK_NEXT:
		dict_move(static_cast<long>(std::max<std::size_t>(1, dict_rows_)));
		return true;
	case VK_HOME:
		dict_state_.cursor = 0;
		dict_state_.top = 0;
		dict_sync_view();
		invalidate();
		return true;
	case VK_END:
		if (!dict_state_.visible.empty())
			dict_state_.cursor = dict_state_.visible.size() - 1;
		dict_sync_view();
		invalidate();
		return true;
	case VK_TAB:
		dict_focus(shift ? yo::DictField::filter : yo::DictField::text);
		return true;
	case VK_RETURN:
		if (ctrl)
			dict_add();
		else
			dict_apply();
		return true;
	case VK_DELETE:
		dict_erase();
		return true;
	case VK_F5:
		dict_probe();
		return true;
	case VK_BACK:
		dict_backspace();
		return true;
	default:
		break;
	}
	if (ctrl && vk == 'S')
	{
		dict_save();
		return true;
	}
	return true; // остальные клавиши документу не отдаём
}

bool App::dict_char(wchar_t ch)
{
	if (!dict_open_)
		return false;
	const auto cp = static_cast<char32_t>(ch);
	// Суррогаты поодиночке (пары приходят не всегда) и управляющие — не текст.
	if (cp >= 0x20 && cp != 0x7F && !(cp >= 0xD800 && cp <= 0xDFFF))
		dict_type(cp);
	return true;
}

bool App::dict_click(float x, float y)
{
	if (!dict_open_)
		return false;
	const auto inside = [](const D2D1_RECT_F& r, float px, float py)
	{ return px >= r.left && px <= r.right && py >= r.top && py <= r.bottom; };

	for (const DictButtonView& b : dict_view_.buttons)
	{
		if (inside(b.rect, x, y))
		{
			dict_activate(b.id);
			return true;
		}
	}
	for (std::size_t i = 0; i < dict_view_.rows.size(); ++i)
	{
		if (!inside(dict_view_.rows[i].rect, x, y))
			continue;
		dict_state_.cursor = dict_state_.top + i;
		dict_pick_selected();
		return true;
	}
	if (inside(dict_view_.filter_rect, x, y))
	{
		dict_focus(yo::DictField::filter);
		return true;
	}
	if (inside(dict_view_.edit_rect, x, y))
	{
		dict_focus(yo::DictField::text);
		return true;
	}
	return inside(dict_view_.rect, x, y); // клик по панели — документу не отдаём
}

bool App::dict_mouse_move(float x, float y)
{
	if (!dict_open_)
		return false;
	int hot = -1;
	for (const DictButtonView& b : dict_view_.buttons)
	{
		if (x >= b.rect.left && x <= b.rect.right && y >= b.rect.top && y <= b.rect.bottom)
		{
			hot = b.id;
			break;
		}
	}
	if (hot != dict_hot_)
	{
		dict_hot_ = hot;
		invalidate();
	}
	return hot >= 0;
}

// ---------------------------------------------------------------- последние файлы

std::string App::mru_flags() const
{
	// Буквы — как в Files.NameN оригинала: u — UTF-8, U — UTF-16LE,
	// S — BOM. CP1251 пишется без флагов (кодировка по умолчанию).
	std::string flags;
	if (enc_ == Encoding::utf8)
		flags += 'u';
	else if (is_utf16(enc_))
		flags += 'U';
	if (bom_)
		flags += 'S';
	return flags;
}

bool App::mru_decode_flags(std::string_view flags, Encoding& enc, bool& bom)
{
	if (flags.find('R') != std::string_view::npos)
		return false; // RTF открываем как текст, кодировку не навязываем
	if (flags.find('u') != std::string_view::npos)
		enc = Encoding::utf8;
	else if (flags.find('U') != std::string_view::npos)
		enc = Encoding::utf16le;
	else
		return false;
	bom = flags.find('S') != std::string_view::npos;
	return true;
}

void App::mru_touch(const std::filesystem::path& path, std::size_t position)
{
	if (path.empty())
		return;
	const std::string utf8_path = path_utf8(path);
	last_file_index_ = mru_.add(MruEntry{ utf8_path, position, mru_flags() });
	build_menus();
	update_menu_checks();
	layout_menus();
}

void App::mru_remember_current()
{
	if (path_.empty() || buf_ == nullptr || ed_ == nullptr)
		return;
	mru_.remember(path_utf8(path_), ed_->caret(), mru_flags());
}

void App::clear_mru()
{
	if (mru_.empty())
		return;
	if (MessageBoxW(hwnd_, L"Очистить список последних файлов?", L"Ё",
					MB_YESNO | MB_ICONQUESTION) != IDYES)
		return;
	mru_.clear();
	last_file_index_ = 0;
	build_menus();
	update_menu_checks();
	layout_menus();
	save_settings();
	invalidate();
}

bool App::open_mru(std::size_t index)
{
	const MruEntry* entry = mru_.at(index);
	if (entry == nullptr)
		return false;
	const std::string utf8_path = entry->path;
	const std::size_t position = entry->position;
	const std::string flags = entry->flags;
	const std::filesystem::path path(widen(utf8_path));

	std::error_code ec;
	if (!std::filesystem::exists(path, ec))
	{
		const std::wstring question = L"Файл не найден:\n" + path.wstring() +
			L"\n\nУбрать его из списка последних файлов?";
		if (MessageBoxW(hwnd_, question.c_str(), L"Ё", MB_YESNO | MB_ICONWARNING) == IDYES)
		{
			mru_.remove(utf8_path);
			if (mru_.empty())
				last_file_index_ = 0;
			build_menus();
			update_menu_checks();
			layout_menus();
			save_settings();
			invalidate();
		}
		return false;
	}
	if (!ask_save_changes())
		return false;
	mru_remember_current();

	Encoding enc = Encoding::utf8;
	bool bom = false;
	const bool forced = mru_decode_flags(flags, enc, bom) && !is_rtf_path(path);
	const bool ok = forced ? load_forced(path, enc, bom) : load_from_path(path);
	if (!ok)
		return false;

	// Позицию курсора восстанавливаем, как GoAndShowPosition оригинала.
	// После открытия список мог обновиться — индекс ищем заново.
	ed_->set_caret(std::min(position, buf_->size()));
	caret_on_ = true;
	if (const std::size_t at = mru_.find(utf8_path); at < kMruMax)
		last_file_index_ = at;
	refresh_view();
	return true;
}

void App::mark_yofy_changes(std::size_t from, std::size_t to)
{
	yofy_marks_.clear();
	yofy_mark_index_ = 0;
	if (!mark_ || buf_ == nullptr)
		return;
	// Замены е→ё в UTF-8 не меняют длину (оба символа — два байта),
	// поэтому подсвечиваем сами «ё»/«Ё» в обработанном диапазоне: они и есть
	// сделанные замены. Так же красит замены Mark в оригинале.
	const std::size_t end = std::min(to, buf_->size());
	std::size_t pos = from;
	constexpr std::size_t kChunk = 1U << 16;
	constexpr std::size_t kMaxMarks = 200000;
	while (pos < end && yofy_marks_.size() < kMaxMarks)
	{
		const std::size_t take = std::min(kChunk, end - pos);
		std::string data;
		buf_->read(pos, take, data);
		for (std::size_t i = 0; i + 1 < data.size(); ++i)
		{
			const auto c0 = static_cast<unsigned char>(data[i]);
			const auto c1 = static_cast<unsigned char>(data[i + 1]);
			if ((c0 == 0xD1U && c1 == 0x91U) || (c0 == 0xD0U && c1 == 0x81U)) // ё / Ё
				yofy_marks_.emplace_back(pos + i, pos + i + 2);
		}
		if (take <= 1)
			break;
		pos += take - 1; // байт перекрытия: пара не рвётся на границе куска
	}
}

void App::clear_yofy_marks()
{
	yofy_marks_.clear();
	yofy_mark_index_ = 0;
	invalidate();
}

void App::yofy_goto(bool forward)
{
	if (yofy_marks_.empty() || ed_ == nullptr)
	{
		MessageBeep(MB_ICONASTERISK);
		return;
	}
	const std::size_t caret = ed_->caret();
	std::size_t target = std::string::npos;
	if (forward)
	{
		for (const auto& m : yofy_marks_)
		{
			if (m.first > caret)
			{
				target = m.first;
				break;
			}
		}
		if (target == std::string::npos)
			target = yofy_marks_.front().first; // с конца — по кругу в начало
	}
	else
	{
		for (auto it = yofy_marks_.rbegin(); it != yofy_marks_.rend(); ++it)
		{
			if (it->first < caret)
			{
				target = it->first;
				break;
			}
		}
		if (target == std::string::npos)
			target = yofy_marks_.back().first;
	}
	ed_->set_caret(target);
	ed_->set_caret(std::min(target + 2, buf_->size()), true);
	caret_on_ = true;
	ed_->ensure_caret_visible();
	update_scrollbars();
	invalidate();
}

void App::toggle_line_numbers()
{
	show_numbers_ = !show_numbers_;
	update_menu_checks();
	find_sync_view();
	refresh_view();
}

// Перенос по словам: в оригинале его нет, поэтому по умолчанию выключен.
void App::toggle_word_wrap()
{
	word_wrap_ = !word_wrap_;
	apply_word_wrap();
	save_settings();
	invalidate();
}

void App::apply_word_wrap()
{
	if (ed_ != nullptr)
		ed_->set_word_wrap(word_wrap_);
	update_menu_checks();
	find_sync_view();
	refresh_view();
}

void App::apply_mark_colors()
{
	// [Colors] ColorMark / ColorBackMark хранятся индексами палитры окна
	// настроек (в оригинале — COLORREF). COLORREF = 0x00BBGGRR.
	const auto to_color = [](std::uint32_t rgb, float alpha)
	{
		const float r = static_cast<float>((rgb >> 0U) & 0xFFU) / 255.0F;
		const float g = static_cast<float>((rgb >> 8U) & 0xFFU) / 255.0F;
		const float b = static_cast<float>((rgb >> 16U) & 0xFFU) / 255.0F;
		return D2D1::ColorF(r, g, b, alpha);
	};
	renderer_.set_yofy_colors(to_color(mark_color_, 1.0F), to_color(back_mark_color_, 0.35F));
}

bool App::pick_editor_font()
{
	// TFontDialog оригинала. Только моноширинные шрифты (CF_FIXEDPITCHONLY):
	// редактор считает ширину одного символа, и «пропорциональный» шрифт
	// развалил бы раскладку строк.
	LOGFONTW lf{};
	lf.lfHeight = -static_cast<LONG>(std::lround(font_size_ * std::max(1.0F, dpi_scale_)));
	lf.lfWeight = FW_NORMAL;
	lf.lfCharSet = static_cast<BYTE>(std::clamp(font_charset_, 0, 255));
	lf.lfPitchAndFamily = FIXED_PITCH | FF_MODERN;
	if (!font_name_.empty())
		lstrcpynW(lf.lfFaceName, font_name_.c_str(), LF_FACESIZE);

	CHOOSEFONTW cf{};
	cf.lStructSize = sizeof(cf);
	cf.hwndOwner = hwnd_;
	cf.lpLogFont = &lf;
	cf.Flags = CF_SCREENFONTS | CF_FIXEDPITCHONLY | CF_INITTOLOGFONTSTRUCT | CF_NOVERTFONTS |
			   CF_LIMITSIZE | CF_NOSTYLESEL;
	cf.nSizeMin = 6;  // пункты
	cf.nSizeMax = 36;
	if (ChooseFontW(&cf) == FALSE)
		return false;

	const float scale = std::max(1.0F, dpi_scale_);
	// Кегль: PointSize (в десятых пункта) точнее, чем lfHeight, который
	// округлён до пикселей экрана. Пункт = 1/72", DIP = 1/96" → ×96/72.
	float size = font_size_;
	if (cf.iPointSize > 0)
		size = static_cast<float>(cf.iPointSize) / 10.0F * (96.0F / 72.0F);
	else if (lf.lfHeight != 0)
		size = static_cast<float>(std::abs(lf.lfHeight)) / scale;
	size = std::clamp(size, 8.0F, 48.0F);
	font_name_ = lf.lfFaceName;
	font_charset_ = lf.lfCharSet;
	options_state_.font_size = size;
	options_state_.font_name = narrow(font_name_);
	// Применяем сразу — в окне настроек видно результат, как SampleEditor.
	if (renderer_.set_editor_font(font_name_, size, hwnd_))
	{
		font_size_ = renderer_.font_size();
		sync_metrics();
	}
	options_sync_view();
	invalidate();
	return true;
}

bool App::pick_mark_color(bool background)
{
	// ColorDialog оригинала. Значение — COLORREF, как TColor в ini.
	static COLORREF custom[16] = {};
	CHOOSECOLORW cc{};
	cc.lStructSize = sizeof(cc);
	cc.hwndOwner = hwnd_;
	cc.rgbResult = background ? back_mark_color_ : mark_color_;
	cc.lpCustColors = custom;
	cc.Flags = CC_FULLOPEN | CC_RGBINIT | CC_ANYCOLOR;
	if (ChooseColorW(&cc) == FALSE)
		return false;

	if (background)
	{
		back_mark_color_ = cc.rgbResult;
		options_state_.back_mark_color = cc.rgbResult;
	}
	else
	{
		mark_color_ = cc.rgbResult;
		options_state_.mark_color = cc.rgbResult;
	}
	apply_mark_colors();
	options_sync_view();
	invalidate();
	return true;
}

void App::show_about()
{
	// Текст — по AboutForm оригинала: версия, словари, счётчики, URL.
	std::wstring text = L"Ё (yo-c-) — ёфикатор русских текстов\n\n";
	text += L"Порт ёфикатора Владимира Иванова (https://github.com/vgiv/yo).\n";
	text += L"Основной словарь: ";
	text += widen(dictionary_display(false));
	if (dict_ != nullptr)
	{
		text += L" (";
		text += std::to_wstring(dict_->size());
		text += L" слов)";
	}
	text += L"\nСловарь регулярных выражений: ";
	text += widen(dictionary_display(true));
	if (regexdict_ != nullptr)
	{
		text += L" (";
		text += std::to_wstring(regexdict_->size());
		text += L" правил)";
	}
	text += L"\n\nГорячие клавиши — как в оригинале: F9 / Ctrl+F9 — ёфицировать\n";
	text += L"с текущей позиции / с начала текста, Ctrl+W / Ctrl+Q — переход по заменам,\n";
	text += L"Shift+F10 — убрать выделение, Ctrl+Alt+O — опции, Ctrl+F / Ctrl+R — найти.\n\n";
	text += L"URL: http://github.com/vgiv/yo\n";
	text += L"Настройки хранятся в yo.ini рядом с программой.";
	MessageBoxW(hwnd_, text.c_str(), L"О программе", MB_OK | MB_ICONINFORMATION);
}

void App::show_help_more()
{
	// Moreabout1 оригинала: ShellExecute(HelpURL) — страница проекта.
	constexpr wchar_t kHelpUrl[] = L"http://github.com/vgiv/yo";
	const HINSTANCE result =
		ShellExecuteW(hwnd_, L"open", kHelpUrl, nullptr, nullptr, SW_SHOWNORMAL);
	if (reinterpret_cast<INT_PTR>(result) <= 32)
		MessageBoxW(hwnd_, kHelpUrl, L"Ещё о программе", MB_OK | MB_ICONINFORMATION);
}

void App::show_file_props()
{
	if (buf_ == nullptr)
		return;
	std::wstring text;
	text += L"Имя: ";
	text += path_.empty() ? L"(файл ещё не сохранён)" : path_.wstring();
	text += L"\n\nРазмер в памяти: ";
	text += std::to_wstring(buf_->size());
	text += L" байт";
	if (!path_.empty())
	{
		std::error_code ec;
		const auto bytes = std::filesystem::file_size(path_, ec);
		if (!ec)
		{
			text += L"\nРазмер на диске: ";
			text += std::to_wstring(static_cast<unsigned long long>(bytes));
			text += L" байт";
		}
	}
	text += L"\nСтрок: ";
	text += std::to_wstring(buf_->line_count());
	text += L"\nКодировка: ";
	text += encoding_name(enc_, bom_);
	text += L"\nПеревод строк: ";
	text += eol_name(eol_);
	text += L"\nИзменён: ";
	text += buf_->modified() ? L"да" : L"нет";
	// Символы и степень ёфикации — как CalculateFileProp оригинала, но с
	// исправленной формулой (FEATURES.md §12.2: nYo/(nYe+nYo), а не nYo*1500/nYe).
	std::uint64_t cyrillic = 0;
	std::uint64_t ye = 0;
	std::uint64_t yo = 0;
	{
		const std::size_t chunk = 1U << 16;
		std::string data;
		for (std::size_t pos = 0; pos < buf_->size();)
		{
			const std::size_t take = std::min(chunk, buf_->size() - pos);
			buf_->read(pos, take, data);
			for (std::size_t i = 0; i + 1 < data.size(); ++i)
			{
				const auto c0 = static_cast<unsigned char>(data[i]);
				const auto c1 = static_cast<unsigned char>(data[i + 1]);
				if (c0 == 0xD1U && c1 == 0x91U) // ё
				{
					++yo;
					++cyrillic;
				}
				else if (c0 == 0xD0U && c1 == 0x81U) // Ё
				{
					++yo;
					++cyrillic;
				}
				else if (c0 == 0xD0U && (c1 == 0xB5U || c1 == 0x95U)) // е / Е
				{
					++ye;
					++cyrillic;
				}
				else if (c0 == 0xD0U && c1 >= 0x90U && c1 <= 0xBFU)
					++cyrillic; // остальная кириллица (А-я)
			}
			pos += take;
		}
	}
	const std::uint64_t letters = ye + yo;
	const std::uint64_t degree = letters > 0 ? (yo * 100 + letters / 2) / letters : 0;
	text += L"\nКириллицы: ";
	text += std::to_wstring(cyrillic);
	text += L"   «е»: ";
	text += std::to_wstring(ye);
	text += L"   «ё»: ";
	text += std::to_wstring(yo);
	text += L"\nСтепень ёфикации: ";
	text += std::to_wstring(degree);
	text += L"% — ";
	if (degree == 0)
		text += L"не ёфицировано";
	else if (degree < 75)
		text += L"возможно, ёфицировано не полностью";
	else
		text += L"возможно, ёфицировано";

	if (last_yofy_.lines > 0)
	{
		text += L"\n\nПоследняя ёфикация: замен ";
		text += std::to_wstring(last_yofy_.replaced);
		text += L", строк ";
		text += std::to_wstring(last_yofy_.lines);
		text += L", вопросов ";
		text += std::to_wstring(last_yofy_.asked);
	}
	MessageBoxW(hwnd_, text.c_str(), L"Свойства файла", MB_OK | MB_ICONINFORMATION);
}

// ---------------------------------------------------------------- ёфикация

namespace {

// Каталоги, где ищутся словари: рядом с exe, в data/ и в репозитории.
std::vector<std::filesystem::path> data_dirs()
{
	std::vector<std::filesystem::path> dirs;
	wchar_t exe[32768] = L"";
	if (GetModuleFileNameW(nullptr, exe, static_cast<DWORD>(std::size(exe))) > 0)
	{
		const std::filesystem::path exe_path(exe);
		dirs.push_back(exe_path.parent_path());
		dirs.push_back(exe_path.parent_path() / L"data");
		dirs.push_back(exe_path.parent_path() / L".." / L"tests" / L"data");
		dirs.push_back(exe_path.parent_path() / L".." / L".." / L"tests" / L"data");
	}
#ifdef YO_GUI_DATA_DIR
	dirs.emplace_back(std::filesystem::path(std::wstring(L"" YO_GUI_DATA_DIR)));
#endif
	return dirs;
}

std::filesystem::path find_data_file(const std::filesystem::path& name)
{
	for (const auto& dir : data_dirs())
	{
		std::error_code ec;
		const std::filesystem::path candidate = dir / name;
		if (std::filesystem::exists(candidate, ec))
			return candidate;
	}
	return {};
}

} // namespace

Options App::yofy_options() const
{
	// Все опции §ReadIni оригинала в одном месте: движок читает их при
	// создании, поэтому после смены настроек он пересоздаётся.
	Options opts;
	opts.always_ask = always_ask_;
	opts.mark = mark_;
	opts.check_yo = check_yo_;
	opts.no_var_only = no_var_only_;
	opts.var_only = var_only_;
	opts.propose_last = propose_last_;
	opts.fb_format = fb_format_;
	opts.regexs = regexs_;
	opts.to_confirm_abbr = to_confirm_abbr_;
	opts.to_confirm_ellipsis = to_confirm_ellipsis_;
	opts.to_confirm_cap = to_confirm_cap_;
	opts.lines_below = lines_below_;
	opts.words_buffer_length = 5; // WordsBufferLength оригинала
	return opts;
}

bool App::ensure_yoficator(bool quiet)
{
	if (yofy_)
		return true;

	// Пути из yo.ini (как в оригинале) важнее поиска рядом с exe; короткие
	// имена разрешаются от каталога программы.
	const std::filesystem::path dict_path = dictionary_path(false);
	const std::filesystem::path regex_path = dictionary_path(true);
	if (dict_path.empty() || !std::filesystem::exists(dict_path))
	{
		if (!quiet)
			MessageBoxW(hwnd_,
						L"Не найден словарь yox.dat.\n"
						L"Положите yox.dat и yo_regexpr.dat рядом с программой "
						L"(или в каталог data) и повторите.",
						L"Ё", MB_ICONWARNING);
		return false;
	}

	std::vector<LoadIssue> issues;
	auto dict = Dictionary::load(dict_path, &issues);
	if (!dict)
	{
		if (!quiet)
			MessageBoxW(hwnd_, L"Не удалось загрузить словарь yox.dat.", L"Ё", MB_ICONERROR);
		return false;
	}
	dict_ = std::make_unique<Dictionary>(std::move(*dict));
	load_user_selections(); // ответы «Всегда Е/Ё» из прошлых запусков

	if (!regex_path.empty())
	{
		auto regex = RegexDictionary::load(regex_path, &issues);
		if (regex)
			regexdict_ = std::make_unique<RegexDictionary>(std::move(*regex));
	}

	Options opts = yofy_options();
	yofy_ = std::make_unique<Yoficator>(*dict_, regexdict_.get(), opts);
	// Неоднозначные слова — в панель вопросов (как AskPanel оригинала).
	yofy_->set_ask([this](const AskRequest& req) { return ask_word(req); });
	set_always_ask(always_ask_);
	set_silent_yofy(silent_yofy_);
	return true;
}

void App::run_yofification(bool from_start)
{
	if (yofy_busy_ || ask_.active || options_open_)
		return; // проход уже идёт (или открыты настройки): повторный вход сломал бы состояние
	if (readonly_)
	{
		MessageBeep(MB_ICONASTERISK);
		return;
	}
	if (!ensure_yoficator())
		return;

	// F9 — «Ёфицировать с текущей позиции»: старт от каретки (Yopos1
	// оригинала берёт SelStart, а не начало выделения). Ctrl+F9 — с начала
	// текста (Yobegin1: DoYoficate(0, 1)). Если что-то выделено, проход идёт
	// по выделению: так удобнее и это не расходится с DoYoficate(LineN, PosN).
	std::size_t from = 0;
	std::size_t to = buf_->size();
	if (ed_->has_selection())
	{
		from = ed_->sel_begin();
		to = ed_->sel_end();
	}
	else if (!from_start)
	{
		from = ed_->caret();
	}
	if (from >= to)
		return;

	skipped_words_.clear();
	ask_count_ = 0;
	cancel_requested_ = false;
	SetCursor(LoadCursorW(nullptr, IDC_WAIT));
	sync_metrics();
	yofy_->stats().reset();

	yofy_line_ = buf_->line_of(from);
	yofy_lines_ = buf_->line_of(to - 1) + 1;
	yofy_busy_ = true;
	ask_.line = yofy_line_;

	// Проход идёт кусками по 2048 строк: память ограничена куском (а не
	// размером файла), прогресс и Esc работают на всём диапазоне, а все
	// правки по-прежнему попадают в одну группу undo.
	std::size_t cancel_tick = 0;
	const auto started = std::chrono::steady_clock::now();
	last_yofy_ = yofify_document(
		*buf_, from, to, *yofy_, 2048,
		[this](std::size_t line)
		{
			ask_.line = line;
			yofy_line_ = line;
			// Прогресс в статусе и живой интерфейс: без периодической
			// перерисовки окно выглядит зависшим.
			if (line % 64 == 0)
				invalidate();
		},
		[this, &cancel_tick]
		{
			// Обработка сообщений — не на каждой строке: PeekMessage
			// дороже разбора строки на коротких строках.
			if (++cancel_tick % 32 != 0)
				return false;
			return pump_cancel();
		});
	yofy_busy_ = false;
	last_stats_ = yofy_->stats();
	last_yofy_ms_ = std::chrono::duration<double, std::milli>(
		std::chrono::steady_clock::now() - started).count();
	trace("ёфикация", last_yofy_ms_, to - from);
	SetCursor(LoadCursorW(nullptr, IDC_IBEAM));

	// Нажали «закрыть» посреди прохода — доводим закрытие до конца.
	if (quitting_)
	{
		PostMessageW(hwnd_, WM_CLOSE, 0, 0);
		return;
	}

	if (ask_count_ > 0)
		save_user_selections(); // ответы «Всегда Е/Ё» переживут перезапуск
	// Проход закончился — подсветка последнего слова больше не нужна.
	ask_.has_word = false;

	if (last_yofy_.changed)
	{
		mark_yofy_changes(from, to);
		ed_->set_caret(std::min(from, buf_->size()));
		refresh_view();
	}
	else
	{
		refresh_view();
	}
}

bool App::pump_cancel()
{
	MSG msg{};
	while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
	{
		if (msg.message == WM_QUIT)
			return true;
		if (!ask_.active && msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE)
			return true;
		if (!ask_.active && msg.message == WM_CLOSE)
		{
			// Закрытие окна посреди ёфикации закрывает программу, а не
			// «отвечает на вопрос»: иначе пользователь жмёт крестик, а
			// вопросы продолжаются один за другим.
			quitting_ = true;
			return true;
		}
		if (msg.message == WM_COMMAND)
			return false; // меню во время прохода: команды не выполняем
		TranslateMessage(&msg);
		DispatchMessageW(&msg);
	}
	return false;
}

// ---------------------------------------------------------------- панель вопросов

void App::set_always_ask(bool on)
{
	always_ask_ = on;
	update_menu_checks();
	invalidate();
	if (yofy_)
	{
		Options opts = yofy_->options();
		opts.always_ask = on;
		yofy_->set_options(opts);
	}
}

void App::set_silent_yofy(bool on)
{
	silent_yofy_ = on;
	update_menu_checks();
	invalidate();
	if (yofy_)
	{
		if (on)
		{
			// Неоднозначные слова просто пропускаем: проход не встаёт на
			// каждом. Отсутствующий обработчик движок понял бы как «прервать».
			yofy_->set_ask([](const AskRequest&) { return AskAnswer::skip; });
		}
		else
		{
			yofy_->set_ask([this](const AskRequest& req) { return ask_word(req); });
		}
	}
}

void App::ask_build_view()
{
	ask_.rows.clear();
	ask_.buttons.clear();
	ask_.has_panel = false;
	ask_.inset = 0.0F;

	RECT rc{};
	GetClientRect(hwnd_, &rc);

	// Заголовок панели — только «Выберите верный вариант» (или причина):
	// счётчик вопросов и подсказки ушли, как просил пользователь.
	const AskTexts texts = make_ask_texts(ask_.req);
	// Раскладка панели — в DIP, как и всё остальное: GetClientRect отдаёт
	// физические пиксели, поэтому размеры делим на масштаб.
	const float scale = std::max(1.0F, renderer_.dpi_scale());
	const float client_w = static_cast<float>(std::max<LONG>(1, rc.right)) / scale;
	const float client_h = static_cast<float>(std::max<LONG>(1, rc.bottom)) / scale;
	AskLayoutInput in;
	in.client_w = client_w;
	in.client_h = client_h;
	in.status_h = renderer_.status_height();
	in.padding = renderer_.padding();
	// Меню, панель вопросов и статус рисуются шрифтом интерфейса и не должны
	// расти вместе с текстом: Ctrl+± меняет только текст.
	in.line_height = renderer_.ask_line_height();
	in.char_width = renderer_.ask_char_width();
	in.variants = texts.variants;
	in.reason = texts.reason;
	// Оригинал показывает панель полосой во всю ширину над статусом,
	// а не плавающим окошком по центру.
	in.bottom_bar = true;
	in.word1 = encode_line(ask_.req.word1, Encoding::utf8);
	in.word2 = encode_line(ask_.req.word2, Encoding::utf8);

	// Слово, о котором спрашивают: смещения в AskRequest — в кодовых точках
	// внутри строки (UString), а подсветке нужны байты всего текста.
	// Пересчёт — в ядре (yo::ask_word_bytes), он же покрыт тестами.
	const std::size_t ask_line = ask_.req.line;
	ask_.has_word = false;
	ask_.word_from = 0;
	ask_.word_to = 0;
	ask_.word_text.clear();
	if (ask_line < buf_->line_count())
	{
		const std::string_view text = buf_->line_text(ask_.req.line);
		const AskWordRange range = ask_word_bytes(text, ask_.req.offset, ask_.req.length);
		if (range.valid())
		{
			const std::size_t line_start = buf_->line_start(ask_.req.line);
			ask_.has_word = true;
			ask_.word_from = line_start + range.from;
			ask_.word_to = line_start + range.to;
			ask_.word_text = widen(std::string(text.substr(range.from, range.to - range.from)));
		}
	}

	// В панели — всё предложение с этим словом: по одному слову невозможно
	// понять, где Е, а где Ё.
	// Переносов нет: предложение идёт одной строкой во всю ширину полосы,
	// а слово в ней стоит по центру (рендерер сам подрезает края).
	in.context = sentence_around(*buf_, ask_line);

	// Ширину строк меряет тот же шрифт, которым рисуем панель.
	const AskLayout layout = layout_ask_panel(
		// Мерим тем же шрифтом, которым панель рисуется (fmt_ask_, а не
		// интерфейсный 12 DIP): иначе подписи кнопок не влезали.
		in, [this](std::string_view text) { return renderer_.measure_ask(widen(text)); });
	if (!layout.visible)
		return;

	// Полоса снизу уже стоит там, где надо; компактную панель (окно не
	// поместилось) ставим по центру, чтобы её не приняли за зависание.
	float dy = 0.0F;
	if (!layout.bottom)
	{
		const float area_h =
			std::max(1.0F, client_h - renderer_.status_height() - renderer_.bar_height());
		dy = std::max(0.0F, (area_h - layout.h) * 0.5F) - layout.y;
	}

	const float pad = std::max(2.0F, renderer_.padding());
	const float left = layout.x + pad;
	const float right = layout.x + layout.w - pad;
	for (std::size_t i = 0; i < layout.rows.size(); ++i)
	{
		AskRow row;
		row.text = widen(layout.rows[i].text);
		row.kind = layout.rows[i].kind == AskRowKind::title	  ? AskKind::accent
				   : layout.rows[i].kind == AskRowKind::variant ? AskKind::dim
																: AskKind::normal;
		row.rect = D2D1::RectF(left, layout.rows[i].y + dy, right, layout.rows[i].y + dy + layout.rows[i].h);
		// Слово внутри предложения выделяем и в панели — видно, о чём вопрос.
		// Ищем его по границам слова: иначе подсветится первое вхождение.
		if (!ask_.word_text.empty() && layout.rows[i].kind == AskRowKind::context)
		{
			const auto is_alpha = [](wchar_t c) { return (c >= L'А' && c <= L'я') || c == L'Ё' || c == L'ё'; };
			std::size_t at = row.text.find(ask_.word_text);
			while (at != std::wstring::npos)
			{
				const bool left_ok = at == 0 || !is_alpha(row.text[at - 1]);
				const std::size_t after = at + ask_.word_text.size();
				const bool right_ok = after >= row.text.size() || !is_alpha(row.text[after]);
				if (left_ok && right_ok)
					break;
				at = row.text.find(ask_.word_text, at + 1);
			}
			if (at != std::wstring::npos)
			{
				row.has_hot = true;
				row.hot_from = at;
				row.hot_to = at + ask_.word_text.size();
			}
		}
		ask_.rows.push_back(std::move(row));
	}

	for (const AskButtonBox& box : layout.buttons)
	{
		AskButton button;
		button.label = widen(box.label);
		button.answer = to_answer(box.choice);
		button.rect.left = static_cast<LONG>(box.x);
		button.rect.top = static_cast<LONG>(box.y + dy);
		button.rect.right = static_cast<LONG>(box.x + box.w);
		button.rect.bottom = static_cast<LONG>(box.y + dy + box.h);
		ask_.buttons.push_back(std::move(button));
	}

	ask_.panel.left = static_cast<LONG>(layout.x);
	ask_.panel.top = static_cast<LONG>(layout.y + dy);
	ask_.panel.right = static_cast<LONG>(layout.x + layout.w);
	ask_.panel.bottom = static_cast<LONG>(layout.y + dy + layout.h);
	ask_.has_panel = true;
	// Полоса снизу отнимает место у текста: строка с вопросом должна быть
	// видна над ней, а не под ней.
	ask_.inset = layout.bottom ? layout.h : 0.0F;
	// Видимая область — всё, что осталось от текста после строки меню,
	// статуса, полосы поиска и панели вопросов (те же слагаемые, что в
	// Renderer::paint, иначе ряд «уезжает» под панель).
	const float find_h = show_find_ ? renderer_.find_height() : 0.0F;
	ed_->set_viewport(renderer_.text_area_width(),
		std::max(1.0F, renderer_.text_area_height() - find_h - ask_.inset));
	// Прокручиваем к РЯДУ слова, а не к строке: при переносе слов строка
	// занимает несколько рядов, и слово оставалось под панелью вопросов.
	std::size_t anchor = 0;
	if (ask_.has_word)
		anchor = ask_.word_from;
	else if (ask_.req.line < buf_->line_count())
		anchor = buf_->line_start(ask_.req.line);
	scroll_row_into_view(ed_->row_of_offset(anchor), ask_.inset > 0.0F);
}

AskView App::ask_view() const
{
	AskView view;
	if (!ask_.has_panel)
		return view;
	view.panel = D2D1::RectF(static_cast<float>(ask_.panel.left), static_cast<float>(ask_.panel.top),
							static_cast<float>(ask_.panel.right), static_cast<float>(ask_.panel.bottom));
	view.rows = ask_.rows;
	view.has_word = ask_.has_word;
	view.word_from = ask_.word_from;
	view.word_to = ask_.word_to;
	view.bottom_inset = ask_.inset;
	view.buttons.reserve(ask_.buttons.size());
	for (const AskButton& b : ask_.buttons)
	{
		AskButtonView v;
		v.rect = D2D1::RectF(static_cast<float>(b.rect.left), static_cast<float>(b.rect.top),
							static_cast<float>(b.rect.right), static_cast<float>(b.rect.bottom));
		v.label = b.label;
		const auto index = static_cast<std::ptrdiff_t>(&b - ask_.buttons.data());
		v.focus = index == ask_.focus;
		v.hot = index == ask_.hot;
		v.tone = b.answer == AskAnswer::ye		 ? AskTone::ye
				 : b.answer == AskAnswer::yo	 ? AskTone::yo
												 : AskTone::neutral;
		view.buttons.push_back(std::move(v));
	}
	return view;
}

void App::scroll_row_into_view(std::size_t row, bool at_bottom)
{
	if (!ed_)
		return;
	// «Быстрая промотка» (Yo/FastScroll) — как в оригинале: текст под вопрос
	// не докручивается вообще (GoAndShowPosition вызывается только при
	// выключенной опции).
	if (fast_scroll_)
		return;
	// Расчёт — в ядре (Editor::scroll_delta_to_reveal_row): прокрутка в рядах,
	// снизу остаётся `lines_below` рядов промотки (Yo/LinesBelow).
	const long delta = ed_->scroll_delta_to_reveal_row(
		row, at_bottom, at_bottom ? std::clamp(lines_below_, 0, 10) : 0);
	if (delta != 0)
		ed_->scroll_lines(delta);
}

void App::scroll_line_into_view(std::size_t line)
{
	if (!ed_)
		return;
	const auto first = static_cast<long long>(ed_->first_visible_line());
	const auto count = std::max<long long>(1, static_cast<long long>(ed_->visible_line_count()));
	const auto target = static_cast<long long>(line);
	if (target < first)
		ed_->scroll_lines(static_cast<long>(target - first));
	else if (target >= first + count)
		ed_->scroll_lines(static_cast<long>(target - (first + count) + 1));
}

AskAnswer App::ask_word(const AskRequest& req)
{
	if (quitting_)
		return AskAnswer::cancel;

	// Про это слово уже ответили «пропустить» в текущем проходе — не спрашиваем
	// снова (иначе частые слова дают тысячи одинаковых панелей подряд).
	const std::string key = encode_line(req.word1, Encoding::utf8) + "\x1f" +
							encode_line(req.word2, Encoding::utf8);
	if (!skipped_words_.empty() && skipped_words_.contains(key))
		return AskAnswer::skip;

	++ask_count_;
	trace("вопрос", static_cast<double>(ask_count_));
	ask_.req = req;
	ask_.answered = false;
	ask_.answer = AskAnswer::cancel;
	ask_.hot = -1;
	ask_build_view();
	{
		const int ye = ask_.index_of(AskAnswer::ye);
		const int yo = ask_.index_of(AskAnswer::yo);
		ask_.focus = (req.focus_first || req.word2.empty()) ? std::max(ye, 0) : std::max(yo, 0);
	}
	scroll_line_into_view(ask_.line);
	// Если панель не поместилась (крошечное окно), модальный цикл ждал бы
	// ответа, которого негде нажать: отвечаем «пропустить» и идём дальше.
	if (!ask_.has_panel)
	{
		invalidate();
		return AskAnswer::skip;
	}
	ask_.active = true;
	SetCursor(LoadCursorW(nullptr, IDC_ARROW));
	invalidate();
	UpdateWindow(hwnd_);

	// Модальный цикл: панель живёт поверх документа, движок ждёт ответа.
	MSG msg{};
	while (!ask_.answered)
	{
		const BOOL got = GetMessageW(&msg, nullptr, 0, 0);
		if (got == 0)
		{
			// Приложение закрывается: не задаём больше вопросов.
			quitting_ = true;
			PostQuitMessage(static_cast<int>(msg.wParam));
			ask_.answer = AskAnswer::cancel;
			break;
		}
		if (got == -1)
			break;
		TranslateMessage(&msg);
		DispatchMessageW(&msg);
	}

	ask_.active = false;
	ask_.has_panel = false;
	ask_.answered = false;
	if (ask_.answer == AskAnswer::skip)
		skipped_words_.insert(key);
	SetCursor(LoadCursorW(nullptr, IDC_WAIT));
	invalidate();
	return ask_.answer;
}

void App::ask_answer(AskAnswer answer)
{
	if (!ask_.active || ask_.answered)
		return;
	ask_.answer = answer;
	ask_.answered = true;
	invalidate();
}

void App::ask_move_focus(int delta)
{
	const auto count = static_cast<int>(ask_.buttons.size());
	if (count <= 0)
		return;
	int next = ask_.focus + delta;
	next %= count;
	if (next < 0)
		next += count;
	ask_.focus = next;
	invalidate();
}

void App::ask_key(WPARAM vk, bool ctrl)
{
	(void)ctrl;
	if (!ask_.active)
		return;
	switch (vk)
	{
	case VK_ESCAPE:
		ask_answer(AskAnswer::cancel);
		break;
	case VK_LEFT:
	case VK_UP:
		ask_move_focus(-1);
		break;
	case VK_RIGHT:
	case VK_DOWN:
	case VK_TAB:
		ask_move_focus(GetKeyState(VK_SHIFT) < 0 ? -1 : 1);
		break;
	default:
		break;
	}
}

void App::ask_char(wchar_t ch)
{
	if (!ask_.active)
		return;
	if (ch >= L'1' && ch <= L'9')
	{
		const auto index = static_cast<std::size_t>(ch - L'1');
		if (index < ask_.buttons.size())
			ask_answer(ask_.buttons[index].answer);
		return;
	}
	if ((ch == L'\r' || ch == L'\n') && !ask_.buttons.empty())
	{
		const auto index = static_cast<std::size_t>(std::clamp(ask_.focus, 0,
			static_cast<int>(ask_.buttons.size()) - 1));
		ask_answer(ask_.buttons[index].answer);
	}
}

// Кнопка панели под точкой (координаты в DIP, как и её прямоугольники).
int App::ask_button_at(float x, float y) const
{
	for (std::size_t i = 0; i < ask_.buttons.size(); ++i)
	{
		const RECT& r = ask_.buttons[i].rect;
		if (x >= static_cast<float>(r.left) && x < static_cast<float>(r.right) && y >= static_cast<float>(r.top) &&
			y < static_cast<float>(r.bottom))
			return static_cast<int>(i);
	}
	return -1;
}

void App::track_mouse_leave()
{
	if (mouse_tracking_ || hwnd_ == nullptr)
		return;
	TRACKMOUSEEVENT tme{};
	tme.cbSize = sizeof(tme);
	tme.dwFlags = TME_LEAVE;
	tme.hwndTrack = hwnd_;
	mouse_tracking_ = TrackMouseEvent(&tme) != FALSE;
}

void App::clear_hover()
{
	if (ask_.hot < 0 && options_hot_ < 0 && dict_hot_ < 0 && hot_item_ < 0 && hot_title_ < 0)
		return;
	ask_.hot = -1;
	options_hot_ = -1;
	dict_hot_ = -1;
	hot_item_ = -1;
	hot_title_ = -1;
	for (FindBarButtonView& b : find_view_.buttons)
		b.hot = false;
	invalidate();
}

void App::ask_mouse_move(float x, float y)
{
	if (!ask_.active)
		return;
	const int hot = ask_button_at(x, y);
	if (hot == ask_.hot)
		return;
	ask_.hot = hot;
	if (hot >= 0)
		ask_.focus = hot;  // Enter подтверждает ту кнопку, на которую смотрит мышь
	SetCursor(LoadCursorW(nullptr, hot >= 0 ? IDC_HAND : IDC_ARROW));
	invalidate();
}

void App::ask_click(float x, float y)
{
	if (!ask_.active)
		return;
	const int hit = ask_button_at(x, y);
	if (hit < 0)
		return;  // клик мимо кнопок не отвечает на вопрос
	ask_.focus = hit;
	ask_answer(ask_.buttons[static_cast<std::size_t>(hit)].answer);
}

// ---------------------------------------------------------------- файлы

void App::set_document(std::shared_ptr<TextBuffer> buf, const std::filesystem::path& path, Encoding enc,
					   bool bom)
{
	buf_ = std::move(buf);
	ed_ = std::make_unique<Editor>(buf_);
	path_ = path;
	enc_ = enc;
	bom_ = bom;
	eol_ = dominant_eol(*buf_);
	readonly_ = false;
	show_find_ = false;
	find_query_.clear();
	find_replace_text_.clear();
	find_marks_.clear();
	find_hits_.clear();
	find_error_.clear();
	find_index_ = 0;
	find_total_ = 0;
	find_replace_row_ = false;
	yofy_marks_.clear();
	yofy_mark_index_ = 0;
	sync_metrics();
	ed_->move_doc_start();
	refresh_view();
}

void App::new_document()
{
	mru_remember_current(); // позиция покидаемого файла — в MRU
	set_document(std::make_shared<TextBuffer>(), {}, Encoding::utf8, false);
}

bool App::load_from_path(const std::filesystem::path& path)
{
	std::error_code ec;
	if (!std::filesystem::exists(path, ec))
	{
		MessageBoxW(hwnd_, L"Файл не найден.", L"Ё", MB_ICONWARNING);
		return false;
	}
	if (is_rtf_path(path))
		MessageBoxW(hwnd_, L"RTF-файлы на этом этапе открываются только для чтения.", L"Ё", MB_ICONINFORMATION);

	const std::string head = read_head(path, 4096);
	EncodingInfo info = detect_encoding(
		{ reinterpret_cast<const std::uint8_t*>(head.data()), head.size() }, DetectOptions{});

	if (info.enc == Encoding::utf8 && !info.bom)
	{
		// Быстрый путь: UTF-8 без BOM отображается в память как есть.
		auto mapped = MappedFile::open(path_utf8(path));
		if (!mapped)
		{
			MessageBoxW(hwnd_, L"Не удалось открыть файл.", L"Ё", MB_ICONERROR);
			return false;
		}
		set_document(std::make_shared<TextBuffer>(mapped), path, info.enc, info.bom);
		readonly_ = is_rtf_path(path);
		mru_touch(path, 0);
		return true;
	}

	auto in = open_binary(path);
	if (!in)
	{
		MessageBoxW(hwnd_, L"Не удалось открыть файл.", L"Ё", MB_ICONERROR);
		return false;
	}
	if (info.bom)
		in->seekg(is_utf16(info.enc) ? 2 : 3);

	LineReader reader(std::move(in), info.enc);
	std::string text;
	UString line;
	std::string eol;
	while (reader.next(line, eol))
	{
		text += encode_line(line, Encoding::utf8);
		text += eol;
	}
	set_document(std::make_shared<TextBuffer>(std::make_shared<MemorySource>(std::move(text))), path, info.enc,
				 info.bom);
	if (is_rtf_path(path))
		readonly_ = true;
	return true;
}

bool App::load_forced(const std::filesystem::path& path, Encoding enc, bool bom)
{
	std::error_code ec;
	if (!std::filesystem::exists(path, ec))
		return false;
	auto in = open_binary(path);
	if (!in)
	{
		MessageBoxW(hwnd_, L"Не удалось открыть файл.", L"Ё", MB_ICONERROR);
		return false;
	}
	if (bom)
		in->seekg(is_utf16(enc) ? 2 : 3);

	LineReader reader(std::move(in), enc);
	std::string text;
	UString line;
	std::string eol;
	while (reader.next(line, eol))
	{
		text += encode_line(line, Encoding::utf8);
		text += eol;
	}
	set_document(std::make_shared<TextBuffer>(std::make_shared<MemorySource>(std::move(text))), path, enc,
				 bom);
	readonly_ = is_rtf_path(path);
	mru_touch(path, 0);
	return true;
}

void App::reload_document()
{
	if (path_.empty())
		return;
	if (buf_ && buf_->modified() && !ask_save_changes())
		return;
	load_forced(path_, enc_, bom_);
}

void App::apply_encoding(Encoding enc, bool bom)
{
	enc_ = enc;
	bom_ = bom;
	// Для открытого неизменённого файла сразу показываем результат: читаем
	// диск заново выбранной кодировкой. Изменённый документ не трогаем —
	// иначе правки потерялись бы; кодировка применится при сохранении.
	if (!path_.empty() && buf_ && !buf_->modified())
		load_forced(path_, enc_, bom_);
	else
		refresh_view();
}

void App::apply_eol(Eol eol)
{
	eol_ = eol;
	refresh_view();
}

void App::ask_current_word()
{
	if (yofy_busy_ || ask_.active)
		return;
	if (!ed_ || buf_->line_count() == 0)
		return;
	if (readonly_)
	{
		MessageBeep(MB_ICONASTERISK);
		return;
	}
	if (!ensure_yoficator())
		return;

	// Слово под курсором спрашиваем тем же движком, что и полную ёфикацию:
	// временно включаем «спрашивать про все слова», чтобы панель вопросов
	// появилась гарантированно, даже если слово не числится неоднозначным.
	const std::size_t line_no = buf_->line_of(ed_->caret());
	const std::string_view text = buf_->line_text(line_no);
	const std::size_t line_start = buf_->line_start(line_no);
	const std::size_t at = ed_->caret() - line_start;

	std::size_t begin = std::min(at, text.size());
	while (begin > 0 && !is_word_break(static_cast<unsigned char>(text[begin - 1])))
		--begin;
	std::size_t end = std::min(at, text.size());
	while (end < text.size() && !is_word_break(static_cast<unsigned char>(text[end])))
		++end;
	if (end <= begin)
	{
		MessageBeep(MB_ICONASTERISK);
		return;
	}

	skipped_words_.clear();
	ask_count_ = 0;
	const bool saved_always_ask = always_ask_;
	set_always_ask(true);
	ask_.line = line_no;
	SetCursor(LoadCursorW(nullptr, IDC_WAIT));
	yofy_->stats().reset();
	last_yofy_ = yofify_range(*buf_, line_start + begin, line_start + end, *yofy_,
							  [this](std::size_t line) { ask_.line = line; });
	SetCursor(LoadCursorW(nullptr, IDC_IBEAM));
	set_always_ask(saved_always_ask);

	if (last_yofy_.changed)
	{
		ed_->set_caret(line_start + begin);
		refresh_view();
	}
	else
	{
		invalidate();
	}
}

bool App::open_dialog()
{
	mru_remember_current();
	std::vector<wchar_t> file(32768, L'\0');
	OPENFILENAMEW ofn{};
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = hwnd_;
	ofn.lpstrFilter = L"Текстовые файлы\0*.txt;*.text;*.md;*.log\0Все файлы\0*.*\0";
	ofn.lpstrFile = file.data();
	ofn.nMaxFile = static_cast<DWORD>(file.size());
	ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER;
	if (GetOpenFileNameW(&ofn) == FALSE)
		return false;
	return load_from_path(std::filesystem::path(file.data()));
}

bool App::write_document(const std::filesystem::path& path)
{
	const std::filesystem::path tmp = path.string() + ".yo-tmp";
	{
		std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
		if (!out)
		{
			MessageBoxW(hwnd_, L"Не удалось записать файл.", L"Ё", MB_ICONERROR);
			return false;
		}
		LineWriter writer(out, enc_, bom_);
		const std::size_t lines = buf_->line_count();
		for (std::size_t i = 0; i < lines; ++i)
		{
			const std::string_view bytes = buf_->line_text(i);
			const UString text = decode_line({ reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size() },
											 Encoding::utf8);
			writer.write_line(text, eol_bytes(buf_->line_eol(i)));
		}
		out.flush();
		if (!out)
		{
			MessageBoxW(hwnd_, L"Ошибка записи файла.", L"Ё", MB_ICONERROR);
			return false;
		}
	}
	std::error_code ec;
	std::filesystem::rename(tmp, path, ec);
	if (ec)
	{
		std::filesystem::remove(path, ec);
		ec.clear();
		std::filesystem::rename(tmp, path, ec);
	}
	if (ec)
	{
		std::filesystem::remove(tmp, ec);
		MessageBoxW(hwnd_, L"Не удалось заменить файл.", L"Ё", MB_ICONERROR);
		return false;
	}
	buf_->mark_saved();
	update_title();
	invalidate();
	return true;
}

bool App::save()
{
	if (path_.empty())
		return save_as();
	return write_document(path_);
}

bool App::save_as()
{
	std::vector<wchar_t> file(32768, L'\0');
	if (!path_.empty())
	{
		const std::wstring name = path_.wstring();
		const std::size_t n = std::min(name.size(), file.size() - 1);
		std::copy_n(name.begin(), static_cast<std::ptrdiff_t>(n), file.begin());
	}
	OPENFILENAMEW ofn{};
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = hwnd_;
	ofn.lpstrFilter = L"Текстовые файлы\0*.txt\0Все файлы\0*.*\0";
	ofn.lpstrFile = file.data();
	ofn.nMaxFile = static_cast<DWORD>(file.size());
	ofn.Flags = OFN_OVERWRITEPROMPT | OFN_EXPLORER;
	ofn.lpstrDefExt = L"txt";
	if (GetSaveFileNameW(&ofn) == FALSE)
		return false;
	path_ = std::filesystem::path(file.data());
	if (!write_document(path_))
		return false;
	mru_touch(path_, ed_ ? ed_->caret() : 0);
	update_title();
	return true;
}

bool App::ask_save_changes()
{
	if (!buf_->modified())
		return true;
	const int answer = MessageBoxW(hwnd_, L"Сохранить изменения?", L"Ё",
								   MB_YESNOCANCEL | MB_ICONQUESTION);
	if (answer == IDCANCEL)
		return false;
	if (answer == IDYES)
		return save();
	return true;
}

void App::on_drop(HDROP drop)
{
	const UINT count = DragQueryFileW(drop, 0xFFFFFFFFU, nullptr, 0);
	if (count > 0)
	{
		wchar_t file[32768] = L"";
		if (DragQueryFileW(drop, 0, file, static_cast<UINT>(std::size(file))) > 0)
		{
			if (ask_save_changes())
				load_from_path(std::filesystem::path(file));
		}
	}
	DragFinish(drop);
}

// ---------------------------------------------------------------- сообщения

LRESULT App::on_message(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
	// Пока панель вопросов ждёт ответа, ввод и команды идут только ей.
	if (ask_.active)
	{
		switch (msg)
		{
		case WM_CHAR:
			ask_char(static_cast<wchar_t>(wp));
			return 0;
		case WM_KEYDOWN:
			ask_key(wp, GetKeyState(VK_CONTROL) < 0);
			return 0;
		case WM_LBUTTONDOWN:
		{
			const float scale = std::max(1.0F, renderer_.dpi_scale());
			ask_click(static_cast<float>(GET_X_LPARAM(lp)) / scale,
				static_cast<float>(GET_Y_LPARAM(lp)) / scale);
			return 0;
		}
		case WM_MOUSEMOVE:
		{
			track_mouse_leave();
			const float scale = std::max(1.0F, renderer_.dpi_scale());
			ask_mouse_move(static_cast<float>(GET_X_LPARAM(lp)) / scale,
				static_cast<float>(GET_Y_LPARAM(lp)) / scale);
			return 0;
		}
		case WM_LBUTTONUP:
		case WM_LBUTTONDBLCLK:
		case WM_MOUSEWHEEL:
		case WM_CONTEXTMENU:
		case WM_DROPFILES:
		case WM_COMMAND:
		case WM_SYSCOMMAND:
			return 0;
		case WM_CLOSE:
			// Крестик посреди ёфикации — это желание закрыть программу, а не
			// «ответить на текущий вопрос». Прежнее поведение превращало
			// закрытие в бесконечную череду вопросов.
			quitting_ = true;
			ask_answer(AskAnswer::cancel);
			return 0;
		default:
			break;
		}
	}

	switch (msg)
	{
	case WM_CREATE:
		on_create();
		return 0;
	case WM_SIZE:
		if (wp != SIZE_MINIMIZED)
			on_size();
		return 0;
	case WM_PAINT:
		on_paint();
		return 0;
	case WM_ERASEBKGND:
		return 1;
	case WM_GETMINMAXINFO:
	{
		auto* mmi = reinterpret_cast<MINMAXINFO*>(lp);
		mmi->ptMinTrackSize.x = 420;
		mmi->ptMinTrackSize.y = 240;
		return 0;
	}
	case WM_DPICHANGED:
		on_dpi_changed(wp, lp);
		return 0;
	case WM_TIMER:
		if (wp == kCaretTimer)
		{
			on_timer();
			return 0;
		}
		break;
	case WM_SETFOCUS:
	{
		caret_on_ = true;
		const UINT blink = GetCaretBlinkTime();
		SetTimer(hwnd_, kCaretTimer, (blink == 0 || blink == INFINITE) ? 530U : blink, nullptr);
		invalidate();
		return 0;
	}
	case WM_KILLFOCUS:
		// Ховер-подсветка не должна оставаться на кнопках, когда окно
		// потеряло фокус (пользователь ушёл в другое приложение).
		clear_hover();
		caret_on_ = false;
		KillTimer(hwnd_, kCaretTimer);
		invalidate();
		return 0;
	case WM_MOUSELEAVE:
		// Мышь ушла из окна — снимаем подсветку и просим сообщение снова
		// при следующем входе (TrackMouseEvent ниже).
		mouse_tracking_ = false;
		clear_hover();
		return 0;
	case WM_CHAR:
		on_char(static_cast<wchar_t>(wp));
		return 0;
	case WM_KEYDOWN:
		if (open_menu_ >= 0 || wp == VK_F10)
		{
			menu_key(wp);
			return 0;
		}
		on_key_down(wp, GetKeyState(VK_CONTROL) < 0, GetKeyState(VK_SHIFT) < 0);
		return 0;
	case WM_LBUTTONDOWN:
	{
		// Сообщения мыши приходят в физических пикселях, рисуем мы в DIP.
		const float scale = std::max(1.0F, renderer_.dpi_scale());
		const float x = static_cast<float>(GET_X_LPARAM(lp)) / scale;
		const float y = static_cast<float>(GET_Y_LPARAM(lp)) / scale;
		const bool extend = GetKeyState(VK_SHIFT) < 0;
		SetFocus(hwnd_);
		if (menu_click(x, y))
			return 0;
		if (dict_click(x, y))
			return 0;
		if (options_click(x, y))
			return 0;
		if (find_click(x, y))
			return 0;
		on_lbutton_down(x, y, extend);
		return 0;
	}
	case WM_LBUTTONUP:
		on_mouse_up();
		return 0;
	case WM_RBUTTONDOWN:
	{
		// Правый клик вне выделения ставит курсор (как в любом редакторе):
		// контекстное меню тогда относится к слову под мышью.
		const float scale = std::max(1.0F, renderer_.dpi_scale());
		const float x = static_cast<float>(GET_X_LPARAM(lp)) / scale;
		const float y = static_cast<float>(GET_Y_LPARAM(lp)) / scale;
		if (y < renderer_.bar_height())
			return 0;
		const std::size_t hit = caret_offset_at(x, y);
		if (!ed_->has_selection() || hit < ed_->sel_begin() || hit > ed_->sel_end())
		{
			ed_->set_caret(hit);
			invalidate();
		}
		SetFocus(hwnd_);
		return 0;
	}
	case WM_CONTEXTMENU:
		on_context_menu(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
		return 0;
	case WM_MOUSEMOVE:
	{
		track_mouse_leave();
		const float scale = std::max(1.0F, renderer_.dpi_scale());
		const float x = static_cast<float>(GET_X_LPARAM(lp)) / scale;
		const float y = static_cast<float>(GET_Y_LPARAM(lp)) / scale;
		SetCursor(LoadCursorW(nullptr, y < renderer_.bar_height() ? IDC_ARROW : IDC_IBEAM));
		if (menu_mouse_move(x, y))
			return 0;
		if (dict_mouse_move(x, y))
			return 0;
		if (options_mouse_move(x, y))
			return 0;
		if (find_mouse_move(x, y))
			return 0;
		on_mouse_move(x, y);
		return 0;
	}
	case WM_MOUSEWHEEL:
		on_mouse_wheel(GET_WHEEL_DELTA_WPARAM(wp));
		return 0;
	case WM_VSCROLL:
		on_vscroll(LOWORD(wp));
		return 0;
	case WM_HSCROLL:
		on_hscroll(LOWORD(wp));
		return 0;
	case WM_COMMAND:
		// HIWORD == 0 — команда из нашего меню, HIWORD == 1 — из таблицы
		// акселераторов (TranslateAccelerator всегда ставит 1). Раньше вторые
		// молча отбрасывались, поэтому Ctrl+C/V/A, F9 и остальные «горячие»
		// клавиши не работали.
		on_command(LOWORD(wp));
		return 0;
	case WM_DROPFILES:
		on_drop(reinterpret_cast<HDROP>(wp));
		return 0;
	case WM_CLOSE:
		if (!ask_save_changes())
			return 0;
		save_settings(); // шрифт, положение окна, опции ёфикации — в yo.ini
		KillTimer(hwnd_, kCaretTimer);
		DestroyWindow(hwnd_);
		return 0;
	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;
	default:
		break;
	}
	return DefWindowProcW(hwnd, msg, wp, lp);
}

} // namespace yo::gui
