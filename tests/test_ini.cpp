// INI-настройки (yo.ini): разбор, значения по умолчанию, запись.

#include "yo/ini.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>

using namespace yo;

TEST_CASE("ini: разбор секций, комментариев и значений", "[ini]")
{
	Ini ini;
	ini.parse("; комментарий\n"
			  "# тоже комментарий\n"
			  "[gui]\n"
			  "font_size = 17\n"
			  "window=100,50,1200,800\n"
			  "\n"
			  "[yofy]\n"
			  "always_ask=1\n"
			  "silent=0\n"
			  "broken line without equals\n");
	CHECK(ini.get_int("gui", "font_size", 15) == 17);
	CHECK(ini.get("gui", "window") == "100,50,1200,800");
	CHECK(ini.get_bool("yofy", "always_ask", false));
	CHECK_FALSE(ini.get_bool("yofy", "silent", true));
	CHECK(ini.get_int("gui", "нет_такого", 42) == 42);
	CHECK(ini.get("gui", "нет_такого", "по умолчанию") == "по умолчанию");
	CHECK_FALSE(ini.has("gui", "нет_такого"));
}

TEST_CASE("ini: мусор в значениях не ломает запуск", "[ini]")
{
	Ini ini;
	ini.parse("[gui]\nfont_size=12px\nratio=abc\nflag=может быть\n");
	CHECK(ini.get_int("gui", "font_size", 15) == 15); // «12px» не число
	CHECK(ini.get_float("gui", "ratio", 1.5F) == 1.5F);
	CHECK(ini.get_bool("gui", "flag", true));
}

TEST_CASE("ini: запись и повторный разбор", "[ini]")
{
	Ini ini;
	ini.set_int("gui", "font_size", 18);
	ini.set_float("gui", "zoom", 1.5F);
	ini.set_bool("yofy", "always_ask", true);
	ini.set("gui", "window", "1,2,3,4");
	const std::string text = ini.dump();
	CHECK(text.find("[gui]") != std::string::npos);
	CHECK(text.find("font_size=18") != std::string::npos);
	CHECK(text.find("zoom=1.5") != std::string::npos);
	CHECK(text.find("always_ask=1") != std::string::npos);

	Ini again;
	again.parse(text);
	CHECK(again.get_int("gui", "font_size", 0) == 18);
	CHECK(again.get_float("gui", "zoom", 0.0F) == 1.5F);
	CHECK(again.get_bool("yofy", "always_ask", false));
	CHECK(again.get("gui", "window") == "1,2,3,4");
}

TEST_CASE("ini: повторная запись ключа не дублирует его", "[ini]")
{
	Ini ini;
	ini.set("gui", "font_size", "15");
	ini.set("gui", "Font_Size", "20"); // регистр ключа не важен
	CHECK(ini.get_int("gui", "font_size", 0) == 20);
	const std::string text = ini.dump();
	// Ключ пишется один раз (с исходным написанием), значение — последнее.
	const std::size_t first = text.find("font_size");
	REQUIRE(first != std::string::npos);
	CHECK(text.find("font_size", first + 1) == std::string::npos);
	CHECK(text.find("=20") != std::string::npos);
}
