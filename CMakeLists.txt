cmake_minimum_required(VERSION 3.24)
project(RemoteControl3 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK3 REQUIRED IMPORTED_TARGET gtk+-3.0)
pkg_check_modules(GTKSOURCEVIEW3 REQUIRED IMPORTED_TARGET gtksourceview-3.0)

add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../grclib" "${CMAKE_BINARY_DIR}/grclib")

add_library(rc3_options src/RCOptions.cpp)
target_include_directories(rc3_options PUBLIC src)

enable_testing()
add_executable(RCOptionsTests tests/RCOptionsTests.cpp)
target_link_libraries(RCOptionsTests PRIVATE rc3_options)
add_test(NAME RCOptionsTests COMMAND RCOptionsTests)

add_executable(RemoteControl3 src/main.cpp)
target_link_libraries(RemoteControl3 PRIVATE rc3_options grclib PkgConfig::GTK3 PkgConfig::GTKSOURCEVIEW3)
set_target_properties(RemoteControl3 PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin")

foreach(configuration DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    set_target_properties(RemoteControl3 PROPERTIES "RUNTIME_OUTPUT_DIRECTORY_${configuration}" "${CMAKE_SOURCE_DIR}/bin")
endforeach()

if(WIN32)
    add_custom_command(TARGET RemoteControl3 POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:grclib>" "$<TARGET_FILE_DIR:RemoteControl3>")
elseif(APPLE)
    set_target_properties(RemoteControl3 PROPERTIES BUILD_RPATH "@loader_path" INSTALL_RPATH "@loader_path")
    add_custom_command(TARGET RemoteControl3 POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:grclib>" "$<TARGET_FILE_DIR:RemoteControl3>")
else()
    set_target_properties(RemoteControl3 PROPERTIES BUILD_RPATH "$ORIGIN" INSTALL_RPATH "$ORIGIN")
    add_custom_command(TARGET RemoteControl3 POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:grclib>" "$<TARGET_FILE_DIR:RemoteControl3>")
endif()

foreach(assetDirectory images language-specs sounds)
    add_custom_command(TARGET RemoteControl3 POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/original/rc3_win_dark/${assetDirectory}" "$<TARGET_FILE_DIR:RemoteControl3>/${assetDirectory}")
endforeach()
