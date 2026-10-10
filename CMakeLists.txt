cmake_minimum_required(VERSION 3.21)

project(denon-remote
    VERSION 0.3.0
    DESCRIPTION "Qt6 remote control for Denon/Marantz network receivers"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets Network)
# Provides the tray icon; Qt's own does not pass on the mouse wheel.
find_package(KF6StatusNotifierItem 6.0 REQUIRED)
qt_standard_project_setup()

# Everything that talks to the receiver, without any GUI, so the tests can
# use it too.
qt_add_library(denon-remote-core STATIC
    src/AvrConnection.cpp
    src/AvrConnection.h
    src/AvrState.cpp
    src/AvrState.h
    src/Discovery.cpp
    src/Discovery.h
    src/WebCatalogue.cpp
    src/WebCatalogue.h
)
target_include_directories(denon-remote-core PUBLIC src)
target_compile_definitions(denon-remote-core PRIVATE QT_NO_CAST_FROM_ASCII=0)
target_link_libraries(denon-remote-core PUBLIC Qt6::Core Qt6::Network)

qt_add_executable(denon-remote
    src/main.cpp
    src/ConnectDialog.cpp
    src/ConnectDialog.h
    src/ZonePanel.cpp
    src/ZonePanel.h
    src/SoundPanel.cpp
    src/SoundPanel.h
    src/MainWindow.cpp
    src/MainWindow.h
)

qt_add_resources(denon-remote "appicon"
    PREFIX "/"
    FILES data/icons/denon-remote.svg
)

target_compile_definitions(denon-remote PRIVATE
    DENON_REMOTE_VERSION="${PROJECT_VERSION}"
    QT_NO_CAST_FROM_ASCII=0
)

target_link_libraries(denon-remote PRIVATE
    denon-remote-core Qt6::Widgets Qt6::Network KF6::StatusNotifierItem)

option(BUILD_TESTING "Build the unit tests" ON)
if(BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()

include(GNUInstallDirs)

install(TARGETS denon-remote
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

install(FILES data/denon-remote.desktop
    DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/applications)

install(FILES data/icons/denon-remote.svg
    DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/scalable/apps)

install(FILES data/denon-remote.metainfo.xml
    DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/metainfo)

install(FILES data/denon-remote.1
    DESTINATION ${CMAKE_INSTALL_MANDIR}/man1)
