find_package(Qt5 COMPONENTS Widgets REQUIRED)
find_package(Qt5 COMPONENTS SerialPort REQUIRED)

set(CMAKE_AUTOMOC ON)

add_executable(RetroWaveOPL3Panel
    main.cpp
    panelwindow.h
    panelwindow.cpp
    fm_diagram_widget.h
    fm_diagram_widget.cpp
    serial_qt.h
    serial_qt.cpp
)

target_link_libraries(RetroWaveOPL3Panel PRIVATE
    retrowave_core
    rtmidi
    wopl_parser
    Qt5::Widgets
    Qt5::SerialPort
)

# Copy .wopl bank files to build output directory via cmake -P script
# (handles filenames with special characters like single quotes)
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/copy_banks.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/copy_banks.cmake"
    @ONLY
)
# Ensure exported banks are generated before we try to copy them
add_dependencies(RetroWaveOPL3Panel exported_banks)
add_custom_command(TARGET RetroWaveOPL3Panel POST_BUILD
    COMMAND ${CMAKE_COMMAND} -P "${CMAKE_CURRENT_BINARY_DIR}/copy_banks.cmake"
    COMMENT "Copying bank files to output directory"
)
