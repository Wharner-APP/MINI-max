# ---- mmcore: everything that does not need widgets (also used by tests) ----------------
add_library(mmcore STATIC
  src/core/config.cpp src/core/prefs.cpp src/core/api.cpp src/core/connection_monitor.cpp
  src/core/device_info.cpp src/core/install_markers.cpp src/core/crypto.cpp src/core/payments.cpp
  src/core/chats_model.cpp src/core/messages_model.cpp src/core/session_store.cpp src/core/catalog.cpp
  src/core/demo_data.cpp src/core/engine.cpp)
target_include_directories(mmcore PUBLIC src)
target_link_libraries(mmcore PUBLIC mmcommon tomlplusplus::tomlplusplus)

if(MINIMAX_BUILD_TESTS)
  add_executable(test_core tests/test_core.cpp)
  target_link_libraries(test_core PRIVATE mmcore)
  add_test(NAME core COMMAND test_core ${PROJECT_SOURCE_DIR}/configs/client.toml)
endif()

# ---- optional logo.ico (put it in the repository root) --------------------------------------
set(MM_LOGO "${PROJECT_SOURCE_DIR}/logo.ico")
set(MM_EXTRA_SOURCES "")
if(EXISTS "${MM_LOGO}")
  configure_file(src/res/logo.qrc.in ${CMAKE_BINARY_DIR}/generated/logo.qrc @ONLY)
  list(APPEND MM_EXTRA_SOURCES ${CMAKE_BINARY_DIR}/generated/logo.qrc)
  message(STATUS "logo.ico found - used as the application icon")
else()
  message(WARNING "logo.ico not found in the repository root - building without a custom icon")
endif()

if(WIN32)
  set(MM_RC_ICON "")
  if(EXISTS "${MM_LOGO}")
    set(MM_RC_ICON "IDI_ICON1 ICON \"${MM_LOGO}\"")
  endif()
  set(MM_RC_DESC "${MM_APP_NAME}")
  set(MM_RC_FILE "minimax.exe")
  configure_file(win/app.rc.in ${CMAKE_BINARY_DIR}/generated/client.rc @ONLY)
  list(APPEND MM_EXTRA_SOURCES ${CMAKE_BINARY_DIR}/generated/client.rc)
endif()

add_executable(minimax
  src/main.cpp
  src/ui/theme.cpp src/ui/icons.cpp src/ui/widgets.cpp src/ui/popup.cpp src/ui/pages.cpp src/ui/delegates.cpp
  src/ui/auth_view.cpp src/ui/chat_view.cpp src/ui/main_view.cpp src/ui/main_window.cpp
  src/res/res.qrc ${MM_EXTRA_SOURCES})
target_link_libraries(minimax PRIVATE mmcore ${QTV}::Widgets ${QTV}::Svg ${QTV}::Multimedia ${QTV}::MultimediaWidgets)
if(WIN32)
  set_target_properties(minimax PROPERTIES WIN32_EXECUTABLE ON)
  if(TARGET Qt5::WinMain)
    target_link_libraries(minimax PRIVATE Qt5::WinMain)
  endif()
endif()

if(APPLE)
  set_target_properties(minimax PROPERTIES MACOSX_BUNDLE ON
    MACOSX_BUNDLE_BUNDLE_NAME "${MM_APP_NAME}" MACOSX_BUNDLE_GUI_IDENTIFIER "app.wharner.minimax"
    MACOSX_BUNDLE_BUNDLE_VERSION "${MM_VERSION_STR}" MACOSX_BUNDLE_SHORT_VERSION_STRING "${MM_VERSION_STR}"
    MACOSX_BUNDLE_COPYRIGHT "${MM_COPYRIGHT}")
elseif(UNIX)
  set_target_properties(minimax PROPERTIES BUILD_RPATH "$ORIGIN/lib" INSTALL_RPATH "$ORIGIN/lib")
endif()

# configs/ next to the executable; connect.ip is generated here (never overwritten if it exists)
add_custom_command(TARGET minimax POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:minimax>/configs
  COMMAND ${CMAKE_COMMAND} -E copy_if_different ${PROJECT_SOURCE_DIR}/configs/client.toml $<TARGET_FILE_DIR:minimax>/configs/client.toml
  COMMAND ${CMAKE_COMMAND} -DOUT=$<TARGET_FILE_DIR:minimax>/configs/connect.ip -P ${PROJECT_SOURCE_DIR}/cmake/make_connect_ip.cmake
  VERBATIM)
