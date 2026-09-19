# Official Lua 5.4.8, built identically by production and offline policy tests.
file(GLOB LUA_SOURCES CONFIGURE_DEPENDS "src/*.c")
list(REMOVE_ITEM LUA_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/lua.c" "${CMAKE_CURRENT_SOURCE_DIR}/src/luac.c")
add_library(playerbot_lua STATIC ${LUA_SOURCES})
set_target_properties(playerbot_lua PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(playerbot_lua PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(playerbot_lua PUBLIC m)
