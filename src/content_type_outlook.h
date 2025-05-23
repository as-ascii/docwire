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

#ifndef DOCWIRE_CONTENT_TYPE_OUTLOOK_H
#define DOCWIRE_CONTENT_TYPE_OUTLOOK_H

#include "chain_element.h"
#include "content_type_export.h"
#include "content_type_by_signature.h"
#include "data_source.h"
#include "tags.h"

namespace docwire::content_type::outlook
{

DOCWIRE_CONTENT_TYPE_EXPORT void detect(data_source& data,
    const by_signature::database& signatures_db_to_use = by_signature::database{});

class detector : public ChainElement
{
public:

    detector(ref_or_owned<by_signature::database> signatures_db_to_use = by_signature::database{})
        : m_signatures_db_to_use(signatures_db_to_use) {}

    continuation operator()(Tag&& tag, const emission_callbacks& emit_tag) override
    {
        if (!std::holds_alternative<data_source>(tag))
	        return emit_tag(std::move(tag));
	    data_source& data = std::get<data_source>(tag);
        detect(data);
        return emit_tag(std::move(tag));
    }

    bool is_leaf() const override
	{
		return false;
	}

private:
    ref_or_owned<by_signature::database> m_signatures_db_to_use;
};

} // namespace docwire::content_type::outlook

#endif // DOCWIRE_CONTENT_TYPE_OUTLOOK
