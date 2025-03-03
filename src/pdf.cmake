add_library(docwire_pdf SHARED pdf_parser.cpp)

find_package(podofo CONFIG REQUIRED)
find_package(ZLIB REQUIRED)
target_link_libraries(docwire_pdf PRIVATE podofo_shared ZLIB::ZLIB docwire_core)

install(TARGETS docwire_pdf)
if(MSVC)
	install(FILES $<TARGET_PDB_FILE:docwire_pdf> DESTINATION bin CONFIGURATIONS Debug)
endif()

include(GenerateExportHeader)
generate_export_header(docwire_pdf EXPORT_FILE_NAME pdf_export.h)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/pdf_export.h DESTINATION include/docwire)
