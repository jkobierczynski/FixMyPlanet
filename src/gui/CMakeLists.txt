find_package(Qt6 6.2 REQUIRED COMPONENTS Widgets)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

qt_add_executable(fixmyplanet-gui WIN32 MACOSX_BUNDLE
  main.cpp
  imageview.cpp imageview.h
  mainwindow.cpp mainwindow.h
  resources/resources.qrc)
target_link_libraries(fixmyplanet-gui PRIVATE fixmyplanet_core Qt6::Widgets)
set_target_properties(fixmyplanet-gui PROPERTIES
  MACOSX_BUNDLE_BUNDLE_NAME "FixMyPlanet"
  MACOSX_BUNDLE_GUI_IDENTIFIER "io.github.jkobierczynski.fixmyplanet"
  MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
  MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}")
if(MSVC)
  target_compile_options(fixmyplanet-gui PRIVATE /W4 /utf-8)
else()
  target_compile_options(fixmyplanet-gui PRIVATE -Wall -Wextra)
endif()
install(TARGETS fixmyplanet-gui
  BUNDLE DESTINATION .
  RUNTIME DESTINATION bin)
if(UNIX AND NOT APPLE)
  install(FILES ${CMAKE_SOURCE_DIR}/packaging/fixmyplanet.desktop DESTINATION share/applications)
  install(FILES ${CMAKE_SOURCE_DIR}/packaging/fixmyplanet.png DESTINATION share/icons/hicolor/256x256/apps)
endif()
