# GUI-редактор: голый Win32 + Direct2D/DirectWrite (только Windows).
add_executable(yo-gui WIN32
	main.cpp
	app.cpp
	render.cpp
)

target_link_libraries(yo-gui PRIVATE yo::core yo_warnings yo_sanitizers)

target_compile_definitions(yo-gui PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)

if(MSVC)
	target_link_libraries(yo-gui PRIVATE d2d1 dwrite dwmapi comdlg32 shell32 ole32 user32 gdi32 uuid
		uxtheme comctl32)
else()
	# mingw-w64 / zig: те же библиотеки, но флагами линковщика.
	target_link_libraries(yo-gui PRIVATE d2d1 dwrite dwmapi comdlg32 shell32 ole32 user32 gdi32 uuid
		uxtheme comctl32)
endif()

# Манифест: Common Controls 6 (системные полосы прокрутки и меню в оформлении
# Windows), UTF-8, per-monitor DPI. Без него окно выглядит как из Windows 95.
if(MSVC)
	target_link_options(yo-gui PRIVATE
		"/MANIFEST:EMBED"
		"/MANIFESTINPUT:${CMAKE_CURRENT_SOURCE_DIR}/yo-gui.manifest")
elseif(CMAKE_RC_COMPILER)
	# mingw/zig: манифест едет ресурсом (RT_MANIFEST). Кросс-сборка без
	# компилятора ресурсов собирается без манифеста — это проверочная
	# сборка, поставочные артефакты делает clang-cl (ветка MSVC выше).
	target_sources(yo-gui PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/yo-gui.rc)
endif()

# Каталог с данными (словари) для сборки из репозитория: запасной путь
# поиска yox.dat рядом с exe.
target_compile_definitions(yo-gui PRIVATE YO_GUI_DATA_DIR="${CMAKE_SOURCE_DIR}/tests/data")

set_target_properties(yo-gui PROPERTIES CXX_STANDARD 23 CXX_STANDARD_REQUIRED ON)
yo_apply_release_tweaks(yo-gui)
