# GUI-редактор: голый Win32 + Direct2D/DirectWrite (только Windows).
add_executable(yo-gui WIN32
	main.cpp
	app.cpp
	render.cpp
)

target_link_libraries(yo-gui PRIVATE yo::core yo_warnings yo_sanitizers)

target_compile_definitions(yo-gui PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)

if(MSVC)
	target_link_libraries(yo-gui PRIVATE d2d1 dwrite dwmapi comdlg32 shell32 ole32 user32 gdi32 uuid)
else()
	# mingw-w64 / zig: те же библиотеки, но флагами линковщика.
	target_link_libraries(yo-gui PRIVATE d2d1 dwrite dwmapi comdlg32 shell32 ole32 user32 gdi32 uuid)
endif()

set_target_properties(yo-gui PROPERTIES CXX_STANDARD 23 CXX_STANDARD_REQUIRED ON)
