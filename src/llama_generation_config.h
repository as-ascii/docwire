/*********************************************************************************************************************************************/
/*  DocWire SDK: Award-winning modern data processing in C++20. SourceForge
 * Community Choice & Microsoft support. AI-driven processing.      */
/*  Supports nearly 100 data formats, including email boxes and OCR. Boost
 * efficiency in text extraction, web data extraction, data mining,  */
/*  document analysis. Offline processing possible for security and
 * confidentiality                                                          */
/*                                                                                                                                           */
/*  Copyright (c) SILVERCODERS Ltd, http://silvercoders.com */
/*  Project homepage: https://github.com/docwire/docwire */
/*                                                                                                                                           */
/*  SPDX-License-Identifier: GPL-2.0-only OR LicenseRef-DocWire-Commercial */
/*********************************************************************************************************************************************/

#ifndef DOCWIRE_LOCAL_AI_LLAMA_GENERATION_CONFIG_H
#define DOCWIRE_LOCAL_AI_LLAMA_GENERATION_CONFIG_H
#include <string>

namespace docwire::local_ai
{
/*
 * @brief Handles configuration for llama model initialization and paramters
 */
struct llama_generation_config
{
    std::string model_path;

    int n_ctx = 4096;
    int n_threads = 4;
    int max_tokens = 512;

    float temperature = 0.2f;
    float min_p = 0.05f;

    bool verbose = false;
};

} // namespace docwire::local_ai

#endif
