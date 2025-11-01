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

#include <string>

namespace docwire::serialization
{

namespace
{
    // Helper to convert an integer to a two-digit, zero-padded string.
    std::string to_string_padded(int value)
    {
        if (value >= 0 && value < 10)
            return "0" + std::to_string(value);
        else
            return std::to_string(value);
    }
}

value serializer<struct tm>::full(const struct tm& t) const
{
    // We build the string manually for performance, portability, and thread-safety.
    return std::to_string(t.tm_year + 1900) + '-' +
            to_string_padded(t.tm_mon + 1) + '-' + to_string_padded(t.tm_mday) + ' ' +
            to_string_padded(t.tm_hour) + ':' + to_string_padded(t.tm_min) + ':' +
            to_string_padded(t.tm_sec);
}

} // namespace docwire::serialization
