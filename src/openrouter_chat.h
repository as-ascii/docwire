/*********************************************************************************************************************************************/
/*  DocWire SDK: Award-winning modern data processing in C++20. SourceForge Community Choice & Microsoft support. AI-driven processing.      */
/*  Supports nearly 100 data formats, including email boxes and OCR. Boost efficiency in text extraction, web data extraction, data mining,  */
/*  document analysis. Offline processing possible for security and confidentiality                                                          */
/*                                                                                                                                           */
/*  Copyright (c) SILVERCODERS Ltd, http://silvercoders.com                                                                                  */
/*  Project homepage: https://github.com/docwire/docwire                                                                                     */
/*                                                                                                                                           */
/*  SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-DocWire-Commercial                                                                  */
/*********************************************************************************************************************************************/

#ifndef DOCWIRE_OPENROUTER_CHAT_H
#define DOCWIRE_OPENROUTER_CHAT_H

#include "chain_element.h"
#include "openrouter_export.h"
#include <string>

namespace docwire {
namespace openrouter {

enum class image_detail { low, high, automatic };

/**
 * @brief Chat chain element backed by OpenRouter (https://openrouter.ai), a
 * single API that routes requests to hundreds of models across many providers
 * (OpenAI, Anthropic, Google, Meta, Mistral, DeepSeek, and more).
 */
class DOCWIRE_OPENROUTER_EXPORT chat : public chain_element,
                                       public with_pimpl<chat> {
public:
  /**
   * @param system_message System prompt sent with every request.
   * @param api_key OpenRouter API key (create one at
   * https://openrouter.ai/keys).
   * @param model OpenRouter model slug, e.g. "openai/gpt-5" or
   * "anthropic/claude-sonnet-5". Defaults to "openrouter/auto", which lets
   * OpenRouter choose the model.
   * @param temperature Sampling temperature.
   * @param image_detail Vision hint forwarded to models that support image
   * input.
   */
  chat(const std::string &system_message, const std::string &api_key,
       const std::string &model = "openrouter/auto", float temperature = 0.5,
       image_detail image_detail = image_detail::automatic);

  /**
   * @brief Executes transform operation for given node data.
   * @see docwire::message_ptr
   * @see docwire::message_callbacks
   * @param msg The input message to process.
   * @param emit_message Callback invoked to emit produced messages.
   */
  continuation operator()(message_ptr msg,
                          const message_callbacks &emit_message) override;

  bool is_leaf() const override { return false; }

private:
  using with_pimpl<chat>::impl;
};

} // namespace openrouter
} // namespace docwire

#endif // DOCWIRE_OPENROUTER_CHAT_H
