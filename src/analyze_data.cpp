/*********************************************************************************************************************************************/
/*  DocWire SDK: Award-winning modern data processing in C++20. SourceForge Community Choice & Microsoft support. AI-driven processing.      */
/*  Supports nearly 100 data formats, including email boxes and OCR. Boost efficiency in text extraction, web data extraction, data mining,  */
/*  document analysis. Offline processing possible for security and confidentiality                                                          */
/*                                                                                                                                           */
/*  Copyright (c) SILVERCODERS Ltd, http://silvercoders.com                                                                                  */
/*  Project homepage: https://github.com/docwire/docwire                                                                                     */
/*                                                                                                                                           */
/*  SPDX-License-Identifier: GPL-2.0-only OR LicenseRef-DocWire-Commercial                                                                   */
/*********************************************************************************************************************************************/

#include "analyze_data.h"

#include "log.h"

namespace docwire
{
namespace openai
{

AnalyzeData::AnalyzeData(const std::string& api_key, Model model, float temperature, ImageDetail image_detail)
	: Chat("Your task is analyze data in every message and create a summary highlighting the most important insights, trends, key patterns, statistics, findings and other revelant information. Include conclusions that are usually made for type of data you will find in message.",
		api_key, model, temperature, image_detail)
{
	docwire_log_func_with_args(temperature);
}

} // namespace openai
} // namespace docwire
