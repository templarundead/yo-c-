// Точка входа GUI: DPI-осведомлённость, COM, окно.
#include "app.hpp"

#include <commctrl.h>
#include <windows.h>

#include <shellapi.h>

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
