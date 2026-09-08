add_library(docwire_openrouter SHARED
    openrouter_chat.cpp)

find_package(Boost REQUIRED COMPONENTS json)
target_link_libraries(docwire_openrouter PRIVATE docwire_base64 docwire_core docwire_ai docwire_http Boost::json)

install(TARGETS docwire_openrouter EXPORT docwire_targets)
if(MSVC)
	install(FILES $<TARGET_PDB_FILE:docwire_openrouter> DESTINATION bin CONFIGURATIONS Debug)
endif()

include(GenerateExportHeader)
generate_export_header(docwire_openrouter EXPORT_FILE_NAME openrouter_export.h)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/openrouter_export.h DESTINATION include/docwire)
