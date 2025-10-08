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

#include "type_name.h"
#include <boost/algorithm/string.hpp>
#include <boost/core/demangle.hpp>

namespace docwire::type_name
{

namespace
{

std::string normalize_name(const std::string& name)
{
	std::string normalized = name;
	boost::algorithm::erase_all(normalized, "__cdecl ");
	boost::algorithm::erase_all(normalized, "::__cxx11");
	boost::algorithm::erase_all(normalized, "__1::");
	boost::algorithm::erase_all(normalized, "virtual ");
	boost::algorithm::erase_all(normalized, "class ");
	boost::algorithm::erase_all(normalized, "struct ");
	boost::algorithm::replace_all(normalized, "(void)", "()");
	boost::algorithm::replace_all(normalized, " [", "[");
	boost::algorithm::replace_all(normalized, ", ", ",");
	boost::algorithm::replace_all(normalized, " >", ">");
	return normalized;
}

} // anonymous namespace

std::string from_type_index(std::type_index t)
{
	return normalize_name(boost::core::demangle(t.name()));
}

std::string pretty_function(const std::string& function_name)
{
	return normalize_name(function_name);
}

} // namespace docwire::type_name
