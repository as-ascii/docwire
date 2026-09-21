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

#ifndef DOCWIRE_CHAIN_ELEMENT_H
#define DOCWIRE_CHAIN_ELEMENT_H

#include "core_export.h"
#include "message.h"
#include "ref_or_owned.h"

#include <concepts>
#include <memory>
#include <type_traits>
#include <utility>

namespace docwire
{

namespace pipeline
{
struct start_processing;
} // namespace pipeline

template <typename L, typename R>
class parsing_chain;

template <typename Derived>
class chain_element
{
public:
    static constexpr bool is_generator = false;
    static constexpr bool is_leaf = false;

    chain_element() = default;
    chain_element(chain_element&&) = default;
    chain_element& operator=(chain_element&&) = default;
    ~chain_element() = default;

    Derived& derived() noexcept
    {
        return static_cast<Derived&>(*this);
    }

    const Derived& derived() const noexcept
    {
        return static_cast<const Derived&>(*this);
    }

    template <typename Self, typename Other>
        requires std::same_as<std::remove_cvref_t<Self>, Derived>
              && std::derived_from<std::remove_cvref_t<Other>,
                                  chain_element<std::remove_cvref_t<Other>>>
    friend auto operator|(Self&& lhs, Other&& rhs)
    {
        using L = std::remove_cvref_t<Self>;
        using R = std::remove_cvref_t<Other>;

        parsing_chain<L, R> chain{
            ref_or_owned<L>{std::forward<Self>(lhs)},
            ref_or_owned<R>{std::forward<Other>(rhs)}
        };

        if constexpr (parsing_chain<L, R>::is_complete)
        {
            chain(std::make_shared<message<pipeline::start_processing>>(
                pipeline::start_processing{}));
        }

        return chain;
    }
};

}
#endif //DOCWIRE_CHAIN_ELEMENT_H
