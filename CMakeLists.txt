cmake_minimum_required(VERSION 3.16)
project(osu_render LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Qt6
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)

# Automatic moc/uic
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# Sources
set(SOURCES
    src/main.cpp
    src/Renderer.cpp
    src/MainWindow.cpp
)

# Headers
set(HEADERS
    src/Renderer.h
    src/MainWindow.h
)

# Executable
add_executable(osu_render ${SOURCES} ${HEADERS})

target_link_libraries(osu_render PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets)

# Install target (optional)
install(TARGETS osu_render DESTINATION bin)
install(FILES osu-render.desktop DESTINATION share/applications)
