add_executable(yo-cli main.cpp)
target_link_libraries(yo-cli PRIVATE yo::core)
set_target_properties(yo-cli PROPERTIES CXX_STANDARD 23 CXX_STANDARD_REQUIRED ON)

# Отладочная утилита: проверка правил РВ на контексте (не ставится).
add_executable(yo-regex-probe ../../tools/regex_probe.cpp)
target_link_libraries(yo-regex-probe PRIVATE yo::core)
