cmake_minimum_required(VERSION 3.16)
project(OpenEkklesia VERSION 0.8.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Baseline warnings so ~30k LOC cannot fail silently. Per-file -Wconversion
# audits remain opt-in (see NOTES.md polish backlog).
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra)
endif()

# Enable automatic compilation tools for Qt
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Export compile commands for IDE tooling (clangd, etc.)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Qt6 required (Qt5 fallback removed: code uses Qt6-era Charts/PrintSupport).
find_package(QT NAMES Qt6 REQUIRED COMPONENTS Core Widgets Sql Charts PrintSupport)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core Widgets Sql Charts PrintSupport)

# Enable CTest so `ctest` runs the test suite defined below.
enable_testing()

message(STATUS "Found Qt Version: ${QT_VERSION_MAJOR}.${QT_VERSION_MINOR}")

# Define source files organized by module
set(SOURCES
    src/main.cpp

    # Common utilities (shared, product-neutral)
    src/common/appdateformatter.h
    src/common/appdateformatter.cpp
    src/common/churchprofile.h
    src/common/churchprofile.cpp
    src/common/tableutil.h
    src/common/givingstatementgenerator.h
    src/common/givingstatementgenerator.cpp

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
    src/ui/core/touchhelper.h
    src/ui/core/touchhelper.cpp
    src/ui/core/iconmanager.h
    src/ui/core/iconmanager.cpp
    src/ui/core/usermanualdialog.h
    src/ui/core/usermanualdialog.cpp

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
    src/ui/ministry/ministryarchetypes.h
    src/ui/ministry/synergyengine.h
    src/ui/ministry/synergyengine.cpp

    # UI Administration & Security Submodule
    src/ui/admin/settingswidget.h
    src/ui/admin/settingswidget.cpp
    src/ui/admin/vaultwidget.h
    src/ui/admin/vaultwidget.cpp
    src/integrations/planningcenterdialog.h
    src/integrations/planningcenterdialog.cpp
)

add_executable(OpenEkklesia ${SOURCES})

if(APPLE)
    set_target_properties(OpenEkklesia PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "OpenEkklesia"
        MACOSX_BUNDLE_GUI_IDENTIFIER "org.openekklesia.OpenEkklesia"
        MACOSX_BUNDLE_BUNDLE_VERSION "0.8.0"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "0.8.0"
    )
endif()

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
    src/common/appdateformatter.cpp
    src/common/appdateformatter.h
    src/common/churchprofile.cpp
    src/common/churchprofile.h
    src/common/tableutil.h
    src/common/givingstatementgenerator.cpp
    src/common/givingstatementgenerator.h
    src/database/databasemanager.cpp
    src/database/databasemanager.h
    src/database/datasharing.cpp
    src/database/datasharing.h
    src/ui/core/thememanager.cpp
    src/ui/core/thememanager.h
    src/ui/core/iconmanager.cpp
    src/ui/core/iconmanager.h
    src/ui/core/usermanualdialog.cpp
    src/ui/core/usermanualdialog.h
    src/ui/ministry/ministrymatch.cpp
    src/ui/ministry/ministrymatch.h
    src/ui/ministry/shepherdwidget.cpp
    src/ui/ministry/shepherdwidget.h
    src/ui/ministry/servicebuilderwidget.cpp
    src/ui/ministry/servicebuilderwidget.h
    src/ui/ministry/planningwidget.cpp
    src/ui/ministry/planningwidget.h
    src/ui/ministry/graphwidget.cpp
    src/ui/ministry/graphwidget.h
    src/ui/ministry/matchdialog.cpp
    src/ui/ministry/matchdialog.h
    src/ui/ministry/ministryarchetypes.h
    src/ui/ministry/synergyengine.h
    src/ui/ministry/synergyengine.cpp
    src/ui/people/pastoralanalyticswidget.cpp
    src/ui/people/pastoralanalyticswidget.h
    src/ui/ministry/facilitieswidget.cpp
    src/ui/ministry/facilitieswidget.h
    src/ui/people/peoplewidget.cpp
    src/ui/people/peoplewidget.h
    src/ui/admin/settingswidget.cpp
    src/ui/admin/settingswidget.h
    src/ui/admin/vaultwidget.cpp
    src/ui/admin/vaultwidget.h
    src/integrations/planningcenterclient.cpp
    src/integrations/planningcenterclient.h
    src/integrations/planningcenterdialog.cpp
    src/integrations/planningcenterdialog.h
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
    Qt${QT_VERSION_MAJOR}::Charts
    Qt${QT_VERSION_MAJOR}::PrintSupport
)

add_test(NAME openekklesia_tests COMMAND openekklesia_tests)

# --- High-Resolution Performance Benchmark Suite ---
add_executable(openekklesia_benchmarks
    tests/benchmarks.cpp
    src/database/databasemanager.cpp
    src/database/databasemanager.h
    src/database/datasharing.cpp
    src/database/datasharing.h
    src/ui/core/thememanager.cpp
    src/ui/core/thememanager.h
    src/ui/ministry/ministrymatch.cpp
    src/ui/ministry/ministrymatch.h
)

target_include_directories(openekklesia_benchmarks PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/common
    ${CMAKE_CURRENT_SOURCE_DIR}/src/database
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/core
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/people
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/ministry
)

target_link_libraries(openekklesia_benchmarks PRIVATE
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::Sql
    Qt${QT_VERSION_MAJOR}::PrintSupport
)

# Temporary crash-hunt driver (ASan diagnosis) — remove after investigation
