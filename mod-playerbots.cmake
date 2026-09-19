if(BUILD_TESTING)
  set_property(GLOBAL APPEND PROPERTY ACORE_MODULE_TEST_SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/tests/AutoMaintenanceOnLevelupActionTest.cpp")
  set_property(GLOBAL APPEND PROPERTY ACORE_MODULE_TEST_INCLUDES
    "${CMAKE_CURRENT_LIST_DIR}/src")
endif()

# Position-only policy runtime, deliberately not the ALE scripting API.
ModuleNameToVariable("mod-playerbots" PLAYERBOT_POLICY_LINKAGE)
if(NOT "${${PLAYERBOT_POLICY_LINKAGE}}" STREQUAL "disabled")
  if(MOD_ALE_FOUND)
    message(FATAL_ERROR "Playerbot policy Lua and ALE co-installation is not supported")
  endif()
  add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/third_party/lua" "${CMAKE_BINARY_DIR}/playerbot-lua")
  if("${${PLAYERBOT_POLICY_LINKAGE}}" STREQUAL "dynamic")
    GetProjectNameOfModuleName("mod-playerbots" PLAYERBOT_POLICY_TARGET)
  else()
    set(PLAYERBOT_POLICY_TARGET modules)
  endif()
  target_link_libraries(${PLAYERBOT_POLICY_TARGET} PRIVATE playerbot_lua)
endif()
