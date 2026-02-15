add_library(retrowave_core STATIC
    src/protocol.cpp
    src/opl3_hw.cpp
    src/opl3_registers.cpp
    src/opl3_state.cpp
    src/direct_mode.cpp
    src/voice_allocator.cpp
    src/midi_router.cpp
    src/serial_posix.cpp
)

target_include_directories(retrowave_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(retrowave_core PUBLIC cxx_std_17)
