#include "app.hpp"

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
#include <cstdio>
#include <fstream>
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

constexpr UINT_PTR kCaretTimer = 1;
constexpr int kDefaultWidth = 1000;
constexpr int kDefaultHeight = 720;

std::wstring widen(std::string_view utf8) { return utf8_to_wide(utf8); }

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

bool App::create(HINSTANCE hinst, int cmd_show, const std::wstring& file)
{
	hinst_ = hinst;

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

	hwnd_ = CreateWindowExW(0, L"YoCEditorWindow", L"Ё (yo-c-)", WS_OVERLAPPEDWINDOW | WS_VSCROLL | WS_HSCROLL,
							CW_USEDEFAULT, CW_USEDEFAULT, kDefaultWidth, kDefaultHeight, nullptr, nullptr,
							hinst, this);
	if (hwnd_ == nullptr)
		return false;

	ShowWindow(hwnd_, cmd_show);
	UpdateWindow(hwnd_);

	if (!file.empty())
	{
		load_from_path(std::filesystem::path(file));
		refresh_view();
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
	sync_metrics();

	setup_menu();
	setup_accels();

	RECT rc{};
	GetClientRect(hwnd_, &rc);
	renderer_.resize(static_cast<UINT>(rc.right - rc.left), static_cast<UINT>(rc.bottom - rc.top));
	update_scrollbars();
	update_title();

	const UINT blink = GetCaretBlinkTime();
	SetTimer(hwnd_, kCaretTimer, (blink == 0 || blink == INFINITE) ? 530U : blink, nullptr);
}

void App::setup_menu()
{
	const HMENU bar = CreateMenu();

	const HMENU file = CreatePopupMenu();
	AppendMenuW(file, MF_STRING, kCmdNew, L"&Создать\tCtrl+N");
	AppendMenuW(file, MF_STRING, kCmdOpen, L"&Открыть…\tCtrl+O");
	AppendMenuW(file, MF_STRING, kCmdReload, L"Пере&читать с диска\tCtrl+R");
	AppendMenuW(file, MF_STRING, kCmdSave, L"&Сохранить\tCtrl+S");
	AppendMenuW(file, MF_STRING, kCmdSaveAs, L"Сохранить &как…\tCtrl+Shift+S");
	AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
	AppendMenuW(file, MF_STRING, kCmdExit, L"В&ыход\tAlt+F4");
	AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(file), L"&Файл");

	const HMENU edit = CreatePopupMenu();
	AppendMenuW(edit, MF_STRING, kCmdUndo, L"&Отменить\tCtrl+Z");
	AppendMenuW(edit, MF_STRING, kCmdRedo, L"Пов&торить\tCtrl+Y");
	AppendMenuW(edit, MF_SEPARATOR, 0, nullptr);
	AppendMenuW(edit, MF_STRING, kCmdCut, L"&Вырезать\tCtrl+X");
	AppendMenuW(edit, MF_STRING, kCmdCopy, L"&Копировать\tCtrl+C");
	AppendMenuW(edit, MF_STRING, kCmdPaste, L"В&ставить\tCtrl+V");
	AppendMenuW(edit, MF_SEPARATOR, 0, nullptr);
	AppendMenuW(edit, MF_STRING, kCmdSelectAll, L"Выделить &всё\tCtrl+A");
	AppendMenuW(edit, MF_SEPARATOR, 0, nullptr);
	AppendMenuW(edit, MF_STRING, kCmdFind, L"&Найти…\tCtrl+F");
	AppendMenuW(edit, MF_STRING, kCmdFindNext, L"Найти &далее\tF3");
	AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(edit), L"&Правка");

	const HMENU yofy = CreatePopupMenu();
	AppendMenuW(yofy, MF_STRING, kCmdYofify, L"&Ёфицировать\tCtrl+E");
	AppendMenuW(yofy, MF_SEPARATOR, 0, nullptr);
	AppendMenuW(yofy, MF_STRING, kCmdAskWord, L"Спросить про слово под &курсором\tCtrl+Shift+E");
	AppendMenuW(yofy, MF_STRING, kCmdSilentYofy, L"&Без вопросов (неоднозначные пропускать)");
	AppendMenuW(yofy, MF_STRING, kCmdAlwaysAsk, L"Спрашивать про &все слова");
	AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(yofy), L"&Ёфикация");

	const HMENU enc = CreatePopupMenu();
	AppendMenuW(enc, MF_STRING, kCmdEncCp1251, L"CP1251 (Windows)");
	AppendMenuW(enc, MF_STRING, kCmdEncUtf8, L"UTF-8");
	AppendMenuW(enc, MF_STRING, kCmdEncUtf8Bom, L"UTF-8 с BOM");
	AppendMenuW(enc, MF_STRING, kCmdEncUtf16le, L"UTF-16LE");
	AppendMenuW(enc, MF_STRING, kCmdEncUtf16be, L"UTF-16BE");
	enc_menu_ = enc;
	AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(enc), L"&Кодировка");

	const HMENU eol = CreatePopupMenu();
	AppendMenuW(eol, MF_STRING, kCmdEolLf, L"LF (Unix)");
	AppendMenuW(eol, MF_STRING, kCmdEolCrlf, L"CRLF (Windows)");
	AppendMenuW(eol, MF_STRING, kCmdEolCr, L"CR (Mac)");
	eol_menu_ = eol;
	AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(eol), L"Пере&вод строк");

	const HMENU view = CreatePopupMenu();
	AppendMenuW(view, MF_STRING, kCmdFontBigger, L"&Крупнее\tCtrl++");
	AppendMenuW(view, MF_STRING, kCmdFontSmaller, L"&Мельче\tCtrl+-");
	AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(view), L"&Вид");

	SetMenu(hwnd_, bar);
}

