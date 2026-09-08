/*********************************************************************************************************************************************/
/*  DocWire SDK: Award-winning modern data processing in C++20. SourceForge
 * Community Choice & Microsoft support. AI-driven processing.      */
/*  Supports nearly 100 data formats, including email boxes and OCR. Boost
 * efficiency in text extraction, web data extraction, data mining,  */
/*  document analysis. Offline processing possible for security and
 * confidentiality                                                          */
/*                                                                                                                                           */
/*  Copyright (c) SILVERCODERS Ltd, http://silvercoders.com */
/*  Project homepage: https://github.com/docwire/docwire */
/*                                                                                                                                           */
/*  SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-DocWire-Commercial */
/*********************************************************************************************************************************************/

#ifndef DOCWIRE_OPENROUTER_EXTRACT_KEYWORDS_H
#define DOCWIRE_OPENROUTER_EXTRACT_KEYWORDS_H

#include "log_scope.h"
#include "openrouter_chat.h"
#include "serialization_enum.h" // IWYU pragma: keep
#include <string>
namespace docwire {
namespace openrouter {

class DOCWIRE_OPENROUTER_EXPORT extract_keywords : public chat {
public:
  extract_keywords(unsigned int max_keywords, const std::string &api_key,
                   const std::string &model = "openrouter/auto",
                   float temperature = 0,
                   image_detail image_detail = image_detail::automatic)
      : chat("Your task is to identify and extract " +
                 std::to_string(max_keywords) +
                 " most important keywords or key phrases from every message. "
                 "The goal is to capture the most relevant and significant "
                 "terms within the text.",
             api_key, model, temperature, image_detail) {
    DOCWIRE_LOG_SCOPE(max_keywords, model, temperature, image_detail);
  }
};

} // namespace openrouter
} // namespace docwire

#endif // DOCWIRE_OPENROUTER_EXTRACT_KEYWORDS_H
