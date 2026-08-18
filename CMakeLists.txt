cmake_minimum_required(VERSION 3.16)
project(OpenEkklesia LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Enable automatic compilation tools for Qt
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Export compile commands for IDE tooling (clangd, etc.)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Try to find Qt6, fallback to Qt5
find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core Widgets Sql Charts PrintSupport)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core Widgets Sql Charts PrintSupport)

message(STATUS "Found Qt Version: ${QT_VERSION_MAJOR}.${QT_VERSION_MINOR}")

# Define source files organized by module
set(SOURCES
    src/main.cpp

    # Database Module
    src/database/databasemanager.h
    src/database/databasemanager.cpp

    # UI Core Submodule
    src/ui/core/mainwindow.h
    src/ui/core/mainwindow.cpp
    src/ui/core/commandpalette.h
    src/ui/core/commandpalette.cpp
    src/ui/core/kioskwindow.h
    src/ui/core/kioskwindow.cpp
    src/ui/core/thememanager.h
    src/ui/core/thememanager.cpp

    # UI People & Pastoral Submodule
    src/ui/people/peoplewidget.h
    src/ui/people/peoplewidget.cpp
    src/ui/people/pastoralanalyticswidget.h
    src/ui/people/pastoralanalyticswidget.cpp

    # UI Finance & Asset Submodule
    src/ui/finance/contributionswidget.h
    src/ui/finance/contributionswidget.cpp
    src/ui/finance/inventorywidget.h
    src/ui/finance/inventorywidget.cpp
    src/ui/finance/transactiondialog.h
    src/ui/finance/transactiondialog.cpp

    # UI Ministry, Events & Facilities Submodule
    src/ui/ministry/servicebuilderwidget.h
    src/ui/ministry/servicebuilderwidget.cpp
    src/ui/ministry/planningwidget.h
    src/ui/ministry/planningwidget.cpp
    src/ui/ministry/facilitieswidget.h
    src/ui/ministry/facilitieswidget.cpp
    src/ui/ministry/graphwidget.h
    src/ui/ministry/graphwidget.cpp

    # UI Administration & Security Submodule
    src/ui/admin/settingswidget.h
    src/ui/admin/settingswidget.cpp
    src/ui/admin/vaultwidget.h
    src/ui/admin/vaultwidget.cpp
)

add_executable(OpenEkklesia ${SOURCES})

target_include_directories(OpenEkklesia PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/database
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/core
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/people
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/finance
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/ministry
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/admin
)

target_link_libraries(OpenEkklesia PRIVATE
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::Sql
    Qt${QT_VERSION_MAJOR}::Charts
    Qt${QT_VERSION_MAJOR}::PrintSupport
)
