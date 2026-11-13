// Точка входа GUI: DPI-осведомлённость, COM, окно.
#include "app.hpp"

#include <commctrl.h>
#include <windows.h>

#include <shellapi.h>

#include <cstdint>
#include <iterator>
#include <string>

#ifndef DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
#define DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 (reinterpret_cast<DPI_AWARENESS_CONTEXT>(-4))
#endif

namespace {

// Per-monitor DPI v2, если система умеет; иначе — системная осведомлённость.
void enable_dpi_awareness()
{
	using SetCtxFn = BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT);
	if (HMODULE user32 = GetModuleHandleW(L"user32"))
	{
		const auto fn = reinterpret_cast<SetCtxFn>(
			reinterpret_cast<void*>(GetProcAddress(user32, "SetProcessDpiAwarenessContext")));
		if (fn != nullptr && fn(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2) != FALSE)
			return;
	}
	SetProcessDPIAware();
}

// Падение: пишем в %TEMP%\yo-crash.log код исключения, адрес и стек
// (смещения от базы модуля — по ним видно функцию даже без PDB), затем
// отдаём исключение системе как обычно. Без этого «белое окно и закрылось»
// нечем разбирать.
LONG WINAPI yo_crash_filter(EXCEPTION_POINTERS* info)
{
	wchar_t temp[32768] = L"";
	if (GetTempPathW(static_cast<DWORD>(std::size(temp)), temp) == 0)
		return EXCEPTION_CONTINUE_SEARCH;
	const std::wstring path = std::wstring(temp) + L"yo-crash.log";
	HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
							  CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (file != INVALID_HANDLE_VALUE)
	{
		const auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
		char text[1024] = "";
		const auto record = info != nullptr ? info->ExceptionRecord : nullptr;
		const unsigned long code =
			record != nullptr ? static_cast<unsigned long>(record->ExceptionCode) : 0UL;
		const auto address = record != nullptr ? reinterpret_cast<std::uintptr_t>(record->ExceptionAddress)
											 : 0U;
			// Адреса — смещениями от базы exe: так они сопоставимы со сборкой.
		int n = wsprintfA(text, "исключение 0x%08lx, адрес +0x%08llx (база 0x%08llx)\r\n"
						 "стек:\r\n",
						  code, static_cast<unsigned long long>(address - base),
						  static_cast<unsigned long long>(base));
			if (n > 0)
				WriteFile(file, text, static_cast<DWORD>(n), nullptr, nullptr);
		void* frames[48] = {};
		const USHORT depth = CaptureStackBackTrace(0, static_cast<DWORD>(std::size(frames)), frames,
											   nullptr);
		for (USHORT i = 0; i < depth; ++i)
		{
			const auto off = reinterpret_cast<std::uintptr_t>(frames[i]) - base;
			n = wsprintfA(text, "  #%u +0x%08llx\r\n", static_cast<unsigned>(i),
						  static_cast<unsigned long long>(off));
			if (n > 0)
				WriteFile(file, text, static_cast<DWORD>(n), nullptr, nullptr);
		}
		CloseHandle(file);
	}
	return EXCEPTION_CONTINUE_SEARCH; // обычная обработка (WER) остаётся
}

std::wstring first_argument()
{
	int argc = 0;
	wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
	if (argv == nullptr)
		return {};
	std::wstring first;
	if (argc > 1)
		first = argv[1];
	LocalFree(argv);
	return first;
}

} // namespace

int WINAPI WinMain(HINSTANCE hinst, HINSTANCE, LPSTR, int show)
{
	enable_dpi_awareness();
	SetUnhandledExceptionFilter(&yo_crash_filter);
	// Common Controls 6 из манифеста: системные полосы прокрутки и меню
	// рисуются в оформлении Windows, а не в классическом стиле.
	INITCOMMONCONTROLSEX icc{};
	icc.dwSize = sizeof(icc);
	icc.dwICC = ICC_STANDARD_CLASSES | ICC_BAR_CLASSES;
	InitCommonControlsEx(&icc);
	if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)))
		return 1;

	int rc = 1;
	{
		yo::gui::App app;
		if (app.create(hinst, show, first_argument()))
			rc = app.run();
	}
	CoUninitialize();
	return rc;
}
