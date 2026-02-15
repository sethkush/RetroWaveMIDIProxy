add_executable(retrowave-midi-cli
    main.cpp
    daemon.h
    daemon.cpp
)

target_link_libraries(retrowave-midi-cli PRIVATE
    retrowave_core
    ADLMIDI_static
    rtmidi
)

target_include_directories(retrowave-midi-cli PUBLIC
    ${libADLMIDI_SOURCE_DIR}/src/
)


