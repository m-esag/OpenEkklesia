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

# Enable CTest so `ctest` runs the test suite defined below.
enable_testing()

message(STATUS "Found Qt Version: ${QT_VERSION_MAJOR}.${QT_VERSION_MINOR}")

# Define source files organized by module
set(SOURCES
    src/main.cpp

    # Database Module
    src/database/databasemanager.h
    src/database/databasemanager.cpp
    src/database/datasharing.h
    src/database/datasharing.cpp

    # Integrations Module
    src/integrations/planningcenterclient.h
    src/integrations/planningcenterclient.cpp

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
    src/ui/ministry/shepherdwidget.h
    src/ui/ministry/shepherdwidget.cpp
    src/ui/ministry/facilitieswidget.h
    src/ui/ministry/facilitieswidget.cpp
    src/ui/ministry/graphwidget.h
    src/ui/ministry/graphwidget.cpp
    src/ui/ministry/ministrymatch.h
    src/ui/ministry/ministrymatch.cpp
    src/ui/ministry/matchdialog.h
    src/ui/ministry/matchdialog.cpp

    # UI Administration & Security Submodule
    src/ui/admin/settingswidget.h
    src/ui/admin/settingswidget.cpp
    src/ui/admin/vaultwidget.h
    src/ui/admin/vaultwidget.cpp
    src/integrations/planningcenterdialog.h
    src/integrations/planningcenterdialog.cpp
)

add_executable(OpenEkklesia ${SOURCES})

target_include_directories(OpenEkklesia PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/common
    ${CMAKE_CURRENT_SOURCE_DIR}/src/database
    ${CMAKE_CURRENT_SOURCE_DIR}/src/integrations
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

# --- Test Suite (data layer integration tests) ---
add_executable(openekklesia_tests
    tests/tst_openekklesia.cpp
    src/database/databasemanager.cpp
    src/database/databasemanager.h
    src/database/datasharing.cpp
    src/database/datasharing.h
    src/ui/core/thememanager.cpp
    src/ui/core/thememanager.h
    src/ui/ministry/ministrymatch.cpp
    src/ui/ministry/ministrymatch.h
    src/ui/ministry/shepherdwidget.cpp
    src/ui/ministry/shepherdwidget.h
    src/ui/ministry/servicebuilderwidget.cpp
    src/ui/ministry/servicebuilderwidget.h
    src/ui/ministry/planningwidget.cpp
    src/ui/ministry/planningwidget.h
    src/ui/people/pastoralanalyticswidget.cpp
    src/ui/people/pastoralanalyticswidget.h
    src/ui/people/peoplewidget.cpp
    src/ui/people/peoplewidget.h
)

target_include_directories(openekklesia_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/common
    ${CMAKE_CURRENT_SOURCE_DIR}/src/database
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/core
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/people
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/ministry
)

target_link_libraries(openekklesia_tests PRIVATE
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::Sql
    Qt${QT_VERSION_MAJOR}::PrintSupport
)

add_test(NAME openekklesia_tests COMMAND openekklesia_tests)
