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

#include "log.h"

#include <boost/algorithm/string.hpp>
#include <boost/core/demangle.hpp>
#include <boost/date_time/c_local_time_adjustor.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include "json_serialization.h"
#include <magic_enum/magic_enum.hpp>
#include "serialization_enum.h" // IWYU pragma: keep
#include "serialization_filesystem.h" // IWYU pragma: keep
#include "serialization_thread_id.h" // IWYU pragma: keep
#include <mutex>
#include <sstream>
#include "type_name_base.h"

namespace docwire
{

static std::atomic<severity_level> log_verbosity = []() {
    if (const char* env_var = std::getenv("DOCWIRE_LOG_VERBOSITY")) {
        if (auto level = magic_enum::enum_cast<severity_level>(env_var)) {
            return *level;
        }
    }
    return severity_level(error + 1);
}();

void set_log_verbosity(severity_level severity)
{
	log_verbosity = severity;
}

bool log_verbosity_includes(severity_level severity)
{
	return severity >= log_verbosity;
}

static std::atomic<std::ostream*> log_stream = &std::clog;

static std::atomic<bool> first_log_in_stream = true;

void set_log_stream(std::ostream* stream)
{
	log_stream = stream;
	first_log_in_stream = true;
}

template<>
struct pimpl_impl<log_record_stream> : pimpl_impl_base
{
	serialization::object metadata;
	std::vector<serialization::value> log_values;

	void insert_value(serialization::value&& new_v)
	{
		log_values.push_back(std::move(new_v));
	}
};

log_record_stream::log_record_stream(severity_level severity, source_location location)
{
	if (first_log_in_stream)
	{
		*log_stream << "[" << std::endl;
		first_log_in_stream = false;
	}
	else
		*log_stream << "," << std::endl;

	boost::posix_time::ptime utc_time = boost::posix_time::second_clock::universal_time();
	boost::date_time::c_local_adjustor<boost::posix_time::ptime> local_adjustor;
	boost::posix_time::ptime local_time = local_adjustor.utc_to_local(utc_time);
	boost::posix_time::time_duration timezone_offset = local_time - utc_time;
	long timezone_offset_seconds = timezone_offset.total_seconds();
	int timezone_offset_hours = timezone_offset_seconds / 3600;
	int timezone_offset_minutes = (timezone_offset_seconds % 3600) / 60;
	std::stringstream time_stream;
	time_stream << boost::posix_time::to_iso_extended_string(local_time) << std::setw(5) << std::setfill('0')
				<< std::internal << std::showpos << timezone_offset_hours * 100 + timezone_offset_minutes;

	impl().metadata.v = {
		{"timestamp", time_stream.str()},
		{"severity", serialization::full(severity)},
		{"file", serialization::full(std::filesystem::path(location.file_name).filename())},
		{"line", static_cast<std::int64_t>(location.line)},
		{"function", docwire::type_name::pretty_function(location.function_name)},
		{"thread_id", serialization::full(std::this_thread::get_id())}
	};
}

log_record_stream::~log_record_stream()
{
	try
	{
		if (impl().log_values.size() == 1) {
			impl().metadata.v["log"] = std::move(impl().log_values.front());
		} else {
			impl().metadata.v["log"] = serialization::array{std::move(impl().log_values)};
		}
		*log_stream << serialization::to_json(impl().metadata);
	} catch(...) {
		// Don't let exceptions escape from a destructor
	}
}

log_record_stream& log_record_stream::operator<<(const serialization::value& val)
{
	impl().insert_value(serialization::value{val});
	return *this;
}

log_record_stream& log_record_stream::operator<<(const serialization::object& obj)
{
	impl().insert_value(serialization::value{obj});
	return *this;
}

class Exiter
{
public:
	~Exiter()
	{
		if (!first_log_in_stream)
			*log_stream << std::endl << "]" << std::endl;
	}
};

static Exiter exiter;

static create_log_record_stream_func_t create_log_record_stream_func =
[](severity_level severity, source_location location) -> std::unique_ptr<log_record_stream>
{
	return std::make_unique<log_record_stream>(severity, location);
};

void set_create_log_record_stream_func(create_log_record_stream_func_t func)
{
	create_log_record_stream_func = func;
}

DOCWIRE_CORE_EXPORT std::unique_ptr<log_record_stream> create_log_record_stream(severity_level severity, source_location location)
{
	return create_log_record_stream_func(severity, location);
}

namespace
{
	std::mutex cerr_log_redirection_mutex;
} // anonymous namespace

template<>
struct pimpl_impl<cerr_log_redirection> : pimpl_impl_base
{
	std::ostringstream string_stream;
	std::unique_lock<std::mutex> cerr_log_redirection_mutex_lock;
};

cerr_log_redirection::cerr_log_redirection(source_location location)
	: m_redirected(false), m_cerr_buf_backup(nullptr), m_location(location)
{
	redirect();
}

cerr_log_redirection::~cerr_log_redirection()
{
	if (m_redirected)
		restore();
}

void cerr_log_redirection::redirect()
{
	impl().cerr_log_redirection_mutex_lock = std::unique_lock<std::mutex>(cerr_log_redirection_mutex);
	m_cerr_buf_backup = std::cerr.rdbuf(impl().string_stream.rdbuf());
	m_redirected = true;
}

void cerr_log_redirection::restore()
{
	std::cerr.rdbuf(m_cerr_buf_backup);
	impl().cerr_log_redirection_mutex_lock.unlock();
	m_cerr_buf_backup = nullptr;
	if (log_verbosity_includes(debug))
	{
		std::string redirected_cerr = impl().string_stream.str();
		if (!redirected_cerr.empty())
		{
			std::unique_ptr<log_record_stream> log_record_stream = create_log_record_stream(debug, m_location);
			using map_value_type = std::pair<const std::string, serialization::value>;
			*log_record_stream << serialization::object{{map_value_type{"redirected_cerr", redirected_cerr}}};
		}
	}
	m_redirected = false;
}

} // namespace docwire
