cmake_minimum_required(VERSION 3.21)
project(GraphingEngine VERSION 0.1.0 LANGUAGES CXX)

include(CTest)
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
option(BUILD_SHARED_LIBS "Build a shared GraphingImpl library" ON)
option(GRAPHING_ENABLE_SANITIZERS "Enable address and undefined behavior sanitizers" OFF)
option(GRAPHING_BUILD_NATIVE_DRAWING "Build the CoreGraphics or Cairo drawing adapter" ${APPLE})
set(GRAPHING_NATIVE_CAIRO OFF)

add_library(graphing_core OBJECT
    src/Engine.cpp
    src/Expression.cpp
    src/Options.cpp
    src/Analyzer.cpp
    src/Renderer.cpp
    src/Scene.cpp
)
set_target_properties(graphing_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_compile_features(graphing_core PUBLIC cxx_std_17)
target_include_directories(graphing_core PUBLIC include)
target_compile_definitions(graphing_core PRIVATE GRAPHING_ENGINE_IMPL)
add_library(GraphingImpl $<TARGET_OBJECTS:graphing_core>)
add_library(Graphing::GraphingImpl ALIAS GraphingImpl)
target_compile_features(GraphingImpl PUBLIC cxx_std_17)
target_include_directories(GraphingImpl PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
if(BUILD_SHARED_LIBS)
    target_compile_definitions(graphing_core PRIVATE GRAPHING_ENGINE_SHARED)
    target_compile_definitions(GraphingImpl PUBLIC GRAPHING_ENGINE_SHARED)
endif()
set_target_properties(GraphingImpl PROPERTIES VERSION ${PROJECT_VERSION} SOVERSION 0)
if(MSVC)
    target_compile_options(graphing_core PRIVATE /W4 /WX /w44242 /permissive- /utf-8)
    target_compile_definitions(graphing_core PRIVATE NOMINMAX)
else()
    target_compile_options(graphing_core PRIVATE -Wall -Wextra -Wpedantic)
endif()
if(WIN32)
    target_link_libraries(GraphingImpl PRIVATE d2d1)
endif()
if(GRAPHING_ENABLE_SANITIZERS)
    if(MSVC)
        message(FATAL_ERROR "Use Clang or GCC for the address/undefined sanitizer configuration.")
    endif()
    target_compile_options(graphing_core PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_compile_options(GraphingImpl PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(GraphingImpl PUBLIC -fsanitize=address,undefined)
endif()

add_executable(graphing-cli src/Cli.cpp)
target_link_libraries(graphing-cli PRIVATE GraphingImpl)
add_executable(graphing-compatibility-probe tools/CompatibilityProbe.cpp)
target_link_libraries(graphing-compatibility-probe PRIVATE GraphingImpl)
if(WIN32)
    target_link_libraries(graphing-compatibility-probe PRIVATE ole32)
endif()

if(GRAPHING_BUILD_NATIVE_DRAWING AND NOT WIN32)
    add_library(GraphingNative STATIC src/NativeDrawing.cpp)
    add_library(Graphing::GraphingNative ALIAS GraphingNative)
    target_link_libraries(GraphingNative PUBLIC GraphingImpl)
    if(APPLE)
        target_link_libraries(GraphingNative PUBLIC "-framework CoreGraphics" "-framework CoreText" "-framework CoreFoundation")
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(Cairo REQUIRED IMPORTED_TARGET cairo>=1.16)
        set(GRAPHING_NATIVE_CAIRO ON)
        target_compile_definitions(GraphingNative PUBLIC GRAPHING_NATIVE_CAIRO)
        target_link_libraries(GraphingNative PUBLIC PkgConfig::Cairo)
    else()
        message(FATAL_ERROR "The native drawing adapter supports CoreGraphics and Cairo.")
    endif()
    if(BUILD_TESTING)
        add_executable(NativeDrawingTests tests/NativeDrawingTests.cpp)
        target_include_directories(NativeDrawingTests PRIVATE src)
        target_link_libraries(NativeDrawingTests PRIVATE GraphingNative)
        if(APPLE)
            target_link_libraries(NativeDrawingTests PRIVATE "-framework ImageIO")
        endif()
        add_test(NAME NativeDrawing COMMAND NativeDrawingTests)
    endif()
    install(TARGETS GraphingNative EXPORT GraphingEngineTargets ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
endif()

if(BUILD_TESTING)
    add_test(NAME CompatibilityProbe COMMAND ${CMAKE_COMMAND}
        "-DPROBE_EXECUTABLE=$<TARGET_FILE:graphing-compatibility-probe>"
        "-DCASES_FILE=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/graphing-cases.tsv"
        "-DTEST_OUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/compatibility-tests"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/CompatibilityProbeTest.cmake")
    set_tests_properties(CompatibilityProbe PROPERTIES TIMEOUT 30)
    foreach(suite IN ITEMS Expression Renderer Analyzer Contract)
        add_executable(${suite}Tests tests/${suite}Tests.cpp)
        if(suite STREQUAL "Contract")
            target_link_libraries(${suite}Tests PRIVATE GraphingImpl)
            if(WIN32)
                target_link_libraries(${suite}Tests PRIVATE d2d1 windowscodecs ole32)
            endif()
        else()
            target_sources(${suite}Tests PRIVATE $<TARGET_OBJECTS:graphing_core>)
            target_include_directories(${suite}Tests PRIVATE src include)
            target_compile_features(${suite}Tests PRIVATE cxx_std_17)
            if(WIN32)
                target_link_libraries(${suite}Tests PRIVATE d2d1)
            endif()
            if(GRAPHING_ENABLE_SANITIZERS)
                target_compile_options(${suite}Tests PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
                target_link_options(${suite}Tests PRIVATE -fsanitize=address,undefined)
            endif()
        endif()
        if(MSVC)
            target_compile_options(${suite}Tests PRIVATE /utf-8)
        endif()
        add_test(NAME ${suite} COMMAND ${suite}Tests)
        set_tests_properties(${suite} PROPERTIES TIMEOUT 30)
    endforeach()
endif()

install(TARGETS GraphingImpl EXPORT GraphingEngineTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT GraphingEngineTargets NAMESPACE Graphing:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/GraphingEngine)
configure_package_config_file(cmake/GraphingEngineConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/GraphingEngineConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/GraphingEngine)
write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/GraphingEngineConfigVersion.cmake
    COMPATIBILITY SameMajorVersion)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/GraphingEngineConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/GraphingEngineConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/GraphingEngine)
install(FILES LICENSE docs/Microsoft-LICENSE DESTINATION ${CMAKE_INSTALL_DATADIR}/GraphingEngine)
