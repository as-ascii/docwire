add_executable(docwire docwire.cpp)

find_package(Boost REQUIRED COMPONENTS program_options)
target_link_libraries(docwire PRIVATE docwire_core docwire_office_formats docwire_mail docwire_ocr docwire_archives
    docwire_openai docwire_ai docwire_content_type docwire_http Boost::program_options)

if(DOCWIRE_LOCAL_CT2)
    target_link_libraries(docwire PRIVATE docwire_local_ai)
    target_compile_definitions(docwire PRIVATE DOCWIRE_LOCAL_CT2)
endif()
if(DOCWIRE_LLAMA)
    target_compile_definitions(docwire PRIVATE DOCWIRE_LLAMA)
	if(TARGET docwire_local_ai)
	        target_compile_definitions(docwire_local_ai PRIVATE DOCWIRE_LLAMA)
	    endif()
endif()

install(TARGETS docwire DESTINATION bin)
