#include "app.hpp"

#include "yo/utf.hpp"
#include "yo/mapped.hpp"

#include <dwmapi.h>
#include <shellapi.h>
#include <windowsx.h>
#include <cstring>

#include <algorithm>
#include <array>
#include <cstdio>
#include <fstream>
#include <string_view>
#include <vector>

// commdlg.h требует windows.h раньше; App.hpp его уже включил.
#include <commdlg.h>

namespace yo::gui {

namespace {

constexpr int kCmdOpen = 1001;
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
	AppendMenuW(file, MF_STRING, kCmdOpen, L"&Открыть…\tCtrl+O");
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

	const HMENU view = CreatePopupMenu();
	AppendMenuW(view, MF_STRING, kCmdFontBigger, L"&Крупнее\tCtrl++");
	AppendMenuW(view, MF_STRING, kCmdFontSmaller, L"&Мельче\tCtrl+-");
	AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(view), L"&Вид");

	SetMenu(hwnd_, bar);
}

void App::setup_accels()
{
	ACCEL accels[] = {
		{ FCONTROL | FVIRTKEY, 'O', kCmdOpen },
		{ FCONTROL | FVIRTKEY, 'S', kCmdSave },
		{ FCONTROL | FSHIFT | FVIRTKEY, 'S', kCmdSaveAs },
		{ FCONTROL | FVIRTKEY, 'Z', kCmdUndo },
		{ FCONTROL | FVIRTKEY, 'Y', kCmdRedo },
		{ FCONTROL | FVIRTKEY, 'X', kCmdCut },
		{ FCONTROL | FVIRTKEY, 'C', kCmdCopy },
		{ FCONTROL | FVIRTKEY, 'V', kCmdPaste },
		{ FCONTROL | FVIRTKEY, 'A', kCmdSelectAll },
		{ FCONTROL | FVIRTKEY, 'F', kCmdFind },
		{ FVIRTKEY, VK_F3, kCmdFindNext },
		{ FCONTROL | FVIRTKEY, VK_OEM_PLUS, kCmdFontBigger },
		{ FCONTROL | FVIRTKEY, VK_OEM_MINUS, kCmdFontSmaller },
	};
	if (accel_ != nullptr)
		DestroyAcceleratorTable(accel_);
	accel_ = CreateAcceleratorTableW(accels, static_cast<int>(std::size(accels)));
}

// ---------------------------------------------------------------- вид

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
	update_scrollbars();
	update_title();
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

void App::update_scrollbars()
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
	SetScrollInfo(hwnd_, SB_VERT, &sv, TRUE);

	const float cw = std::max(1.0F, renderer_.char_width());
	SCROLLINFO sh{};
	sh.cbSize = sizeof(sh);
	sh.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
	sh.nMin = 0;
	sh.nMax = static_cast<int>(std::max(1.0F, ed_->text_width() / cw));
	sh.nPage = static_cast<UINT>(std::max(1.0F, renderer_.text_area_width() / cw));
	sh.nPos = static_cast<int>(ed_->scroll_x() / cw);
	SetScrollInfo(hwnd_, SB_HORZ, &sh, TRUE);
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
	renderer_.paint(*ed_, status_text(), show_find_, widen(find_query_), true, caret_on_);
	// Рендер мог подкрутить вид, чтобы курсор остался в окне.
	update_scrollbars();
	EndPaint(hwnd_, &ps);
}

void App::on_size()
{
	RECT rc{};
	GetClientRect(hwnd_, &rc);
	renderer_.resize(static_cast<UINT>(std::max<LONG>(1, rc.right)), static_cast<UINT>(std::max<LONG>(1, rc.bottom)));
	sync_metrics();
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
	const float padding = renderer_.padding();
	const float doc_x = x - padding + ed_->scroll_x();
	const float doc_y = y - ed_->scroll_y();
	ed_->click_at(doc_x, doc_y, extend);
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
	ed_->click_at(x - renderer_.padding() + ed_->scroll_x(), y - ed_->scroll_y(), true);
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
	const int lines = -delta / WHEEL_DELTA * 3;
	ed_->scroll_lines(lines);
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
	if (readonly_ || !OpenClipboard(hwnd_))
		return;
	HANDLE data = GetClipboardData(CF_UNICODETEXT);
	std::string utf8;
	if (data != nullptr)
	{
		const auto* wide = static_cast<const wchar_t*>(GlobalLock(data));
		if (wide != nullptr)
		{
			utf8 = wide_to_utf8(wide);
			GlobalUnlock(data);
		}
	}
	CloseClipboard();
	if (utf8.empty())
		return;
	ed_->insert_text(normalize_newlines(utf8, eol_));
	update_scrollbars();
	update_title();
	invalidate();
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
