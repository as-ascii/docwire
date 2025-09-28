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

#ifndef DOCWIRE_MAKE_ERROR_H
#define DOCWIRE_MAKE_ERROR_H

#include <boost/preprocessor/punctuation/comma_if.hpp>
#include <boost/preprocessor/seq/for_each_i.hpp>
#include <boost/preprocessor/seq/seq.hpp>
#include <boost/preprocessor/stringize.hpp>
#include <boost/preprocessor/variadic/to_seq.hpp>
#include "error.h" // IWYU pragma: keep
#include <tuple> // IWYU pragma: keep
#include <type_traits> // IWYU pragma: keep

#define DOCWIRE_MAKE_ERROR_TUPLE_ELEM(r, data, i, elem) \
    BOOST_PP_COMMA_IF(i) errors::convert_to_context(BOOST_PP_STRINGIZE(elem), elem)

#define DOCWIRE_MAKE_ERROR_TUPLE(...) \
    BOOST_PP_SEQ_FOR_EACH_I(DOCWIRE_MAKE_ERROR_TUPLE_ELEM, _, BOOST_PP_VARIADIC_TO_SEQ(__VA_ARGS__))

#define DOCWIRE_MAKE_ERROR(...) \
    [&](const auto& location) { \
        auto context_tuple = std::make_tuple(DOCWIRE_MAKE_ERROR_TUPLE(__VA_ARGS__)); \
        return std::apply([&](auto&&... args) { \
            return errors::impl<std::remove_cvref_t<decltype(args)>...>(context_tuple, location); \
        }, context_tuple); \
    }(DOCWIRE_CURRENT_LOCATION())

#define DOCWIRE_MAKE_ERROR_PTR(...) \
	std::make_exception_ptr(DOCWIRE_MAKE_ERROR(__VA_ARGS__))

#ifdef DOCWIRE_ENABLE_SHORT_MACRO_NAMES
#define make_error DOCWIRE_MAKE_ERROR
#define make_error_ptr DOCWIRE_MAKE_ERROR_PTR
#endif

#endif
