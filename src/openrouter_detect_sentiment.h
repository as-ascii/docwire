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

#ifndef DOCWIRE_OPENROUTER_DETECT_SENTIMENT_H
#define DOCWIRE_OPENROUTER_DETECT_SENTIMENT_H

#include "log_scope.h"
#include "openrouter_chat.h"
#include "serialization_enum.h" // IWYU pragma: keep
namespace docwire {
namespace openrouter {

class DOCWIRE_OPENROUTER_EXPORT detect_sentiment : public chat {
public:
  detect_sentiment(const std::string &api_key,
                   const std::string &model = "openrouter/auto",
                   float temperature = 0,
                   image_detail image_detail = image_detail::automatic)
      : chat("Your task is to detect sentiment for every message", api_key,
             model, temperature, image_detail) {
    DOCWIRE_LOG_SCOPE(model, temperature, image_detail);
  }
};

} // namespace openrouter
} // namespace docwire

#endif // DOCWIRE_OPENROUTER_DETECT_SENTIMENT_H
