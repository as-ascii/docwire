add_library(docwire_rtf SHARED rtf_parser.cpp)

find_path(wv2_incdir wv2/ustring.h) # because of misc.h, TODO: misc.h should be splitted
target_include_directories(docwire_rtf PRIVATE ${wv2_incdir})
find_library(wv2 wv2 HINTS ${wv2_incdir}/../lib/static REQUIRED)
target_link_libraries(docwire_rtf PRIVATE ${wv2} docwire_core)

install(TARGETS docwire_rtf)
if(MSVC)
	install(FILES $<TARGET_PDB_FILE:docwire_rtf> DESTINATION bin CONFIGURATIONS Debug)
endif()

include(GenerateExportHeader)
generate_export_header(docwire_rtf EXPORT_FILE_NAME rtf_export.h)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/rtf_export.h DESTINATION include/docwire)
