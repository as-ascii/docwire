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

#ifndef DOCWIRE_VARIANT_CHAIN_ELEMENT_H
#define DOCWIRE_VARIANT_CHAIN_ELEMENT_H

#include "chain_element.h"
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>

namespace docwire
{

/**
 * @brief A chain element that does nothing and only forwards.
 *
 * Used as one alternative in `std::variant` pipelines to represent an absent
 * optional step while keeping the pipeline type stable.
 */
class noop_transformer : public chain_element<noop_transformer>
{
public:
    continuation operator()(message_ptr msg,
                            const message_callbacks& emit_message)
    {
        return emit_message(std::move(msg));
    }
};

/**
 * @brief A pipeline element wrapping a `std::variant` of alternative chain elements.
 *
 * `variant_chain_element` allows a single, statically typed pipeline to select
 * one of several alternative chain elements at runtime without falling back to
 * dynamic dispatch. Selection is performed with `std::visit`, which the compiler
 * lowers to a tagged-union jump table and inlines a direct call per alternative.
 * This stays consistent with the "no `virtual`, no `std::function`" policy.
 *
 * All alternatives MUST share the same pipeline category:
 *  - every alternative must agree on `is_generator`, and
 *  - every alternative must agree on `is_leaf`.
 *
 * A variant mixing intermediate elements, sources, or terminals is rejected at
 * compile time via `static_assert`. Use `noop_transformer` as an alternative to
 * represent an absent (optional) step.
 *
 * @tparam Variant A `std::variant<Ts...>` whose alternatives are chain elements.
 *
 * @see noop_transformer
 * @see chain_element
 * @see chain_element_type
 */
template <typename Variant>
class variant_chain_element;

template <typename... Ts>
class variant_chain_element<std::variant<Ts...>>
    : public chain_element<variant_chain_element<std::variant<Ts...>>>
{
private:
    static_assert(sizeof...(Ts) > 0,
                  "variant_chain_element requires at least one alternative");

    static constexpr bool first_is_generator =
        std::tuple_element_t<0, std::tuple<Ts...>>::is_generator;

    static constexpr bool first_is_leaf =
        std::tuple_element_t<0, std::tuple<Ts...>>::is_leaf;

    static_assert(((Ts::is_generator == first_is_generator) && ...),
                  "All variant chain elements must share the same generator category");

    static_assert(((Ts::is_leaf == first_is_leaf) && ...),
                  "All variant chain elements must share the same leaf category");

public:
    static constexpr bool is_generator = first_is_generator;
    static constexpr bool is_leaf = first_is_leaf;

    variant_chain_element() = default;

    /**
     * @brief Constructs the element from a populated `std::variant`.
     *
     * @param value The variant holding the active alternative.
     */
    variant_chain_element(std::variant<Ts...> value)
        : m_value{std::move(value)}
    {
    }

    /**
     * @brief Dispatches the message to the currently active alternative.
     *
     * @param msg The message to process.
     * @param emit_message The downstream emission callbacks.
     * @return The continuation status reported by the active alternative.
     */
    continuation operator()(message_ptr msg, const message_callbacks& emit_message)
    {
        return std::visit(
            [&](auto& element) -> continuation
            {
                return element(std::move(msg), emit_message);
            },
            m_value);
    }

private:
    std::variant<Ts...> m_value;
};

template <typename... Ts>
variant_chain_element(std::variant<Ts...>)
    -> variant_chain_element<std::variant<Ts...>>;

/**
 * @brief Pipes a chain element into a raw `std::variant` of chain elements.
 *
 * Allows spelling variant alternatives directly, without explicitly naming
 * `variant_chain_element`.
 *
 * @see variant_chain_element
 */
template <typename L, typename... Ts>
    requires chain_element_type<L> && (chain_element_type<Ts> && ...)
auto operator|(L&& lhs, std::variant<Ts...> rhs)
{
    using variant_type = std::variant<Ts...>;
    return std::forward<L>(lhs)
         | variant_chain_element<variant_type>{std::move(rhs)};
}

/**
 * @brief Pipes a raw `std::variant` of chain elements into a chain element.
 *
 * Allows spelling variant alternatives directly, without explicitly naming
 * `variant_chain_element`.
 *
 * @see variant_chain_element
 */
template <typename... Ts, typename R>
    requires (chain_element_type<Ts> && ...) && chain_element_type<R>
auto operator|(std::variant<Ts...> lhs, R&& rhs)
{
    using variant_type = std::variant<Ts...>;
    return variant_chain_element<variant_type>{std::move(lhs)}
         | std::forward<R>(rhs);
}

} // namespace docwire

#endif // DOCWIRE_VARIANT_CHAIN_ELEMENT_H
