add_library(docwire_local_ai INTERFACE)
target_link_libraries(docwire_local_ai INTERFACE docwire_ai)

if(DOCWIRE_LOCAL_CT2)

    message(STATUS "DOCWIRE_LOCAL_CT2 enabled: Building CT2 backend.")

    add_library(docwire_ai_ct2 SHARED local_ai_embed.cpp local_ai_summarize.cpp local_ai_translate.cpp model_chain_element.cpp ct2_runner.cpp tokenizer.cpp)

    target_compile_definitions(docwire_ai_ct2 PUBLIC DOCWIRE_LOCAL_CT2)

    find_package(Boost REQUIRED COMPONENTS filesystem system json)
    find_package(ctranslate2 CONFIG REQUIRED)
    find_library(sentencepiece_LIBRARIES sentencepiece REQUIRED)

    if(MSVC)
        find_package(absl CONFIG REQUIRED)
        list(APPEND sentencepiece_LIBRARIES
            absl::strings
            absl::flags
            absl::flags_parse
            absl::log
            absl::check)

        find_package(protobuf CONFIG REQUIRED)
        list(APPEND sentencepiece_LIBRARIES protobuf::libprotobuf-lite)
    endif()

    target_link_libraries(docwire_ai_ct2 PRIVATE docwire_core docwire_ai Boost::filesystem Boost::json CTranslate2::ctranslate2 ${sentencepiece_LIBRARIES})

    docwire_find_resource(FLAN_T5_FULL_PATH REL_PATH "flan-t5-large-ct2-int8" REQUIRED)
    docwire_target_resources(docwire_ai_ct2 "flan-t5-large-ct2-int8" SOURCE "${FLAN_T5_FULL_PATH}")

    docwire_find_resource(E5_MODEL_FULL_PATH REL_PATH "multilingual-e5-small-ct2-int8" REQUIRED)
    docwire_target_resources(docwire_ai_ct2 "multilingual-e5-small-ct2-int8" SOURCE "${E5_MODEL_FULL_PATH}")

    install(TARGETS docwire_ai_ct2 EXPORT docwire_targets)

    if(MSVC)
        install(FILES $<TARGET_PDB_FILE:docwire_ai_ct2> DESTINATION bin CONFIGURATIONS Debug)
    endif()

    target_link_libraries(docwire_local_ai INTERFACE docwire_ai_ct2)

endif()

if(DOCWIRE_LLAMA)

    message(STATUS "DOCWIRE_LLAMA enabled: building llama backend")

    add_library(docwire_ai_llama SHARED llama_runner.cpp)

    find_package(llama CONFIG REQUIRED)

    target_link_libraries(docwire_ai_llama PRIVATE docwire_core docwire_ai llama)

    target_compile_definitions(docwire_ai_llama PUBLIC DOCWIRE_LLAMA)

    install(TARGETS docwire_ai_llama EXPORT docwire_targets)

    target_link_libraries(docwire_local_ai INTERFACE docwire_ai_llama)

endif()

if(NOT DOCWIRE_LOCAL_CT2 AND NOT DOCWIRE_LLAMA)
    message(STATUS "No Local AI backends enabled.")
endif()
