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

#include "yo/find.hpp"
#include "yo/mru.hpp"
#include "yo/optionspanel.hpp"

#include <filesystem>
#include <memory>
#include <utility>
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

	// --- поиск и замена (полоса внизу окна, как у RichEdit-диалогов) ---
	void find_toggle(bool replace_row = false);
	void find_close();
	void find_next();			   // F3
	void find_prev();			   // Shift+F3
	void find_run(bool forward);   // находит и выделяет
	void find_replace();		   // «Заменить» (одно совпадение)
	void find_replace_all();	   // «Заменить всё» одной группой undo
	void find_add_text(const std::string& utf8);
	void find_backspace();
	void find_toggle_case();
	void find_toggle_word();
	void find_sync_view();   // геометрия полосы + подсветка видимых совпадений
	void find_collect_marks();
	void find_count_hits();
	[[nodiscard]] bool find_click(float x, float y);
	[[nodiscard]] bool find_mouse_move(float x, float y);
	[[nodiscard]] bool find_char(wchar_t ch);
	[[nodiscard]] bool find_key(WPARAM vk, bool ctrl, bool shift);

	// --- окно настроек (аналог OptionsForm, но наложением в тёмной теме) ---
	void options_open_dialog();
	void options_close();
	void options_apply(bool remember);
	void options_sync_view();
	void options_activate(int row);
	void options_font_step(int delta);
	void options_move(int delta);
	[[nodiscard]] bool options_click(float x, float y);
	[[nodiscard]] bool options_mouse_move(float x, float y);
	[[nodiscard]] bool options_key(WPARAM vk, bool ctrl, bool shift);
	// Выбор файла словаря (меню «Файл» и строки окна настроек).
	bool pick_dictionary(bool regex_dict);
	// Перечитать словари с диска (OpenDic1 / OpenRegExprDic1 оригинала).
	void reload_dictionaries();
	// Сброс пользовательских «Всегда Е/Ё» (ClearUserSel оригинала).
	void clear_user_selection();
	[[nodiscard]] std::string dictionary_display(bool regex_dict) const;

	// --- прочее из меню оригинала ---
	// --- последние файлы (MRU, как Openfile0 оригинала) ---
	// Добавить/запомнить файл в списке и перестроить меню.
	void mru_touch(const std::filesystem::path& path, std::size_t position);
	// Запомнить позицию курсора текущего файла (при закрытии/переключении).
	void mru_remember_current();
	bool open_mru(std::size_t index);
	void clear_mru();
	[[nodiscard]] std::string mru_flags() const; // текущая кодировка буквами ini
	[[nodiscard]] static bool mru_decode_flags(std::string_view flags, Encoding& enc, bool& bom);

	void toggle_line_numbers();
	void show_about();
	void show_file_props();
	// Переход по сделанным заменам (Ctrl+W / Ctrl+Q, как FindyyFwd/FindyyBack).
	void yofy_goto(bool forward);
	void clear_yofy_marks();
	// Подсветка ёфикационных замен в диапазоне (как Mark оригинала).
	void mark_yofy_changes(std::size_t from, std::size_t to);

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
	Stats last_stats_{};		 // счётчики последнего прохода (§4.7)
	double last_yofy_ms_ = 0.0;	 // его длительность — для T/S в статусе
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
	bool find_replace_row_ = false; // вторая строка полосы (Ctrl+H)
	std::string find_query_;
	std::string find_replace_text_;
	int find_focus_ = 0;			// 0 — образец, 1 — замена
	yo::FindOptions find_opts_{};
	bool find_regex_ = false;		// Ctrl+Shift+R: образец — регулярное выражение
	std::size_t find_total_ = 0;	// сколько совпадений в документе
	std::size_t find_index_ = 0;	// номер текущего (1-based), 0 — нет
	std::wstring find_error_;		// ошибка разбора РВ
	FindBarView find_view_{};		// готовая раскладка полосы (клики и рисование)
	std::vector<std::pair<std::size_t, std::size_t>> find_marks_;
	std::vector<std::size_t> find_hits_; // все совпадения (для «N из M»)
	// Замены текущего/последнего прохода: по ним ходят Ctrl+W / Ctrl+Q.
	std::vector<std::pair<std::size_t, std::size_t>> yofy_marks_;
	std::size_t yofy_mark_index_ = 0;
	bool show_numbers_ = false;

	// Окно настроек: значения правятся в options_state_, применяются
	// кнопкой «Применить»/«Запомнить».
	bool options_open_ = false;
	yo::OptionsState options_state_{};
	yo::OptionsLayout options_layout_{};
	OptionsView options_view_{};
	int options_row_ = 0; // выбранная строка
	int options_hot_ = -1; // кнопка под мышью
	char32_t pending_high_ = 0;
	int wheel_accum_ = 0; // остаток дельты колеса (тачпады шлют меньше 120)
	// --- настройки (yo.ini рядом с exe, как у оригинала) ---
	// Ключи названы как в оригинале ([Yo] AlwaysAsk, Mark, FontSize, ...),
	// поэтому подходит и yo.ini от старого ёфикатора.
	bool mark_ = true;
	bool propose_last_ = true;
	bool regexs_ = true;
	bool to_confirm_abbr_ = true;
	std::wstring dict_setting_;	 // [Yo] Dictionary (пусто — искать рядом с exe)
	std::wstring regex_setting_; // [Yo] RegExprDictionary
	MruList mru_;
	std::size_t last_file_index_ = 0; // [Files] LastFileN
	bool recall_last_file_ = true;	  // [Yo] RecallLastFile
	std::filesystem::path ini_path_;
	RECT saved_rect_{};
	bool has_saved_rect_ = false;
	bool saved_maximized_ = false;
	void load_settings();
	void save_settings();
	[[nodiscard]] std::filesystem::path settings_path() const;

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
