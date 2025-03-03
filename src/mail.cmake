add_library(docwire_mail SHARED eml_parser.cpp pst_parser.cpp)

find_library(bfio bfio REQUIRED)
find_library(pff pff REQUIRED)
find_package(mailio CONFIG REQUIRED)
find_package(Boost REQUIRED COMPONENTS date_time) # mailio requires it
target_link_libraries(docwire_mail PRIVATE ${bfio} ${pff} mailio docwire_core)

install(TARGETS docwire_mail)
if(MSVC)
	install(FILES $<TARGET_PDB_FILE:docwire_mail> DESTINATION bin CONFIGURATIONS Debug)
endif()

include(GenerateExportHeader)
generate_export_header(docwire_mail EXPORT_FILE_NAME mail_export.h)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/mail_export.h DESTINATION include/docwire)
