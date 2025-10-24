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

#include "charset_converter.h"

#include <cstring>
#include <iconv.h>
#include "throw_if.h"
#include <map>
#include <utility>
#include <memory>

namespace docwire
{

template<>
struct pimpl_impl<charset_converter> : pimpl_impl_base
{
	struct iconv_descriptor
	{
		iconv_t descriptor;
		
		iconv_descriptor(const std::string& from, const std::string& to)
		{
			descriptor = iconv_open(to.c_str(), from.c_str());
			throw_if(descriptor == (iconv_t)(-1), "iconv_open() failed", strerror(errno), from, to);
		}
		~iconv_descriptor()
		{
			if (descriptor != (iconv_t)(-1))
				iconv_close(descriptor);
		}

		iconv_descriptor(const iconv_descriptor&) = delete;
		iconv_descriptor& operator=(const iconv_descriptor&) = delete;
		iconv_descriptor(iconv_descriptor&&) = delete;
		iconv_descriptor& operator=(iconv_descriptor&&) = delete;
	};

	pimpl_impl(const std::string& from, const std::string& to) : m_from(from), m_to(to) {}

	// Each thread gets its own iconv descriptor. This is necessary because
	// iconv() modifies state associated with the descriptor, making a shared descriptor not thread-safe.
	// We use a map to store a descriptor for each unique pair of encodings per thread.
	iconv_t get_thread_local_descriptor()
	{
		thread_local std::map<std::pair<std::string, std::string>, std::unique_ptr<iconv_descriptor>> descriptors;
		auto key = std::make_pair(m_from, m_to);
		auto it = descriptors.find(key);
		if (it == descriptors.end())
		{
			it = descriptors.emplace(std::move(key), std::make_unique<iconv_descriptor>(m_from, m_to)).first;
		}
		return it->second->descriptor;
	}

	std::string m_from;
	std::string m_to;
};

charset_converter::charset_converter(const std::string &from, const std::string &to)
	: with_pimpl<charset_converter>(from, to)
{
}

charset_converter::~charset_converter() = default;

std::string charset_converter::convert(std::string_view input)
{	
	if (input.empty())
		return "";

	// iconv API is not const-correct for the input buffer.
	const char* inptr = input.data();
	size_t inbytesleft = input.length();

	iconv_t descriptor = impl().get_thread_local_descriptor();
	// Reset descriptor to its initial state for a new conversion.
	iconv(descriptor, nullptr, nullptr, nullptr, nullptr);

	// A reasonable starting point for most conversions. UTF-8 can take up to 4 bytes per character.
	size_t output_size = input.length() * 2;
	std::string output(output_size, '\0');
	size_t total_written = 0;

	while (inbytesleft > 0)
	{
		char* outptr = output.data() + total_written;
		size_t outbytesleft = output.size() - total_written;

		size_t result = iconv(descriptor, const_cast<char**>(&inptr), &inbytesleft, &outptr, &outbytesleft);
		total_written = output.size() - outbytesleft;

		if (result == (size_t)-1)
		{
			if (errno == E2BIG) // Output buffer is full.
			{
				// Double the buffer size and continue.
				output.resize(output.size() * 2);
			}
			else // A non-recoverable error occurred.
				throw make_error("iconv() failed", strerror(errno));
		}
	}
	output.resize(total_written);
	return output;
}

} // namespace docwire
