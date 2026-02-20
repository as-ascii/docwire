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
/*  SPDX-License-Identifier: GPL-2.0-only OR LicenseRef-DocWire-Commercial */
/*********************************************************************************************************************************************/

#ifndef DOCWIRE_LOCAL_AI_AI_RUNNER_H
#define DOCWIRE_LOCAL_AI_AI_RUNNER_H

#include "local_ai_export.h"
#include <stdexcept>
#include <string>
#include <vector>

namespace docwire::local_ai {

class DOCWIRE_LOCAL_AI_EXPORT ai_runner {
  public:
    virtual ~ai_runner() = default;

    virtual std::string process(const std::string& input) = 0;

    virtual std::vector<double> embed(const std::string&) = 0;
};

} // namespace docwire::local_ai

#endif
