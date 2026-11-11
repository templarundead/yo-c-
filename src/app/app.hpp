// Главное окно редактора: Win32-сообщения, файлы, меню, буфер обмена, поиск.
//
// Вся логика редактирования — в yo::Editor (тестируется без Windows),
// раскладка и рисование — в Renderer. Здесь только склейка: сообщения окна,
// открытие/сохранение в исходной кодировке, буфер обмена, поиск, прокрутка.
#pragma once

#include "render.hpp"

#include "yo/codec.hpp"
#include "yo/editor.hpp"
#include "yo/textbuf.hpp"

#include <windows.h>
#include <shellapi.h> // HDROP

#include <filesystem>
#include <memory>
#include <string>

namespace yo::gui {

class App
{
public:
	App();
	~App();
	App(const App&) = delete;
	App& operator=(const App&) = delete;

	// Создаёт окно и (если задан) открывает файл. false — фатальная ошибка.
	bool create(HINSTANCE hinst, int cmd_show, const std::wstring& file);
	int run();

private:
	static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
	LRESULT on_message(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

	void on_create();
	void on_size();
	void on_paint();
	void on_dpi_changed(WPARAM wp, LPARAM lp);
	void on_timer();
	void on_key_down(WPARAM vk, bool ctrl, bool shift);
	void on_char(wchar_t ch);
	void on_lbutton_down(float x, float y, bool extend);
	void on_mouse_move(float x, float y);
	void on_mouse_up();
	void on_mouse_wheel(int delta);
	void on_vscroll(int code);
	void on_hscroll(int code);
	void on_command(int id);
	void on_drop(HDROP drop);

	void new_document();
	bool open_dialog();
	bool open_path(const std::filesystem::path& path);
	bool load_from_path(const std::filesystem::path& path);
	bool save();
	bool save_as();
	bool write_document(const std::filesystem::path& path);
	bool ask_save_changes();

	void setup_menu();
	void setup_accels();
	void update_title();
	void update_scrollbars();
	void sync_metrics();
	void invalidate();
	void refresh_view();

	[[nodiscard]] std::wstring status_text() const;
	[[nodiscard]] std::string insert_eol_bytes() const;

	void clipboard_copy(bool cut);
	void clipboard_paste();
	void find_toggle();
	void find_next();
	[[nodiscard]] std::size_t find_from(std::size_t from, const std::string& needle) const;

	void set_document(std::shared_ptr<TextBuffer> buf, const std::filesystem::path& path, Encoding enc,
					  bool bom);

	HINSTANCE hinst_ = nullptr;
	HWND hwnd_ = nullptr;
	HACCEL accel_ = nullptr;
	std::shared_ptr<TextBuffer> buf_;
	std::unique_ptr<Editor> ed_;
	Renderer renderer_;

	std::filesystem::path path_;
	Encoding enc_ = Encoding::utf8;
	bool bom_ = false;
	Eol eol_ = Eol::lf;
	bool readonly_ = false;

	float font_size_ = 15.0F;
	float dpi_scale_ = 1.0F;
	bool caret_on_ = true;
	bool dragging_ = false;
	bool show_find_ = false;
	std::string find_query_;
	char32_t pending_high_ = 0;
};

} // namespace yo::gui
