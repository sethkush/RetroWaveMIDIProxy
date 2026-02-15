find_package(Qt5 COMPONENTS Widgets REQUIRED)
find_package(Qt5 COMPONENTS SerialPort REQUIRED)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

add_executable(RetroWaveMIDIProxyGUI
    main.cpp
    mainwindow.cpp
    mainwindow.h
    mainwindow.ui
    serial_qt.h
    serial_qt.cpp
)

target_link_libraries(RetroWaveMIDIProxyGUI PRIVATE
    retrowave_core
    ADLMIDI_static
    rtmidi
    Qt5::Widgets
    Qt5::SerialPort
)

target_include_directories(RetroWaveMIDIProxyGUI PUBLIC
    ${libADLMIDI_SOURCE_DIR}/src/
)
