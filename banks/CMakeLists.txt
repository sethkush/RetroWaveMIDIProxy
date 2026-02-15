# Embed any .wopl bank files in this directory as C byte arrays.
# Each file becomes a const uint8_t array in banks_embedded.h.

file(GLOB WOPL_FILES "${CMAKE_CURRENT_SOURCE_DIR}/*.wopl")

set(BANKS_HEADER "${CMAKE_CURRENT_BINARY_DIR}/banks_embedded.h")
set(BANKS_SOURCE "${CMAKE_CURRENT_BINARY_DIR}/banks_embedded.cpp")

# Generate header and source with embedded bank data
file(WRITE "${BANKS_HEADER}" "// Auto-generated embedded bank data\n#pragma once\n\n#include <cstdint>\n#include <cstddef>\n\nnamespace retrowave {\nnamespace banks {\n\nstruct EmbeddedBank {\n\tconst char *name;\n\tconst uint8_t *data;\n\tsize_t size;\n};\n\nextern const EmbeddedBank kBundledBanks[];\nextern const size_t kBundledBanksCount;\n\n} // namespace banks\n} // namespace retrowave\n")

file(WRITE "${BANKS_SOURCE}" "// Auto-generated embedded bank data\n#include \"banks_embedded.h\"\n\nnamespace retrowave {\nnamespace banks {\n\n")

set(BANK_NAMES "")
set(BANK_COUNT 0)

foreach(WOPL_FILE ${WOPL_FILES})
    get_filename_component(BANK_NAME "${WOPL_FILE}" NAME_WE)
    string(MAKE_C_IDENTIFIER "${BANK_NAME}" BANK_IDENT)

    file(READ "${WOPL_FILE}" BANK_HEX HEX)
    string(LENGTH "${BANK_HEX}" BANK_HEX_LEN)
    math(EXPR BANK_SIZE "${BANK_HEX_LEN} / 2")

    # Convert hex to comma-separated byte list
    string(REGEX REPLACE "([0-9a-fA-F][0-9a-fA-F])" "0x\\1," BANK_BYTES "${BANK_HEX}")

    file(APPEND "${BANKS_SOURCE}" "static const uint8_t bank_${BANK_IDENT}[] = {\n${BANK_BYTES}\n};\n\n")
    list(APPEND BANK_NAMES "{\"${BANK_NAME}\", bank_${BANK_IDENT}, ${BANK_SIZE}}")
    math(EXPR BANK_COUNT "${BANK_COUNT} + 1")
endforeach()

if(BANK_COUNT GREATER 0)
    string(REPLACE ";" ",\n\t" BANK_LIST "${BANK_NAMES}")
    file(APPEND "${BANKS_SOURCE}" "const EmbeddedBank kBundledBanks[] = {\n\t${BANK_LIST}\n};\n\n")
else()
    file(APPEND "${BANKS_SOURCE}" "const EmbeddedBank kBundledBanks[] = {{nullptr, nullptr, 0}};\n\n")
endif()

file(APPEND "${BANKS_SOURCE}" "const size_t kBundledBanksCount = ${BANK_COUNT};\n\n} // namespace banks\n} // namespace retrowave\n")

add_library(retrowave_banks STATIC "${BANKS_SOURCE}")
target_include_directories(retrowave_banks PUBLIC "${CMAKE_CURRENT_BINARY_DIR}")
target_compile_features(retrowave_banks PUBLIC cxx_std_17)

# Build-time tool to export all libADLMIDI embedded banks as .wopl files
set(EXPORTED_BANKS_DIR "${CMAKE_CURRENT_BINARY_DIR}/exported")
add_executable(export_banks "${CMAKE_CURRENT_SOURCE_DIR}/export_banks.cpp")
target_link_libraries(export_banks PRIVATE ADLMIDI_static wopl_parser)
target_include_directories(export_banks PRIVATE
    ${libADLMIDI_SOURCE_DIR}/include
)

add_custom_command(
    OUTPUT "${EXPORTED_BANKS_DIR}/.stamp"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${EXPORTED_BANKS_DIR}"
    COMMAND export_banks "${EXPORTED_BANKS_DIR}"
    COMMAND ${CMAKE_COMMAND} -E touch "${EXPORTED_BANKS_DIR}/.stamp"
    DEPENDS export_banks
    COMMENT "Exporting libADLMIDI embedded banks to .wopl files"
)
add_custom_target(exported_banks ALL DEPENDS "${EXPORTED_BANKS_DIR}/.stamp")
