/*********************************************************************************************************************************************/
/*  DocWire SDK: Award-winning modern data processing in C++20. SourceForge Community Choice &
 * Microsoft support. AI-driven processing.      */
/*  Supports nearly 100 data formats, including email boxes and OCR. Boost efficiency in text
 * extraction, web data extraction, data mining,  */
/*  document analysis. Offline processing possible for security and confidentiality */
/*                                                                                                                                           */
/*  Copyright (c) SILVERCODERS Ltd, http://silvercoders.com */
/*  Project homepage: https://github.com/docwire/docwire */
/*                                                                                                                                           */
/*  SPDX-License-Identifier: GPL-2.0-only OR LicenseRef-DocWire-Commercial */
/*********************************************************************************************************************************************/

#ifndef DOCWIRE_LOCAL_AI_SUMMARIZE_H
#define DOCWIRE_LOCAL_AI_SUMMARIZE_H

#include "local_ai_export.h"
#include "model_chain_element.h"

namespace docwire::local_ai
{

class DOCWIRE_LOCAL_AI_EXPORT local_summarize : public model_chain_element
{
  public:
    explicit local_summarize();

    explicit local_summarize(std::shared_ptr<ai_runner> runner);
};

} // namespace docwire::local_ai

#endif // DOCWIRE_LOCAL_SUMMARIZE_H
