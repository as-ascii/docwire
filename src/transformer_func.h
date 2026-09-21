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

#ifndef DOCWIRE_TRANSFORMER_FUNC_H
#define DOCWIRE_TRANSFORMER_FUNC_H

#include "chain_element.h"
#include "core_export.h"
#include <functional>
#include <utility>

namespace docwire
{

using message_transform_func = std::function<continuation(message_ptr, const message_callbacks& emit_message)>;

/**
 * @brief Wraps single function (message_transform_func) into chain_element object
 */
class transformer_func : public chain_element<transformer_func>
{
public:
  /**
   * @param transformer_function callback function, which will be called in transform().
   */
  transformer_func(message_transform_func transformer_function)
    : m_transformer_function{std::move(transformer_function)}
  {}

	/**
	 * @brief Executes transform on the given message.
	 * @see docwire::message_ptr
	 * @param msg Incoming message.
	 * @param emit_message Callback to emit downstream messages.
	 */
	continuation operator()(message_ptr msg, const message_callbacks& emit_message)
	{
		return m_transformer_function(std::move(msg), emit_message);
	}

private:
  message_transform_func m_transformer_function;
};

} // namespace docwire

#endif //DOCWIRE_TRANSFORMER_FUNC_H
