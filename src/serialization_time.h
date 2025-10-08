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

#ifndef DOCWIRE_SERIALIZATION_TIME_H
#define DOCWIRE_SERIALIZATION_TIME_H

#include "core_export.h"
#include <ctime>
#include "serialization_base.h"

namespace docwire::serialization
{

template <>
struct serializer<struct tm>
{
    DOCWIRE_CORE_EXPORT value full(const struct tm& t) const;
    value typed_summary(const struct tm& t) const { return decorate_with_typeid(this->full(t), type_name::pretty<struct tm>()); }
};

} // namespace docwire::serialization

#endif // DOCWIRE_SERIALIZATION_TIME_H
