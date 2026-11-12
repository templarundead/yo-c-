// Главное окно редактора: Win32-сообщения, файлы, меню, буфер обмена, поиск.
//
// Вся логика редактирования — в yo::Editor (тестируется без Windows),
// раскладка и рисование — в Renderer. Здесь только склейка: сообщения окна,
// открытие/сохранение в исходной кодировке, буфер обмена, поиск, прокрутка.
#pragma once

#include "render.hpp"

#include "yo/codec.hpp"
#include "yo/dict.hpp"
#include "yo/editor.hpp"
#include "yo/regexdict.hpp"
#include "yo/textbuf.hpp"
#include "yo/askpanel.hpp"
#include "yo/yofy.hpp"
#include "yo/yofytext.hpp"

#include <windows.h>
#include <shellapi.h> // HDROP

#include <filesystem>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

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
	void on_context_menu(int x, int y);
	void on_vscroll(int code);
	void on_hscroll(int code);
	void on_command(int id);
	void on_drop(HDROP drop);

	void new_document();
	bool open_dialog();
	bool open_path(const std::filesystem::path& path);
	bool load_from_path(const std::filesystem::path& path);
	// Чтение файла заданной кодировкой (меню «Кодировка», Ctrl+R).
	bool load_forced(const std::filesystem::path& path, Encoding enc, bool bom);
	void reload_document();
	void apply_encoding(Encoding enc, bool bom);
	void apply_eol(Eol eol);
	void ask_current_word();
	bool save();
	bool save_as();
	bool write_document(const std::filesystem::path& path);
	bool ask_save_changes();

	// Своя строка меню: системное меню на тёмной теме остаётся белым.
	struct MenuEntry
	{
		std::wstring label;
		int cmd = 0;
		bool separator = false;
		bool checked = false;
		bool enabled = true;
	};

	struct MenuDef
	{
		std::wstring title;
		std::vector<MenuEntry> entries;
	};

	void build_menus();
	void setup_accels();
	void update_menu_checks();
	void layout_menus();
	[[nodiscard]] MenuBarView menu_bar_view();
	// Возвращает true, если клик/движение обработаны строкой меню.
	bool menu_mouse_move(float x, float y);
	bool menu_click(float x, float y);
	void menu_close();
	void menu_key(WPARAM vk);
	void update_title();
	// redraw=false — вызов из WM_PAINT: не просим систему перерисовывать
	// полосы повторно (иначе лишние проходы отрисовки при каждой прокрутке).
	void update_scrollbars(bool redraw = true);
	void sync_metrics();
	void invalidate();
	void refresh_view();

	[[nodiscard]] std::wstring status_text() const;
	[[nodiscard]] std::string insert_eol_bytes() const;

	bool ensure_yoficator();
	void run_yofification();
	void set_always_ask(bool on);
	// «Ёфикация без вопросов»: неоднозначные слова пропускаются молча.
	void set_silent_yofy(bool on);
	// Обработка нажатий во время долгого прохода: Esc/закрытие окна прерывают
	// ёфикацию (иначе окно выглядит зависшим).
	bool pump_cancel();

	// Панель вопросов ёфикации (аналог AskPanel оригинала): движок спрашивает
	// про неоднозначное слово, панель показывает варианты и ждёт ответа
	// в собственном модальном цикле сообщений.
	struct AskButton
	{
		RECT rect{};
		std::wstring label;
		AskAnswer answer = AskAnswer::cancel;
	};

	struct AskState
	{
		bool active = false;
		bool answered = false;
		AskAnswer answer = AskAnswer::cancel;
		AskRequest req{};
		std::size_t line = 0;	   // строка, на которой спросили
		RECT panel{};
		bool has_panel = false;
		float inset = 0.0F;  // высота полосы снизу (DIP), 0 — панель плавающая
		std::vector<AskRow> rows;  // текст панели (геометрия уже посчитана)
		std::vector<AskButton> buttons;
		int hot = -1;  // кнопка под мышью (-1 — нет)
		// Обрабатываемое слово — байтовые смещения в тексте документа:
		// рендерер подсвечивает его, как AskPanel оригинала.
		bool has_word = false;
		std::size_t word_from = 0;
		std::size_t word_to = 0;
		std::wstring word_text;  // само слово — чтобы подсветить его в панели
		int focus = 0;			   // кнопка под Enter

		[[nodiscard]] int index_of(AskAnswer wanted) const
		{
			for (std::size_t i = 0; i < buttons.size(); ++i)
			{
				if (buttons[i].answer == wanted)
					return static_cast<int>(i);
			}
			return -1;
		}
	};

	[[nodiscard]] AskAnswer ask_word(const AskRequest& req);
	void ask_build_view();
	[[nodiscard]] AskView ask_view() const;
	void ask_answer(AskAnswer answer);
	void ask_move_focus(int delta);
	void ask_key(WPARAM vk, bool ctrl);
	void ask_char(wchar_t ch);
	void ask_click(float x, float y);
	void ask_mouse_move(float x, float y);
	[[nodiscard]] int ask_button_at(float x, float y) const;
	void scroll_line_into_view(std::size_t line);
	// Экран (DIP) → смещение в документе: текст начинается ниже строки меню.
	[[nodiscard]] std::size_t caret_offset_at(float x, float y) const;

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
	std::vector<MenuDef> menus_;
	std::vector<D2D1_RECT_F> menu_title_rects_;
	int open_menu_ = -1;  // индекс открытого меню
	int hot_item_ = -1;	  // подсвеченный пункт выпадающего списка
	int hot_title_ = -1;  // подсвеченный заголовок
	std::vector<D2D1_RECT_F> menu_item_rects_;
	D2D1_RECT_F menu_panel_{};
	std::shared_ptr<TextBuffer> buf_;
	std::unique_ptr<Editor> ed_;
	Renderer renderer_;

	std::unique_ptr<Dictionary> dict_;
	std::unique_ptr<RegexDictionary> regexdict_;
	std::unique_ptr<Yoficator> yofy_;
	YofyResult last_yofy_{};
	AskState ask_;

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
	int wheel_accum_ = 0; // остаток дельты колеса (тачпады шлют меньше 120)
	bool always_ask_ = false; // «спрашивать про все слова» (Yo/AlwaysAsk)
	bool silent_yofy_ = false; // «без вопросов»
	// Слова, про которые уже ответили «пропустить» в текущем проходе: повторно
	// не спрашиваем. Иначе документ с частым словом даёт тысячи одинаковых
	// вопросов — выглядит как зависание.
	std::unordered_set<std::string> skipped_words_;
	std::size_t ask_count_ = 0;	  // сколько вопросов задано в текущем проходе
	bool yofy_busy_ = false;	  // проход идёт (для прогресса в статусе)
	std::size_t yofy_line_ = 0;	  // текущая строка прохода
	std::size_t yofy_lines_ = 0;  // сколько строк в проходе
	bool cancel_requested_ = false;
	bool quitting_ = false;	  // пришёл WM_QUIT: панель больше не ждёт ответа
};

} // namespace yo::gui
