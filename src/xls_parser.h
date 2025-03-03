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

#ifndef DOCWIRE_XLS_PARSER_H
#define DOCWIRE_XLS_PARSER_H

#include "ole_office_formats_export.h"
#include "parser.h"
#include <string>
#include <vector>

namespace docwire
{

class ThreadSafeOLEStorage;

class DOCWIRE_OLE_OFFICE_FORMATS_EXPORT XLSParser : public Parser, public with_pimpl<XLSParser>
{
	private:
		friend pimpl_impl<XLSParser>;
		using with_pimpl<XLSParser>::impl;
		using with_pimpl<XLSParser>::renew_impl;

	public:
		XLSParser();
		const std::vector<mime_type> supported_mime_types() override
		{
			return {
			mime_type{"application/vnd.ms-excel"},
			mime_type{"application/vnd.ms-excel.sheet.macroenabled.12"},
			mime_type{"application/vnd.ms-excel.template.macroenabled.12"}
			};
		};
		void parse(const data_source& data) override;
		std::string parse(ThreadSafeOLEStorage& storage);
};

} // namespace docwire

#endif
