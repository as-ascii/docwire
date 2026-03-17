set(MODEL_NAME "qwen2-7b-instruct")
set(MODEL_QUANT "q4_k_m")

set(MODEL_FILE "${MODEL_NAME}-${MODEL_QUANT}.gguf")

vcpkg_download_distfile(
    MODEL_ARCHIVE
    URLS "https://huggingface.co/Qwen/Qwen2-7B-Instruct-GGUF/resolve/main/${MODEL_FILE}"
    FILENAME "${MODEL_FILE}"
    SHA512 39c1f9702856cf5faff13b672033c5c99246b5393550ed58ab9ba0eb2d5ce5d50cc2710b2d9f08d51ad6ce7f6b66826f5c916128fa06c3ac2f78865e167146b8
)

file(INSTALL
    ${MODEL_ARCHIVE}
    DESTINATION ${CURRENT_PACKAGES_DIR}/share/${PORT}
)

file(WRITE
    ${CURRENT_PACKAGES_DIR}/share/${PORT}/copyright
    "Model weights from HuggingFace repository Qwen/Qwen2-7B-Instruct-GGUF."
)
