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


#ifndef DOCWIRE_PST_PARSER_H
#define DOCWIRE_PST_PARSER_H

#include "mail_export.h"
#include <vector>

#include "parser.h"
#include "pimpl.h"

namespace docwire
{

class DOCWIRE_MAIL_EXPORT PSTParser : public Parser, public with_pimpl<PSTParser>
{
private:
  using with_pimpl<PSTParser>::impl;
  friend pimpl_impl<PSTParser>;

public:

  void parse(const data_source& data) override;

  const std::vector<mime_type> supported_mime_types() override
  {
    return {
    mime_type{"application/vnd.ms-outlook-pst"},
    mime_type{"application/vnd.ms-outlook-ost"}
    };
  };

  PSTParser();
};

} // namespace docwire

#endif //DOCWIRE_PST_PARSER_H
