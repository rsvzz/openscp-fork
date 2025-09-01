cmake_minimum_required(VERSION 3.22)
project(OpenSCP_hello LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Para que Qt procese automáticamente moc/uic/rcc
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# Si instalaste Qt con Homebrew:
# export CMAKE_PREFIX_PATH="/opt/homebrew/opt/qt@6"
# Si fue con el Qt Installer (ruta típica):
# export CMAKE_PREFIX_PATH="$HOME/Qt/6.9/macos"
find_package(Qt6 REQUIRED COMPONENTS Widgets)

add_executable(openscp_hello
  ui/main.cpp
  ui/MainWindow.hpp
  ui/MainWindow.cpp
)

target_link_libraries(openscp_hello PRIVATE Qt6::Widgets)

# (Opcional) genera compile_commands.json para mejor autocompletado
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)