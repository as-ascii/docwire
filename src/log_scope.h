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

#ifndef DOCWIRE_LOG_SCOPE_H
#define DOCWIRE_LOG_SCOPE_H

#include "log_entry.h"
#include <optional> // IWYU pragma: keep

namespace docwire::log::detail
{

template <typename... Args>
class scope
{
public:
	scope(source_location location, std::tuple<Args...>&& args_tuple)
		: m_location(location), m_args_tuple(std::move(args_tuple))
	{
        docwire::log::entry(
            m_location,
            std::tuple_cat(
                std::make_tuple(log::scope_enter{}),
                m_args_tuple
            )
       );
	}

    ~scope() noexcept
    {
        try
        {
            docwire::log::entry(
                m_location,
                std::tuple_cat(
                    std::make_tuple(log::scope_exit{}),
                    m_args_tuple));
        } catch(...) {}
    }

private:
	source_location m_location;
    std::tuple<Args...> m_args_tuple;
};

} // namespace docwire::log::detail

#ifdef NDEBUG
    #define DOCWIRE_LOG_SCOPE(...) \
        [[maybe_unused]] auto docwire_log_scope_object_at_line_##__LINE__ = \
            [&](const auto& loc) { \
                if constexpr (docwire::log::detail::should_log_in_release<docwire::log::scope_enter __VA_OPT__(, ) DOCWIRE_LOG_GET_TYPES(__VA_ARGS__)>()) { \
                    if (docwire::log::detail::is_logging_enabled()) { \
                        return std::optional<docwire::log::detail::scope<DOCWIRE_DIAGNOSTIC_CONTEXT_GET_TYPES(__VA_ARGS__)>>(std::in_place, loc, std::make_tuple(DOCWIRE_DIAGNOSTIC_CONTEXT_MAKE_TUPLE(__VA_ARGS__))); \
                    } \
                } \
                return std::optional<docwire::log::detail::scope<DOCWIRE_DIAGNOSTIC_CONTEXT_GET_TYPES(__VA_ARGS__)>>{}; \
            }(docwire::source_location::current());
#else
    #define DOCWIRE_LOG_SCOPE(...)                                                                                                                                                                                          \
        [[maybe_unused]] auto docwire_log_scope_object_at_line_##__LINE__ =                                                                                                                                              \
            [&](const auto& loc) {                                                                                                                                                                                      \
                if (docwire::log::detail::is_logging_enabled()) {                                                                                                                                                       \
                    return std::optional<docwire::log::detail::scope<DOCWIRE_DIAGNOSTIC_CONTEXT_GET_TYPES(__VA_ARGS__)>>(std::in_place, loc, std::make_tuple(DOCWIRE_DIAGNOSTIC_CONTEXT_MAKE_TUPLE(__VA_ARGS__)));         \
                }                                                                                                                                                                                                       \
                return std::optional<docwire::log::detail::scope<DOCWIRE_DIAGNOSTIC_CONTEXT_GET_TYPES(__VA_ARGS__)>>{};                                                                                                   \
            }(docwire::source_location::current());
#endif

#ifdef DOCWIRE_ENABLE_SHORT_MACRO_NAMES
    #define log_scope(...) DOCWIRE_LOG_SCOPE(__VA_ARGS__)
#endif

#endif // DOCWIRE_LOG_SCOPE_H
