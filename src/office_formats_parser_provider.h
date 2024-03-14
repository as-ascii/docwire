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


#ifndef DOCWIRE_OFFICE_FORMATS_PARSER_PROVIDER_H
#define DOCWIRE_OFFICE_FORMATS_PARSER_PROVIDER_H

#include "parser_provider.h"
#include "defines.h"

namespace docwire
{

class DllExport OfficeFormatsParserProvider : public ParserProvider
{
public:
  OfficeFormatsParserProvider();
  std::unique_ptr<ParserBuilder> findParserByExtension(const std::string &extension) const override;
  std::unique_ptr<ParserBuilder> findParserByData(const std::vector<char>& buffer) const override;
  std::set<std::string> getAvailableExtensions() const override;

private:
  void addExtensions(const std::vector<std::string> &extensions);
  bool isExtensionInVector(const std::string &extension, const std::vector<std::string> &extension_list) const;
  std::set<std::string> available_extensions;
};

} // namespace docwire

#endif //DOCWIRE_OFFICE_FORMATS_PARSER_PROVIDER_H