void App::setup_accels()
{
	ACCEL accels[] = {
		{ FCONTROL | FVIRTKEY, 'N', kCmdNew },
		{ FCONTROL | FVIRTKEY, 'O', kCmdOpen },
		{ FCONTROL | FVIRTKEY, 'R', kCmdReload },
		{ FCONTROL | FVIRTKEY, 'S', kCmdSave },
		{ FCONTROL | FSHIFT | FVIRTKEY, 'S', kCmdSaveAs },
		{ FCONTROL | FVIRTKEY, 'Z', kCmdUndo },
		{ FCONTROL | FVIRTKEY, 'Y', kCmdRedo },
		{ FCONTROL | FVIRTKEY, 'X', kCmdCut },
		{ FCONTROL | FVIRTKEY, 'C', kCmdCopy },
		{ FCONTROL | FVIRTKEY, 'V', kCmdPaste },
		{ FCONTROL | FVIRTKEY, 'A', kCmdSelectAll },
		{ FCONTROL | FVIRTKEY, 'F', kCmdFind },
		{ FCONTROL | FVIRTKEY, 'E', kCmdYofify },
		{ FCONTROL | FSHIFT | FVIRTKEY, 'E', kCmdAskWord },
		{ FVIRTKEY, VK_F3, kCmdFindNext },
		{ FCONTROL | FVIRTKEY, VK_OEM_PLUS, kCmdFontBigger },
		{ FCONTROL | FVIRTKEY, VK_OEM_MINUS, kCmdFontSmaller },
	};
	if (accel_ != nullptr)
		DestroyAcceleratorTable(accel_);
	accel_ = CreateAcceleratorTableW(accels, static_cast<int>(std::size(accels)));
}

// ---------------------------------------------------------------- вид

void App::update_menu_checks()
{
	int enc_id = kCmdEncUtf8;
	switch (enc_)
	{
	case Encoding::cp1251:
		enc_id = kCmdEncCp1251;
		break;
	case Encoding::utf8:
		enc_id = bom_ ? kCmdEncUtf8Bom : kCmdEncUtf8;
		break;
	case Encoding::utf16le:
		enc_id = kCmdEncUtf16le;
		break;
	case Encoding::utf16be:
		enc_id = kCmdEncUtf16be;
		break;
	}
	int eol_id = kCmdEolLf;
	if (eol_ == Eol::crlf)
		eol_id = kCmdEolCrlf;
	else if (eol_ == Eol::cr)
		eol_id = kCmdEolCr;

	if (enc_menu_ != nullptr)
		CheckMenuRadioItem(enc_menu_, static_cast<UINT>(kCmdEncCp1251), static_cast<UINT>(kCmdEncUtf16be),
						   static_cast<UINT>(enc_id), MF_BYCOMMAND);
	if (eol_menu_ != nullptr)
		CheckMenuRadioItem(eol_menu_, static_cast<UINT>(kCmdEolLf), static_cast<UINT>(kCmdEolCr),
						   static_cast<UINT>(eol_id), MF_BYCOMMAND);
}

