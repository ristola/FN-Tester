cmake_minimum_required (VERSION 3.13)

project(FnSymbolAnalyzer)

add_definitions( -DLOGIC2 )

set(CMAKE_OSX_DEPLOYMENT_TARGET "10.14" CACHE STRING "Minimum supported MacOS version" FORCE)

# enable generation of compile_commands.json, helpful for IDEs to locate include files.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# custom CMake Modules are located in the cmake directory.
set(CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake)

include(ExternalAnalyzerSDK)

set(SOURCES
src/FnSymbolAnalyzer.cpp
src/FnSymbolAnalyzer.h
src/FnSymbolAnalyzerResults.cpp
src/FnSymbolAnalyzerResults.h
src/FnSymbolAnalyzerSettings.cpp
src/FnSymbolAnalyzerSettings.h
src/FnSymbolSimulationDataGenerator.cpp
src/FnSymbolSimulationDataGenerator.h
src/Pcb085Profile.cpp
src/Pcb085Profile.h
)

add_analyzer_plugin(${PROJECT_NAME} SOURCES ${SOURCES})
