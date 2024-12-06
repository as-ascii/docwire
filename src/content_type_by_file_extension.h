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

#ifndef DOCWIRE_CONTENT_TYPE_BY_FILE_EXTENSION_H
#define DOCWIRE_CONTENT_TYPE_BY_FILE_EXTENSION_H

#include "data_source.h"
#include "defines.h"

namespace docwire::content_type::by_file_extension
{

DllExport void detect(data_source& data);

} // namespace docwire::content_type::by_file_extension

#endif // DOCWIRE_CONTENT_TYPE_BY_FILE_EXTENSION_H