void App::sync_metrics()
{
	Metrics m;
	m.char_width = renderer_.char_width();
	m.line_height = renderer_.line_height();
	m.padding_x = 0.0F;
	ed_->set_metrics(m);
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
	sv.nMax = static_cast<int>(std::max<std::size_t>(1, buf_->line_count()) - 1);
	sv.nPage = static_cast<UINT>(std::max<std::size_t>(1, ed_->visible_line_count()));
	sv.nPos = static_cast<int>(ed_->first_visible_line());
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
		}
		if (ask_count_ > 0)
		{
			text += L", вопросов ";
			text += std::to_wstring(ask_count_);
		}
		text += L"   Esc — прервать";
	}
	if (last_yofy_.rolled_back)
		text += L"   ёфикация: откат, изменений нет";
	else if (last_yofy_.lines > 0)
	{
		text += L"   ёфикация: замен ";
		text += std::to_wstring(last_yofy_.replaced);
		text += L", строк ";
		text += std::to_wstring(last_yofy_.lines);
		if (last_yofy_.cancelled)
			text += L" (прервано)";
	}
	if (show_find_ && find_query_.empty())
		text += L"   введите образец, Enter — найти, Esc — закрыть";
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
	const auto paint_started = std::chrono::steady_clock::now();
	renderer_.paint(*ed_, status_text(), show_find_, widen(find_query_), true, caret_on_,
					ask_.active && ask_.has_panel ? &view : nullptr);
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

	if (show_find_)
	{
		if (ch == L'\r' || ch == L'\n')
		{
			find_next();
			return;
		}
		if (ch >= 0x20)
		{
			find_query_ += utf16_to_utf8(std::u16string(1, static_cast<char16_t>(ch)));
			invalidate();
		}
		return;
	}

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
	if (show_find_)
	{
		if (vk == VK_ESCAPE)
		{
			find_toggle();
			return;
		}
		if (vk == VK_RETURN)
		{
			find_next();
			return;
		}
		if (vk == VK_BACK)
		{
			// Удаляем последний символ UTF-8 образца.
			std::size_t cut = find_query_.size();
			while (cut > 0 && (static_cast<unsigned char>(find_query_[cut - 1]) & 0xC0U) == 0x80U)
				--cut;
			if (cut > 0)
				--cut;
			find_query_.erase(cut);
			invalidate();
			return;
		}
		if (vk == VK_LEFT || vk == VK_RIGHT || vk == VK_UP || vk == VK_DOWN || vk == VK_HOME ||
			vk == VK_END || vk == VK_PRIOR || vk == VK_NEXT || vk == VK_DELETE)
			return;
	}
	// Сочетания обрабатываем и здесь, а не только через таблицу акселераторов:
	// TranslateAccelerator легко «теряет» нажатия (например, когда фокус у
	// диалога или таблица не создалась), и тогда Ctrl+C/V/Z молча не работают.
	if (ctrl && !ask_.active)
	{
		switch (vk)
		{
		case 'N':
			on_command(kCmdNew);
			return;
		case 'O':
			on_command(kCmdOpen);
			return;
		case 'R':
			on_command(kCmdReload);
			return;
		case 'S':
			on_command(shift ? kCmdSaveAs : kCmdSave);
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
		case 'F':
			on_command(kCmdFind);
			return;
		case 'E':
			on_command(shift ? kCmdAskWord : kCmdYofify);
			return;
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

void App::on_lbutton_down(float x, float y, bool extend)
{
	// Экран → документ живёт в ядре (offset_at_client): при прокрутке
	// прокрутку нужно ПРИБАВЛЯТЬ, иначе курсор встаёт выше клика на
	// 2*scroll_y/lh строк и выделение «съезжает» на несколько строк.
	ed_->set_caret(ed_->offset_at_client(x, y, renderer_.padding()), extend);
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
	ed_->set_caret(ed_->offset_at_client(x, y, renderer_.padding()), true);
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
	long target = static_cast<long>(ed_->first_visible_line());
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
		target = static_cast<long>(buf_->line_count());
		break;
	default:
		return;
	}
	ed_->scroll_lines(target - static_cast<long>(ed_->first_visible_line()));
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

	POINT pt{ x, y };
	if (x == -1 && y == -1)
	{
		// Меню с клавиатуры (Shift+F10 / клавиша «меню»): ставим у курсора.
		const CaretPos pos = ed_->caret_pos();
		pt.x = static_cast<LONG>(renderer_.padding() + ed_->caret_x() - ed_->scroll_x());
		pt.y = static_cast<LONG>(ed_->line_top(pos.line - 1) - ed_->scroll_y() + renderer_.line_height());
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

	SetForegroundWindow(hwnd_); // иначе меню не закрывается по клику мимо
	TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_LEFTALIGN | TPM_TOPALIGN, pt.x, pt.y, 0, hwnd_,
				   nullptr);
	PostMessageW(hwnd_, WM_NULL, 0, 0); // известная «заплатка» меню Win32
	DestroyMenu(menu);
}

// ---------------------------------------------------------------- команды

void App::on_command(int id)
{
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
		find_toggle();
		break;
	case kCmdFindNext:
		find_next();
		break;
	case kCmdYofify:
		run_yofification();
		break;
	case kCmdAlwaysAsk:
		set_always_ask(!always_ask_);
		break;
	case kCmdFontBigger:
		if (renderer_.set_font_size(font_size_ + 1.0F, hwnd_))
		{
			font_size_ = renderer_.font_size();
			refresh_view();
		}
		break;
	case kCmdFontSmaller:
		if (renderer_.set_font_size(font_size_ - 1.0F, hwnd_))
		{
			font_size_ = renderer_.font_size();
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

void App::find_toggle()
{
	show_find_ = !show_find_;
	invalidate();
}

std::size_t App::find_from(std::size_t from, const std::string& needle) const
{
	if (needle.empty() || from >= buf_->size())
		return std::string::npos;
	const std::size_t chunk = 1U << 16;
	const std::size_t overlap = needle.size() > 1 ? needle.size() - 1 : 0;
	std::string window;
	std::size_t pos = from;
	while (pos < buf_->size())
	{
		const std::size_t take = std::min(chunk, buf_->size() - pos);
		buf_->read(pos, take, window);
		const std::size_t hit = window.find(needle);
		if (hit != std::string::npos)
			return pos + hit;
		if (pos + take >= buf_->size())
			break;
		// Сдвиг с перекрытием: образец может лежать на границе кусков.
		pos += take - std::min(overlap, take);
	}
	return std::string::npos;
}

void App::find_next()
{
	if (find_query_.empty())
		return;
	std::size_t start = ed_->has_selection() ? ed_->sel_end() : ed_->caret();
	std::size_t hit = find_from(start, find_query_);
	if (hit == std::string::npos && start > 0)
		hit = find_from(0, find_query_);
	if (hit == std::string::npos)
	{
		MessageBeep(MB_ICONASTERISK);
		return;
	}
	ed_->set_caret(hit);
	ed_->set_caret(hit + find_query_.size(), true);
	caret_on_ = true;
	update_scrollbars();
	invalidate();
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

bool App::ensure_yoficator()
{
	if (yofy_)
		return true;

	const std::filesystem::path dict_path = find_data_file(L"yox.dat");
	const std::filesystem::path regex_path = find_data_file(L"yo_regexpr.dat");
	if (dict_path.empty())
	{
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
		MessageBoxW(hwnd_, L"Не удалось загрузить словарь yox.dat.", L"Ё", MB_ICONERROR);
		return false;
	}
	dict_ = std::make_unique<Dictionary>(std::move(*dict));

	if (!regex_path.empty())
	{
		auto regex = RegexDictionary::load(regex_path, &issues);
		if (regex)
			regexdict_ = std::make_unique<RegexDictionary>(std::move(*regex));
	}

	Options opts;
	opts.mark = true;
	opts.always_ask = always_ask_;
	yofy_ = std::make_unique<Yoficator>(*dict_, regexdict_.get(), opts);
	// Неоднозначные слова — в панель вопросов (как AskPanel оригинала).
	yofy_->set_ask([this](const AskRequest& req) { return ask_word(req); });
	set_always_ask(always_ask_);
	set_silent_yofy(silent_yofy_);
	return true;
}

void App::run_yofification()
{
	if (yofy_busy_ || ask_.active)
		return; // проход уже идёт: повторный вход сломал бы состояние
	if (readonly_)
	{
		MessageBeep(MB_ICONASTERISK);
		return;
	}
	if (!ensure_yoficator())
		return;

	const std::size_t from = ed_->has_selection() ? ed_->sel_begin() : 0;
	const std::size_t to = ed_->has_selection() ? ed_->sel_end() : buf_->size();
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

	const auto started = std::chrono::steady_clock::now();
	last_yofy_ = yofify_range(
		*buf_, from, to, *yofy_,
		[this](std::size_t line)
		{
			ask_.line = line;
			yofy_line_ = line;
			// Прогресс в статусе и живой интерфейс: без периодической
			// перерисовки и обработки сообщений окно выглядит зависшим.
			if (line % 32 == 0)
				invalidate();
		},
		[this] { return pump_cancel(); });
	yofy_busy_ = false;
	trace("ёфикация", std::chrono::duration<double, std::milli>(
						  std::chrono::steady_clock::now() - started).count(),
		  to - from);
	SetCursor(LoadCursorW(nullptr, IDC_IBEAM));

	// Нажали «закрыть» посреди прохода — доводим закрытие до конца.
	if (quitting_)
	{
		PostMessageW(hwnd_, WM_CLOSE, 0, 0);
		return;
	}

	if (last_yofy_.changed)
	{
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
	if (const HMENU menu = GetMenu(hwnd_))
		CheckMenuItem(menu, static_cast<UINT>(kCmdAlwaysAsk), MF_BYCOMMAND | (on ? MF_CHECKED : MF_UNCHECKED));
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
	if (const HMENU menu = GetMenu(hwnd_))
		CheckMenuItem(menu, static_cast<UINT>(kCmdSilentYofy), MF_BYCOMMAND | (on ? MF_CHECKED : MF_UNCHECKED));
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

	RECT rc{};
	GetClientRect(hwnd_, &rc);

	AskTexts texts = make_ask_texts(ask_.req);
	if (ask_count_ > 0)
	{
		texts.reason += "   (вопрос ";
		texts.reason += std::to_string(ask_count_);
		texts.reason += ", Esc — прервать)";
	}
	AskLayoutInput in;
	in.client_w = static_cast<float>(std::max<LONG>(1, rc.right));
	in.client_h = static_cast<float>(std::max<LONG>(1, rc.bottom));
	in.status_h = renderer_.status_height();
	in.padding = renderer_.padding();
	in.line_height = renderer_.line_height();
	in.char_width = renderer_.char_width();
	in.variants = texts.variants;
	in.reason = texts.reason;

	std::string context = "стр. " + std::to_string(ask_.line + 1) + ": ";
	if (ask_.line < buf_->line_count())
	{
		const std::string_view bytes = buf_->line_text(ask_.line);
		std::size_t begin = 0;
		while (begin < bytes.size() && bytes[begin] == '\t')
			++begin;
		std::size_t end = bytes.size();
		while (end > begin && (bytes[end - 1] == '\r' || bytes[end - 1] == '\n'))
			--end;
		context += std::string(bytes.substr(begin, end - begin));
	}
	in.context = std::move(context);

	// Ширину строк меряет тот же шрифт, которым рисуем панель.
	const AskLayout layout = layout_ask_panel(
		in, [this](std::string_view text) { return renderer_.measure_ui(widen(text)); });
	if (!layout.visible)
		return;

	// Панель ставим по центру текстовой области. Прижатая к низу, она терялась
	// среди текста, а программа в это время ждала ответа — со стороны это
	// выглядело как зависание.
	const float area_h = std::max(1.0F, static_cast<float>(rc.bottom) - renderer_.status_height());
	const float dy = std::max(0.0F, (area_h - layout.h) * 0.5F) - layout.y;

	const float pad = std::max(2.0F, renderer_.padding());
	const float left = layout.x + pad;
	const float right = layout.x + layout.w - pad;
	for (std::size_t i = 0; i < layout.rows.size(); ++i)
	{
		AskRow row;
		row.text = widen(layout.rows[i].text);
		row.kind = i == 0 ? AskKind::normal : (i == 1 ? AskKind::accent : AskKind::dim);
		row.rect = D2D1::RectF(left, layout.rows[i].y + dy, right, layout.rows[i].y + dy + layout.rows[i].h);
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
}

AskView App::ask_view() const
{
	AskView view;
	if (!ask_.has_panel)
		return view;
	view.panel = D2D1::RectF(static_cast<float>(ask_.panel.left), static_cast<float>(ask_.panel.top),
							static_cast<float>(ask_.panel.right), static_cast<float>(ask_.panel.bottom));
	view.rows = ask_.rows;
	view.buttons.reserve(ask_.buttons.size());
	for (const AskButton& b : ask_.buttons)
	{
		AskButtonView v;
		v.rect = D2D1::RectF(static_cast<float>(b.rect.left), static_cast<float>(b.rect.top),
							static_cast<float>(b.rect.right), static_cast<float>(b.rect.bottom));
		v.label = b.label;
		v.focus = static_cast<std::ptrdiff_t>(&b - ask_.buttons.data()) == ask_.focus;
		view.buttons.push_back(std::move(v));
	}
	return view;
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

void App::ask_click(float x, float y)
{
	if (!ask_.active)
		return;
	for (const AskButton& b : ask_.buttons)
	{
		if (x >= static_cast<float>(b.rect.left) && x < static_cast<float>(b.rect.right) &&
			y >= static_cast<float>(b.rect.top) && y < static_cast<float>(b.rect.bottom))
		{
			ask_answer(b.answer);
			return;
		}
	}
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
	sync_metrics();
	ed_->move_doc_start();
	refresh_view();
}

void App::new_document()
{
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
	return write_document(path_);
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
			ask_click(static_cast<float>(GET_X_LPARAM(lp)), static_cast<float>(GET_Y_LPARAM(lp)));
			return 0;
		case WM_LBUTTONUP:
		case WM_MOUSEMOVE:
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
		caret_on_ = false;
		KillTimer(hwnd_, kCaretTimer);
		invalidate();
		return 0;
	case WM_CHAR:
		on_char(static_cast<wchar_t>(wp));
		return 0;
	case WM_KEYDOWN:
		on_key_down(wp, GetKeyState(VK_CONTROL) < 0, GetKeyState(VK_SHIFT) < 0);
		return 0;
	case WM_LBUTTONDOWN:
	{
		const bool extend = GetKeyState(VK_SHIFT) < 0;
		SetFocus(hwnd_);
		on_lbutton_down(static_cast<float>(GET_X_LPARAM(lp)), static_cast<float>(GET_Y_LPARAM(lp)), extend);
		return 0;
	}
	case WM_LBUTTONUP:
		on_mouse_up();
		return 0;
	case WM_RBUTTONDOWN:
	{
		// Правый клик вне выделения ставит курсор (как в любом редакторе):
		// контекстное меню тогда относится к слову под мышью.
		const float x = static_cast<float>(GET_X_LPARAM(lp));
		const float y = static_cast<float>(GET_Y_LPARAM(lp));
		const std::size_t hit = ed_->offset_at_client(x, y, renderer_.padding());
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
		on_mouse_move(static_cast<float>(GET_X_LPARAM(lp)), static_cast<float>(GET_Y_LPARAM(lp)));
		return 0;
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
		if (HIWORD(wp) == 0)
		{
			on_command(LOWORD(wp));
			return 0;
		}
		break;
	case WM_DROPFILES:
		on_drop(reinterpret_cast<HDROP>(wp));
		return 0;
	case WM_CLOSE:
		if (!ask_save_changes())
			return 0;
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
