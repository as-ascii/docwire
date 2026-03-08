/*********************************************************************************************************************************************/
/*  DocWire SDK: Award-winning modern data processing in C++20. SourceForge Community Choice &
 * Microsoft support. AI-driven processing.      */
/*  Supports nearly 100 data formats, including email boxes and OCR. Boost efficiency in text
 * extraction, web data extraction, data mining,  */
/*  document analysis. Offline processing possible for security and confidentiality */
/*                                                                                                                                           */
/*  Copyright (c) SILVERCODERS Ltd, http://silvercoders.com */
/*  Project homepage: https://github.com/docwire/docwire */
/*                                                                                                                                           */
/*  SPDX-License-Identifier: GPL-2.0-only OR LicenseRef-DocWire-Commercial */
/*********************************************************************************************************************************************/
#include "local_ai_summarize.h"
#include "model_chain_element.h"

namespace docwire::local_ai
{

constexpr const char* DEFAULT_SUMMARY_PROMPT = "Your task is to summarize the following text:\n\n";

local_summarize::local_summarize() : model_chain_element(DEFAULT_SUMMARY_PROMPT) {}

local_summarize::local_summarize(std::shared_ptr<ai_runner> runner)
    : model_chain_element(DEFAULT_SUMMARY_PROMPT, runner)
{
}
} // namespace docwire::local_ai
