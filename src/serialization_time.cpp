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

#include "serialization_time.h"
#include <ctime>
#include <iomanip>
#include <sstream>

namespace docwire::serialization
{

value serializer<struct tm>::full(const struct tm& t) const
{
    std::ostringstream oss;
    oss.imbue(std::locale::classic());
    oss << std::put_time(&t, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

} // namespace docwire::serialization
