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

#include "pdf_parser.h"

#include <algorithm>
#include <codecvt>
#include "data_stream.h"
#include "error_tags.h"
#include <iostream>
#include <list>
#include "log.h"
#include <map>
#include "make_error.h"
#include "misc.h"
#include <mutex>
#include <new>
#include <podofo/podofo.h>
#include <set>
#include <stdlib.h>
#include <string.h>
#include "throw_if.h"
#include <vector>
#include <zlib.h>

namespace docwire
{

namespace
{
	std::mutex load_document_mutex;
} // unnamed namespace

//some functions specific for this parser

//converts hex char (2 bytes) to single char. Examples:
//41 will be converted to single 'A'
//30 will be converted to '0'
static unsigned char hex_char_to_single_char(const char* hex_char)
{
	//first four bits
	unsigned char res = 0;
	if (hex_char[0] >= 'A')
		res = (hex_char[0] - 'A' + 10) << 4;
	else
		res = (hex_char[0] - '0') << 4;
	if (hex_char[1] >= 'A')
		return res | (hex_char[1] - 'A' + 10);
	return res | (hex_char[1] - '0');
}

//just like above, but in reverse order
static std::string char_to_hex_char(const char ch)
{
	char res[2];
	res[0] = ((ch & 0xF0) >> 4);
	if (res[0] < 10)
		res[0] += '0';
	else
		res[0] += ('A' - 10);
	res[1] = ch & 0x0F;
	if (res[1] < 10)
		res[1] += '0';
	else
		res[1] += ('A' - 10);
	return std::string(res, 2);
}

static bool hex_char_is_valid(const char hex_char)
{
	return (hex_char >= '0' && hex_char <= '9') || (hex_char >= 'A' && hex_char <= 'F');
}

static void normalize_hex_char(char& ch)
{
	if (ch >= 'a' && ch <= 'f')
		ch -= ('a' - 'A');
}

static std::string unicode_codepoint_to_utf8(unsigned int codepoint)
{
	char out[4];
	if (codepoint < 0x80)
	{
		out[0] = (char)codepoint;
		return std::string(out, 1);
	}
	else if (codepoint < 0x800)
	{
		out[0] = 0xC0 | (codepoint >> 6);
		out[1] = 0x80 | (codepoint & 0x0000003F);
		return std::string(out, 2);
	}
	else if (codepoint < 0x10000)
	{
		out[0] = 0xE0 | (codepoint >> 12);
		out[1] = 0x80 | ((codepoint & 0x00000FFF) >> 6);
		out[2] = 0x80 | (codepoint & 0x0000003F);
		return std::string(out, 3);
	}
	else
	{
		out[0] = 0xF0 | (codepoint >> 18);
		out[1] = 0x80 | ((codepoint & 0x0003FFFF) >> 12);
		out[2] = 0x80 | ((codepoint & 0x00000FFF) >> 6);
		out[3] = 0x80 | (codepoint & 0x0000003F);
		return std::string(out, 4);
	}
	return "";
}

static std::string utf8_codepoint_to_utf8(unsigned int utf8_codepoint)
{
	char out[4];
	if (utf8_codepoint < 0x100)
	{
		out[0] = utf8_codepoint;
		return std::string(out, 1);
	}
	else if (utf8_codepoint < 0x10000)
	{
		out[0] = utf8_codepoint >> 8;
		out[1] = (utf8_codepoint & 0x000000FF);
		return std::string(out, 2);
	}
	else if (utf8_codepoint < 0x1000000)
	{
		out[0] = utf8_codepoint >> 16;
		out[1] = (utf8_codepoint & 0x0000FF00) >> 8;
		out[2] = (utf8_codepoint & 0x000000FF);
		return std::string(out, 3);
	}
	else
	{
		out[0] = utf8_codepoint >> 24;
		out[1] = (utf8_codepoint & 0x00FF0000) >> 16;
		out[2] = (utf8_codepoint & 0x0000FF00) >> 8;
		out[3] = (utf8_codepoint & 0x000000FF);
		return std::string(out, 4);
	}
	return "";
}

static void uint_to_hex_string(unsigned int value, std::string& output)
{
	char buffer[20];
	sprintf(buffer, "%x", value);
	for (int i = 0; i < 20 && buffer[i] != 0; ++i)
	{
		if (buffer[i] >= 'a')
			buffer[i] = buffer[i] - ('a' - 'A');
	}
	if (strlen(buffer) % 2 == 1)
		output += '0';
	output += buffer;
}

static unsigned int hex_string_to_uint(const char* hex_number, size_t size)
{
	unsigned int val = 0;
	for (int i = 0; i < size; ++i)
	{
		val = val << 4;
		if (hex_number[i] <= '9')
			val = val | (hex_number[i] - '0');
		else
			val = val | (hex_number[i] - 'A' + 10);
	}
	return val;
}

static std::string utf16be_to_utf8(std::string& utf16be)
{
	unsigned int utf16 = 0;
	if (utf16be.length() % 4 != 0)
	{
		for (int i = 0; i < 4 - utf16be.length() % 4; ++i)
			utf16be += '0';
	}
	std::string ret;
	for (unsigned int index = 0; index < utf16be.length(); index += 4)
	{
		if (utf16be[index] == 'D' && utf16be[index + 1] == '8' && index + 8 <= utf16be.length())
		{
			utf16 = hex_string_to_uint(&utf16be[index], 8);
			index += 4;
		}
		else
			utf16 = hex_string_to_uint(&utf16be[index], 4);
		ret += unichar_to_utf8(utf16);
	}
	return ret;
}

//increments "hex string". Example:
//0FCB will be converted to 0FCC
static void increment_hex_string(std::string& hex_string)
{
	if (hex_string.length() == 0)
		hex_string = "01";
	else
	{
		int index = hex_string.length() - 1;
		while (true)
		{
			if (hex_string[index] < '9' || hex_string[index] < 'F')
			{
				++hex_string[index];
				return;
			}
			if (hex_string[index] == '9')
			{
				hex_string[index] = 'A';
				return;
			}
			if (hex_string[index] == 'F')
			{
				hex_string[index] = '0';
				--index;
			}
			if (index < 0)
			{
				hex_string = "0001" + hex_string;
				return;
			}
		}
	}
}

void parsePDFDate(tm& date, const std::string& str_date)
{
	if (str_date.length() < 14)
		return;
	std::string year = str_date.substr(0, 4);
	date.tm_year = strtol(year.c_str(), NULL, 10);
	std::string month = str_date.substr(4, 2);
	date.tm_mon = strtol(month.c_str(), NULL, 10);
	std::string day = str_date.substr(6, 2);
	date.tm_mday = strtol(day.c_str(), NULL, 10);
	std::string hour = str_date.substr(8, 2);
	date.tm_hour = strtol(hour.c_str(), NULL, 10);
	std::string minute = str_date.substr(10, 2);
	date.tm_min = strtol(minute.c_str(), NULL, 10);
	std::string second = str_date.substr(12, 2);
	date.tm_sec = strtol(second.c_str(), NULL, 10);
	date.tm_year -= 1900;
	--date.tm_mon;
}

bool last_is_new_line(const std::string& str)
{
	return str.length() > 0 && str[str.length() - 1] == '\n';
}

//predefined encodings:
const static unsigned int PdfDocEncodingUtf8[256] =
{
	0x0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7,
	0x8, 0x9, 0xa, 0xb, 0xc, 0xd, 0xe, 0xf,
	0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x17, 0x17,
	0xcb98, 0xcb87, 0xcb86, 0xcb99, 0xcb9d, 0xcb9b, 0xcb9a, 0xcb9c,
	0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27,
	0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f,
	0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37,
	0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f,
	0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47,
	0x48, 0x49, 0x4a, 0x4b, 0x4c, 0x4d, 0x4e, 0x4f,
	0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57,
	0x58, 0x59, 0x5a, 0x5b, 0x5c, 0x5d, 0x5e, 0x5f,
	0x60, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67,
	0x68, 0x69, 0x6a, 0x6b, 0x6c, 0x6d, 0x6e, 0x6f,
	0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77,
	0x78, 0x79, 0x7a, 0x7b, 0x7c, 0x7d, 0x7e, 0x0,
	0xe280a2, 0xe280a0, 0xe280a1, 0xe280a6, 0xe28094, 0xe28093, 0xc692, 0xe28184,
	0xe280b9, 0xe280ba, 0xe28892, 0xe280b0, 0xe2809e, 0xe2809c, 0xe2809d, 0xe28098,
	0xe28099, 0xe2809a, 0xe284a2, 0xefac81, 0xefac82, 0xc581, 0xc592, 0xc5a0,
	0xc5b8, 0xc5bd, 0xc4b1, 0xc582, 0xc593, 0xc5a1, 0xc5be, 0x0,
	0xe282ac, 0xc2a1, 0xc2a2, 0xc2a3, 0xc2a4, 0xc2a5, 0xc2a6, 0xc2a7,
	0xc2a8, 0xc2a9, 0xc2aa, 0xc2ab, 0xc2ac, 0x0, 0xc2ae, 0xc2af,
	0xc2b0, 0xc2b1, 0xc2b2, 0xc2b3, 0xc2b4, 0xc2b5, 0xc2b6, 0xc2b7,
	0xc2b8, 0xc2b9, 0xc2ba, 0xc2bb, 0xc2bc, 0xc2bd, 0xc2be, 0xc2bf,
	0xc380, 0xc381, 0xc382, 0xc383, 0xc384, 0xc385, 0xc386, 0xc387,
	0xc388, 0xc389, 0xc38a, 0xc38b, 0xc38c, 0xc38d, 0xc38e, 0xc38f,
	0xc390, 0xc391, 0xc392, 0xc393, 0xc394, 0xc395, 0xc396, 0xc397,
	0xc398, 0xc399, 0xc39a, 0xc39b, 0xc39c, 0xc39d, 0xc39e, 0xc39f,
	0xc3a0, 0xc3a1, 0xc3a2, 0xc3a3, 0xc3a4, 0xc3a5, 0xc3a6, 0xc3a7,
	0xc3a8, 0xc3a9, 0xc3aa, 0xc3ab, 0xc3ac, 0xc3ad, 0xc3ae, 0xc3af,
	0xc3b0, 0xc3b1, 0xc3b2, 0xc3b3, 0xc3b4, 0xc3b5, 0xc3b6, 0xc3b7,
	0xc3b8, 0xc3b9, 0xc3ba, 0xc3bb, 0xc3bc, 0xc3bd, 0xc3be, 0xc3bf
};

const static unsigned int WinAnsiEncodingUtf8[256] =
{
	0x0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7,
	0x8, 0x9, 0xa, 0xb, 0xc, 0xd, 0xe, 0xf,
	0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
	0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
	0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27,
	0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f,
	0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37,
	0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f,
	0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47,
	0x48, 0x49, 0x4a, 0x4b, 0x4c, 0x4d, 0x4e, 0x4f,
	0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57,
	0x58, 0x59, 0x5a, 0x5b, 0x5c, 0x5d, 0x5e, 0x5f,
	0x60, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67,
	0x68, 0x69, 0x6a, 0x6b, 0x6c, 0x6d, 0x6e, 0x6f,
	0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77,
	0x78, 0x79, 0x7a, 0x7b, 0x7c, 0x7d, 0x7e, 0x7f,
	0xe282ac, 0x0, 0xe2809a, 0xc692, 0xe2809e, 0xe280a6, 0xe280a0, 0xe280a1,
	0xcb86, 0xe280b0, 0xc5a0, 0xe280b9, 0xc592, 0x0, 0xc5bd, 0x0,
	0x0, 0xe28098, 0xe28099, 0xe2809c, 0xe2809d, 0xe280a2, 0xe28093, 0xe28094,
	0xcb9c, 0xe284a2, 0xc5a1, 0xe280ba, 0xc593, 0x0, 0xc5be, 0xc5b8,
	0xc2a0, 0xc2a1, 0xc2a2, 0xc2a3, 0xc2a4, 0xc2a5, 0xc2a6, 0xc2a7,
	0xc2a8, 0xc2a9, 0xc2aa, 0xc2ab, 0xc2ac, 0xc2ad, 0xc2ae, 0xc2af,
	0xc2b0, 0xc2b1, 0xc2b2, 0xc2b3, 0xc2b4, 0xc2b5, 0xc2b6, 0xc2b7,
	0xc2b8, 0xc2b9, 0xc2ba, 0xc2bb, 0xc2bc, 0xc2bd, 0xc2be, 0xc2bf,
	0xc380, 0xc381, 0xc382, 0xc383, 0xc384, 0xc385, 0xc386, 0xc387,
	0xc388, 0xc389, 0xc38a, 0xc38b, 0xc38c, 0xc38d, 0xc38e, 0xc38f,
	0xc390, 0xc391, 0xc392, 0xc393, 0xc394, 0xc395, 0xc396, 0xc397,
	0xc398, 0xc399, 0xc39a, 0xc39b, 0xc39c, 0xc39d, 0xc39e, 0xc39f,
	0xc3a0, 0xc3a1, 0xc3a2, 0xc3a3, 0xc3a4, 0xc3a5, 0xc3a6, 0xc3a7,
	0xc3a8, 0xc3a9, 0xc3aa, 0xc3ab, 0xc3ac, 0xc3ad, 0xc3ae, 0xc3af,
	0xc3b0, 0xc3b1, 0xc3b2, 0xc3b3, 0xc3b4, 0xc3b5, 0xc3b6, 0xc3b7,
	0xc3b8, 0xc3b9, 0xc3ba, 0xc3bb, 0xc3bc, 0xc3bd, 0xc3be, 0xc3bf
};

const static unsigned int MacRomanEncodingUtf8[256] =
{
	0x0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7,
	0x8, 0x9, 0xa, 0xb, 0xc, 0xd, 0xe, 0xf,
	0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
	0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
	0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27,
	0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f,
	0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37,
	0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f,
	0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47,
	0x48, 0x49, 0x4a, 0x4b, 0x4c, 0x4d, 0x4e, 0x4f,
	0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57,
	0x58, 0x59, 0x5a, 0x5b, 0x5c, 0x5d, 0x5e, 0x5f,
	0x60, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67,
	0x68, 0x69, 0x6a, 0x6b, 0x6c, 0x6d, 0x6e, 0x6f,
	0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77,
	0x78, 0x79, 0x7a, 0x7b, 0x7c, 0x7d, 0x7e, 0x7f,
	0xc384, 0xc385, 0xc387, 0xc389, 0xc391, 0xc396, 0xc39c, 0xc3a1,
	0xc3a0, 0xc3a2, 0xc3a4, 0xc3a3, 0xc3a5, 0xc3a7, 0xc3a9, 0xc3a8,
	0xc3aa, 0xc3ab, 0xc3ad, 0xc3ac, 0xc3ae, 0xc3af, 0xc3b1, 0xc3b3,
	0xc3b2, 0xc3b4, 0xc3b6, 0xc3b5, 0xc3ba, 0xc3b9, 0xc3bb, 0xc3bc,
	0xe280a0, 0xc2b0, 0xc2a2, 0xc2a3, 0xc2a7, 0xe280a2, 0xc2b6, 0xc39f,
	0xc2ae, 0xc2a9, 0xe284a2, 0xc2b4, 0xc2a8, 0xe289a0, 0xc386, 0xc398,
	0xe2889e, 0xc2b1, 0xe289a4, 0xe289a5, 0xc2a5, 0xc2b5, 0xe28882, 0xe28891,
	0xe2888f, 0xcf80, 0xe288ab, 0xc2aa, 0xc2ba, 0xcea9, 0xc3a6, 0xc3b8,
	0xc2bf, 0xc2a1, 0xc2ac, 0xe2889a, 0xc692, 0xe28988, 0xe28886, 0xc2ab,
	0xc2bb, 0xe280a6, 0xc2a0, 0xc380, 0xc383, 0xc395, 0xc592, 0xc593,
	0xe28093, 0xe28094, 0xe2809c, 0xe2809d, 0xe28098, 0xe28099, 0xc3b7, 0xe2978a,
	0xc3bf, 0xc5b8, 0xe28184, 0xe282ac, 0xe280b9, 0xe280ba, 0xefac81, 0xefac82,
	0xe280a1, 0xc2b7, 0xe2809a, 0xe2809e, 0xe280b0, 0xc382, 0xc38a, 0xc381,
	0xc38b, 0xc388, 0xc38d, 0xc38e, 0xc38f, 0xc38c, 0xc393, 0xc394,
	0xefa3bf, 0xc392, 0xc39a, 0xc39b, 0xc399, 0xc4b1, 0xcb86, 0xcb9c,
	0xc2af, 0xcb98, 0xcb99, 0xcb9a, 0xc2b8, 0xcb9d, 0xcb9b, 0xcb87
};

const static unsigned int MacExpertEncodingUtf8[256] =
{
	0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
	0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
	0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
	0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
	0x20, 0xef9ca1, 0xef9bb8, 0xef9ea2, 0xef9ca4, 0xef9ba4, 0xef9ca6, 0xef9eb4,
	0xe281bd, 0xe281be, 0xe280a5, 0xe280a4, 0x2c, 0x2d, 0x2e, 0xe28184,
	0xef9cb0, 0xef9cb1, 0xef9cb2, 0xef9cb3, 0xef9cb4, 0xef9cb5, 0xef9cb6, 0xef9cb7,
	0xef9cb8, 0xef9cb9, 0x3a, 0x3b, 0x0, 0xef9b9e, 0x0, 0xef9cbf,
	0x0, 0x0, 0x0, 0x0, 0xef9fb0, 0x0, 0x0, 0xc2bc,
	0xc2bd, 0xc2be, 0xe2859b, 0xe2859c, 0xe2859d, 0xe2859e, 0xe28593, 0xe28594,
	0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0xefac80, 0xefac81,
	0xefac82, 0xefac83, 0xefac84, 0xe2828d, 0x0, 0xe2828e, 0xef9bb6, 0xef9ba5,
	0xef9da0, 0xef9da1, 0xef9da2, 0xef9da3, 0xef9da4, 0xef9da5, 0xef9da6, 0xef9da7,
	0xef9da8, 0xef9da9, 0xef9daa, 0xef9dab, 0xef9dac, 0xef9dad, 0xef9dae, 0xef9daf,
	0xef9db0, 0xef9db1, 0xef9db2, 0xef9db3, 0xef9db4, 0xef9db5, 0xef9db6, 0xef9db7,
	0xef9db8, 0xef9db9, 0xef9dba, 0xe282a1, 0xef9b9c, 0xef9b9d, 0xef9bbe, 0x0,
	0x0, 0xef9ba9, 0xef9ba0, 0x0, 0x0, 0x0, 0x0, 0xef9fa1,
	0xef9fa0, 0xef9fa2, 0xef9fa4, 0xef9fa3, 0xef9fa5, 0xef9fa7, 0xef9fa9, 0xef9fa8,
	0xef9faa, 0xef9fab, 0xef9fad, 0xef9fac, 0xef9fae, 0xef9faf, 0xef9fb1, 0xef9fb3,
	0xef9fb2, 0xef9fb4, 0xef9fb6, 0xef9fb5, 0xef9fba, 0xef9fb9, 0xef9fbb, 0xef9fbc,
	0x0, 0xe281b8, 0xe28284, 0xe28283, 0xe28286, 0xe28288, 0xe28287, 0xef9bbd,
	0x0, 0xef9b9f, 0xe28282, 0x0, 0xef9ea8, 0x0, 0xef9bb5, 0xef9bb0,
	0xe28285, 0x0, 0xef9ba1, 0xef9ba7, 0xef9fbd, 0x0, 0xef9ba3, 0x0,
	0x0, 0xef9fbe, 0x0, 0xe28289, 0xe28280, 0xef9bbf, 0xef9fa6, 0xef9fb8,
	0xef9ebf, 0xe28281, 0xef9bb9, 0x0, 0x0, 0x0, 0x0, 0x0,
	0x0, 0xef9eb8, 0x0, 0x0, 0x0, 0x0, 0x0, 0xef9bba,
	0xe28092, 0xef9ba6, 0x0, 0x0, 0x0, 0x0, 0xef9ea1, 0x0,
	0xef9fbf, 0x0, 0xc2b9, 0xc2b2, 0xc2b3, 0xe281b4, 0xe281b5, 0xe281b6,
	0xe281b7, 0xe281b9, 0xe281b0, 0x0, 0xef9bac, 0xef9bb1, 0xef9bb3, 0x0,
	0x0, 0xef9bad, 0xef9bb2, 0xef9bab, 0x0, 0x0, 0x0, 0x0,
	0x0, 0xef9bae, 0xef9bbb, 0xef9bb4, 0xef9eaf, 0xef9baa, 0xe281bf, 0xef9baf,
	0xef9ba2, 0xef9ba8, 0xef9bb7, 0xef9bbc, 0x0, 0x0, 0x0, 0x0
};

const static unsigned int StandardEncodingUtf8[256] =
{
	0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
	0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
	0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
	0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
	0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0xe28099,
	0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f,
	0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37,
	0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f,
	0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47,
	0x48, 0x49, 0x4a, 0x4b, 0x4c, 0x4d, 0x4e, 0x4f,
	0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57,
	0x58, 0x59, 0x5a, 0x5b, 0x5c, 0x5d, 0x5e, 0x5f,
	0x60, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67,
	0x68, 0x69, 0x6a, 0x6b, 0x6c, 0x6d, 0x6e, 0x6f,
	0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77,
	0x78, 0x79, 0x7a, 0x7b, 0x7c, 0x7d, 0x7e, 0x0,
	0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
	0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
	0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
	0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
	0x0, 0xc2a1, 0xc2a2, 0xc2a3, 0xe28184, 0xc2a5, 0xc692, 0xc2a7,
	0xc2a4, 0x27, 0xe2809c, 0xc2ab, 0xe280b9, 0xe280ba, 0xefac81, 0xefac82,
	0x0, 0xe28093, 0xe280a0, 0xe280a1, 0xc2b7, 0x0, 0xc2b6, 0xe280a2,
	0xe2809a, 0xe2809e, 0xe2809d, 0xc2bb, 0xe280a6, 0xe280b0, 0x0, 0xc2bf,
	0x0, 0x60, 0xc2b4, 0xcb86, 0xcb9c, 0xc2af, 0xcb98, 0xcb99,
	0xc2a8, 0x0, 0xcb9a, 0xc2b8, 0xcb9d, 0xcb9b, 0xcb87, 0xe28094,
	0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
	0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
	0xc386, 0x0, 0xc2aa, 0x0, 0x0, 0x0, 0x0, 0xc581,
	0xc398, 0xc592, 0xc2ba, 0x0, 0x0, 0x0, 0x0, 0x0,
	0xc3a6, 0x0, 0x0, 0x0, 0xc4b1, 0x0, 0x0, 0xc582,
	0xc3b8, 0xc593, 0xc39f, 0x0, 0x0, 0x0, 0x0, 0x0
};

const static unsigned int SymbolEncodingUtf8[256] =
{
	0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
	0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
	0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
	0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
	0x20, 0x21, 0xe28880, 0x23, 0xe28883, 0x25, 0x26, 0xe2888b,
	0x28, 0x29, 0xe28897, 0x2b, 0x2c, 0xe28892, 0x2e, 0x2f,
	0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37,
	0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f,
	0xe28985, 0xce91, 0xce92, 0xcea7, 0xce94, 0xce95, 0xcea6, 0xce93,
	0xce97, 0xce99, 0xcf91, 0xce9a, 0xce9b, 0xce9c, 0xce9d, 0xce9f,
	0xcea0, 0xce98, 0xcea1, 0xcea3, 0xcea4, 0xcea5, 0xcf82, 0xcea9,
	0xce9e, 0xcea8, 0xce96, 0x5b, 0xe288b4, 0x5d, 0xe28aa5, 0x5f,
	0xefa3a5, 0xceb1, 0xceb2, 0xcf87, 0xceb4, 0xceb5, 0xcf86, 0xceb3,
	0xceb7, 0xceb9, 0xcf95, 0xceba, 0xcebb, 0xcebc, 0xcebd, 0xcebf,
	0xcf80, 0xceb8, 0xcf81, 0xcf83, 0xcf84, 0xcf85, 0xcf96, 0xcf89,
	0xcebe, 0xcf88, 0xceb6, 0x7b, 0x7c, 0x7d, 0xe288bc, 0x0,
	0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
	0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
	0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
	0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
	0xe282ac, 0xcf92, 0xe280b2, 0xe289a4, 0xe28184, 0xe2889e, 0xc692, 0xe299a3,
	0xe299a6, 0xe299a5, 0xe299a0, 0xe28694, 0xe28690, 0xe28691, 0xe28692, 0xe28693,
	0xc2b0, 0xc2b1, 0xe280b3, 0xe289a5, 0xc397, 0xe2889d, 0xe28882, 0xe280a2,
	0xc3b7, 0xe289a0, 0xe289a1, 0xe28988, 0xe280a6, 0xefa3a6, 0xefa3a7, 0xe286b5,
	0xe284b5, 0xe28491, 0xe2849c, 0xe28498, 0xe28a97, 0xe28a95, 0xe28885, 0xe288a9,
	0xe288aa, 0xe28a83, 0xe28a87, 0xe28a84, 0xe28a82, 0xe28a86, 0xe28888, 0xe28889,
	0xe288a0, 0xe28887, 0xef9b9a, 0xef9b99, 0xef9b9b, 0xe2888f, 0xe2889a, 0xe28b85,
	0xc2ac, 0xe288a7, 0xe288a8, 0xe28794, 0xe28790, 0xe28791, 0xe28792, 0xe28793,
	0xe2978a, 0xe28ca9, 0xefa3a8, 0xefa3a9, 0xefa3aa, 0xe28891, 0xefa3ab, 0xefa3ac,
	0xefa3ad, 0xefa3ae, 0xefa3af, 0xefa3b0, 0xefa3b1, 0xefa3b2, 0xefa3b3, 0xefa3b4,
	0x0, 0xe28caa, 0xe288ab, 0xe28ca0, 0xefa3b5, 0xe28ca1, 0xefa3b6, 0xefa3b7,
	0xefa3b8, 0xefa3b9, 0xefa3ba, 0xefa3bb, 0xefa3bc, 0xefa3bd, 0xefa3be, 0x0
};

const static unsigned int ZapfDingbatsEncodingUtf8[256] =
{
	0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
	0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
	0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
	0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
	0x20, 0xe29c81, 0xe29c82, 0xe29c83, 0xe29c84, 0xe2988e, 0xe29c86, 0xe29c87,
	0xe29c88, 0xe29c89, 0xe2989b, 0xe2989e, 0xe29c8c, 0xe29c8d, 0xe29c8e, 0xe29c8f,
	0xe29c90, 0xe29c91, 0xe29c92, 0xe29c93, 0xe29c94, 0xe29c95, 0xe29c96, 0xe29c97,
	0xe29c98, 0xe29c99, 0xe29c9a, 0xe29c9b, 0xe29c9c, 0xe29c9d, 0xe29c9e, 0xe29c9f,
	0xe29ca0, 0xe29ca1, 0xe29ca2, 0xe29ca3, 0xe29ca4, 0xe29ca5, 0xe29ca6, 0xe29ca7,
	0xe29885, 0xe29ca9, 0xe29caa, 0xe29cab, 0xe29cac, 0xe29cad, 0xe29cae, 0xe29caf,
	0xe29cb0, 0xe29cb1, 0xe29cb2, 0xe29cb3, 0xe29cb4, 0xe29cb5, 0xe29cb6, 0xe29cb7,
	0xe29cb8, 0xe29cb9, 0xe29cba, 0xe29cbb, 0xe29cbc, 0xe29cbd, 0xe29cbe, 0xe29cbf,
	0xe29d80, 0xe29d81, 0xe29d82, 0xe29d83, 0xe29d84, 0xe29d85, 0xe29d86, 0xe29d87,
	0xe29d88, 0xe29d89, 0xe29d8a, 0xe29d8b, 0xe2978f, 0xe29d8d, 0xe296a0, 0xe29d8f,
	0xe29d90, 0xe29d91, 0xe29d92, 0xe296b2, 0xe296bc, 0xe29786, 0xe29d96, 0xe29797,
	0xe29d98, 0xe29d99, 0xe29d9a, 0xe29d9b, 0xe29d9c, 0xe29d9d, 0xe29d9e, 0x0,
	0xefa397, 0xefa398, 0xefa399, 0xefa39a, 0xefa39b, 0xefa39c, 0xefa39d, 0xefa39e,
	0xefa39f, 0xefa3a0, 0xefa3a1, 0xefa3a2, 0xefa3a3, 0xefa3a4, 0x0, 0x0,
	0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
	0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
	0x0, 0xe29da1, 0xe29da2, 0xe29da3, 0xe29da4, 0xe29da5, 0xe29da6, 0xe29da7,
	0xe299a3, 0xe299a6, 0xe299a5, 0xe299a0, 0xe291a0, 0xe291a1, 0xe291a2, 0xe291a3,
	0xe291a4, 0xe291a5, 0xe291a6, 0xe291a7, 0xe291a8, 0xe291a9, 0xe29db6, 0xe29db7,
	0xe29db8, 0xe29db9, 0xe29dba, 0xe29dbb, 0xe29dbc, 0xe29dbd, 0xe29dbe, 0xe29dbf,
	0xe29e80, 0xe29e81, 0xe29e82, 0xe29e83, 0xe29e84, 0xe29e85, 0xe29e86, 0xe29e87,
	0xe29e88, 0xe29e89, 0xe29e8a, 0xe29e8b, 0xe29e8c, 0xe29e8d, 0xe29e8e, 0xe29e8f,
	0xe29e90, 0xe29e91, 0xe29e92, 0xe29e93, 0xe29e94, 0xe28692, 0xe28694, 0xe28695,
	0xe29e98, 0xe29e99, 0xe29e9a, 0xe29e9b, 0xe29e9c, 0xe29e9d, 0xe29e9e, 0xe29e9f,
	0xe29ea0, 0xe29ea1, 0xe29ea2, 0xe29ea3, 0xe29ea4, 0xe29ea5, 0xe29ea6, 0xe29ea7,
	0xe29ea8, 0xe29ea9, 0xe29eaa, 0xe29eab, 0xe29eac, 0xe29ead, 0xe29eae, 0xe29eaf,
	0x0, 0xe29eb1, 0xe29eb2, 0xe29eb3, 0xe29eb4, 0xe29eb5, 0xe29eb6, 0xe29eb7,
	0xe29eb8, 0xe29eb9, 0xe29eba, 0xe29ebb, 0xe29ebc, 0xe29ebd, 0xe29ebe, 0x0
};

log_record_stream& operator<<(log_record_stream& s, const PoDoFo::PdfString& str)
{
	if (str.GetState() == PoDoFo::PdfStringState::RawBuffer)
		s << docwire_log_streamable_obj(str, str.IsHex(), str.GetState(), str.GetRawData());
	else
		s << docwire_log_streamable_obj(str, str.IsHex(), str.GetState(), str.GetString());
	return s;
}

log_record_stream& operator<<(log_record_stream& s, const PoDoFo::PdfName& n)
{
	s << docwire_log_streamable_obj(n, n.GetString());
	return s;
}

log_record_stream& operator<<(log_record_stream& s, const PoDoFo::PdfError& e)
{
	s << docwire_log_streamable_obj(e, e.what());
	return s;
}

log_record_stream& operator<<(log_record_stream& s, const PoDoFo::PdfEncoding& e)
{
	s << docwire_log_streamable_obj(e, e.IsNull(), e.HasCIDMapping(), e.IsSimpleEncoding(), e.HasParsedLimits(), e.IsDynamicEncoding(), e.GetId(), /*GetLimits(),*/ e.HasValidToUnicodeMap());
	return s;
}

log_record_stream& operator<<(log_record_stream& s, const PoDoFo::PdfFont& f)
{
	s << docwire_log_streamable_obj(f, f.GetType(), f.SupportsSubsetting(), f.IsStandard14Font(), f.IsCIDKeyed(), f.IsObjectLoaded(), f.IsSubsettingEnabled(), f.IsEmbeddingEnabled(), f.GetSubsetPrefix(), /*f.GetIdentifier(),*/ f.GetEncoding(), f.GetMetrics(), f.GetName()/*, f.GetUsedGIDs()*//*, f.GetDescendantFontObject()*/);
	return s;
}

log_record_stream& operator<<(log_record_stream& s, const PoDoFo::PdfFontMetrics& m)
{
	s << docwire_log_streamable_obj(m, /*GetGlyphCount(),*/ m.HasUnicodeMapping(), m.GetLineSpacing(), m.GetUnderlineThickness(), m.GetUnderlinePosition(), m.GetStrikeThroughPosition(), m.GetStrikeThroughThickness(), m.GetFontFileType(), m.HasFontFileData(), m.GetFontNameSafe(), m.GetBaseFontNameSafe(), /*GetBaseFontName(), GetFontName(),*/ /*GetFontNameRaw(),*/ m.GetFontFamilyName(), m.GetFontStretch(), m.GetWeight(), /*GetWeightRaw(),*/ /*m.GetFlags(),*/ m.GetItalicAngle(), m.GetAscent(), m.GetDescent(), m.GetLeading(), /*GetLeadingRaw(),*/ m.GetCapHeight(), m.GetXHeight(), /*GetXHeightRaw(),*/ m.GetStemV(), m.GetStemH(), /*GetStemHRaw(),*/ m.GetAvgWidth(), /*GetAvgWidthRaw(),*/ m.GetMaxWidth(), /*GetMaxWidthRaw(),*/ m.GetDefaultWidth(), /*GetDefaultWidthRaw(),*/ /*m.GetStyle(),*/ m.IsStandard14FontMetrics(), /*GetMatrix(),*/ m.IsType1Kind(), m.IsTrueTypeKind(), m.IsPdfSymbolic(), /*IsPdfNonSymbolic(),*/ m.GetFilePath()/*, GetFaceIndex()*/);
	return s;
}

log_record_stream& operator<<(log_record_stream& s, const PoDoFo::PdfFontType& t)
{
	std::string stringified_value;
	switch (t)
	{
		case PoDoFo::PdfFontType::Unknown: stringified_value = "Unknown"; break;
		case PoDoFo::PdfFontType::Type1: stringified_value = "Type1"; break;
		case PoDoFo::PdfFontType::Type3: stringified_value = "Type3"; break;
		case PoDoFo::PdfFontType::TrueType: stringified_value = "TrueType"; break;
		case PoDoFo::PdfFontType::CIDType1: stringified_value = "CIDType1"; break;
		case PoDoFo::PdfFontType::CIDTrueType: stringified_value = "CIDTrueType"; break;
		default: stringified_value = "!incorrect!"; break;
	}
	s << docwire_log_streamable_obj(t, stringified_value);
	return s;
}

log_record_stream& operator<<(log_record_stream& s, const PoDoFo::PdfFontFileType& t)
{
	std::string stringified_value;
	switch (t)
	{
		case PoDoFo::PdfFontFileType::Unknown: stringified_value = "Unknown"; break;
		case PoDoFo::PdfFontFileType::Type1: stringified_value = "Type1"; break;
		case PoDoFo::PdfFontFileType::Type1CCF: stringified_value = "Type1CCF"; break;
		case PoDoFo::PdfFontFileType::CIDType1: stringified_value = "CIDType1"; break;
		case PoDoFo::PdfFontFileType::Type3: stringified_value = "Type3"; break;
		case PoDoFo::PdfFontFileType::TrueType: stringified_value = "TrueType"; break;
		case PoDoFo::PdfFontFileType::OpenType: stringified_value = "OpenType"; break;
		default: stringified_value = "!incorrect!"; break;
	}
	s << docwire_log_streamable_obj(t, stringified_value);
	return s;
}

log_record_stream& operator<<(log_record_stream& s, const PoDoFo::PdfFontStretch& st)
{
	std::string stringified_value;
	switch (st)
	{
		case PoDoFo::PdfFontStretch::Unknown: stringified_value = "Unknown"; break;
		case PoDoFo::PdfFontStretch::UltraCondensed: stringified_value = "UltraCondensed"; break;
		case PoDoFo::PdfFontStretch::ExtraCondensed: stringified_value = "ExtraCondensed"; break;
		case PoDoFo::PdfFontStretch::Condensed: stringified_value = "Condensed"; break;
		case PoDoFo::PdfFontStretch::SemiCondensed: stringified_value = "SemiCondensed"; break;
		case PoDoFo::PdfFontStretch::Normal: stringified_value = "Normal"; break;
		case PoDoFo::PdfFontStretch::SemiExpanded: stringified_value = "SemiExpanded"; break;
		case PoDoFo::PdfFontStretch::Expanded: stringified_value = "Expanded"; break;
		case PoDoFo::PdfFontStretch::ExtraExpanded: stringified_value = "ExtraExpanded"; break;
		case PoDoFo::PdfFontStretch::UltraExpanded: stringified_value = "UltraExpanded"; break;
		default: stringified_value = "!incorrect!"; break;
	}
	s << docwire_log_streamable_obj(st, stringified_value);
	return s;
}

std::ostream& operator<<(std::ostream& s, const PoDoFo::PdfObject& o);

log_record_stream& operator<<(log_record_stream& s, const PoDoFo::PdfVariant& v)
{
	s << begin_complex() << docwire_log_streamable_type_of(v) << docwire_log_streamable_var(v.GetDataTypeString());
	if (v.IsString())
		s << docwire_log_streamable_var(v.GetString());
	else if (v.IsNumber())
		s << docwire_log_streamable_var(v.GetNumber());
	else if (v.IsArray())
		s << docwire_log_streamable_var(v.GetArray());
	else
		s << docwire_log_streamable_var(v.ToString());
	s << end_complex();
	return s;
}

log_record_stream& operator<<(log_record_stream& s, const PoDoFo::PdfObject& o)
{
	s << begin_complex() << docwire_log_streamable_type_of(o) << docwire_log_streamable_var(o.GetDataTypeString());
	if (o.IsString())
		s << docwire_log_streamable_var(o.GetString());
	else if (o.IsNumber())
		s << docwire_log_streamable_var(o.GetNumber());
	else if (o.IsArray())
		s << docwire_log_streamable_var(o.GetArray());
	else
		s << docwire_log_streamable_var(o.ToString());
	s << end_complex();
	return s;
}

log_record_stream& operator<<(log_record_stream& s, const PoDoFo::PdfStringState& st)
{
	std::string stringified_value;
	switch (st)
	{
		case PoDoFo::PdfStringState::RawBuffer: stringified_value = "RawBuffer"; break;
		case PoDoFo::PdfStringState::Ascii: stringified_value = "Ascii"; break;
		case PoDoFo::PdfStringState::PdfDocEncoding: stringified_value = "PdfDocEncoding"; break;
		case PoDoFo::PdfStringState::Unicode: stringified_value = "Unicode"; break;
		default: stringified_value = "!incorrect!"; break;
	}
	s << docwire_log_streamable_obj(st, stringified_value);
	return s;
}

log_record_stream& operator<<(log_record_stream& s, const PoDoFo::PdfContentType& t)
{
	std::string stringified_value;
	switch (t)
	{
		case PoDoFo::PdfContentType::Unknown: stringified_value = "Unknown"; break;
		case PoDoFo::PdfContentType::Operator: stringified_value = "Operator"; break;
		case PoDoFo::PdfContentType::ImageDictionary: stringified_value = "ImageDictionary"; break;
		case PoDoFo::PdfContentType::ImageData: stringified_value = "ImageData"; break;
		case PoDoFo::PdfContentType::DoXObject: stringified_value = "DoXObject"; break;
		case PoDoFo::PdfContentType::EndXObjectForm: stringified_value = "EndXObjectForm"; break;
		case PoDoFo::PdfContentType::UnexpectedKeyword: stringified_value = "UnexpectedKeyword"; break;
		default: stringified_value = "!incorrect!"; break;
	}
	s << docwire_log_streamable_obj(t, stringified_value);
	return s;
}

log_record_stream& operator<<(log_record_stream& s, const PoDoFo::PdfContent& c)
{
	s << docwire_log_streamable_obj(c, c.Type, c.Keyword, c.Stack);
	return s;
}

namespace
{
	std::mutex podofo_freetype_mutex;
} // anonymous namespace

template<>
struct pimpl_impl<PDFParser> : with_pimpl_owner<PDFParser>
{
	pimpl_impl(PDFParser& owner) : with_pimpl_owner{owner} {}
	PoDoFo::PdfMemDocument m_pdf_document;

	class PredefinedSimpleEncodings : public std::map<std::string, unsigned int*>
	{
		public:
			PredefinedSimpleEncodings()
			{
				insert(std::pair<std::string, unsigned int*>("MacRomanEncoding", (unsigned int*)MacRomanEncodingUtf8));
				insert(std::pair<std::string, unsigned int*>("WinAnsiEncoding", (unsigned int*)WinAnsiEncodingUtf8));
				insert(std::pair<std::string, unsigned int*>("MacExpertEncoding", (unsigned int*)MacExpertEncodingUtf8));
				insert(std::pair<std::string, unsigned int*>("StandardEncoding", (unsigned int*)StandardEncodingUtf8));
				insert(std::pair<std::string, unsigned int*>("SymbolEncoding",(unsigned int*)SymbolEncodingUtf8));
				insert(std::pair<std::string, unsigned int*>("ZapfDingbatsEncoding", (unsigned int*)ZapfDingbatsEncodingUtf8));
				insert(std::pair<std::string, unsigned int*>("PdfDocEncoding", (unsigned int*)PdfDocEncodingUtf8));
			}
	};

	static PredefinedSimpleEncodings m_pdf_predefined_simple_encodings;

	class CIDToUnicode : public std::map<std::string, std::string>
	{
		public:
			CIDToUnicode()
			{
				insert(std::pair<std::string, std::string>("GB-EUC-H", "Adobe-GB1-UCS2"));
				insert(std::pair<std::string, std::string>("GB-EUC-V", "Adobe-GB1-UCS2"));
				insert(std::pair<std::string, std::string>("GBpc-EUC-H", "Adobe-GB1-UCS2"));
				insert(std::pair<std::string, std::string>("GBpc-EUC-V", "Adobe-GB1-UCS2"));
				insert(std::pair<std::string, std::string>("GBK-EUC-H", "Adobe-GB1-UCS2"));
				insert(std::pair<std::string, std::string>("GBK-EUC-V", "Adobe-GB1-UCS2"));
				insert(std::pair<std::string, std::string>("GBK2K-H", "Adobe-GB1-UCS2"));
				insert(std::pair<std::string, std::string>("GBK2K-V", "Adobe-GB1-UCS2"));
				insert(std::pair<std::string, std::string>("UniGB-UCS2-H", "Adobe-GB1-UCS2"));
				insert(std::pair<std::string, std::string>("UniGB-UCS2-V", "Adobe-GB1-UCS2"));
				insert(std::pair<std::string, std::string>("UniGB-UTF16-H", "Adobe-GB1-UCS2"));
				insert(std::pair<std::string, std::string>("UniGB-UTF16-V", "Adobe-GB1-UCS2"));
				insert(std::pair<std::string, std::string>("B5pc-H", "Adobe-CNS1-UCS2"));
				insert(std::pair<std::string, std::string>("B5pc-V", "Adobe-CNS1-UCS2"));
				insert(std::pair<std::string, std::string>("HKscs-B5-H", "Adobe-CNS1-UCS2"));
				insert(std::pair<std::string, std::string>("HKscs-B5-V", "Adobe-CNS1-UCS2"));
				insert(std::pair<std::string, std::string>("ETen-B5-H", "Adobe-CNS1-UCS2"));
				insert(std::pair<std::string, std::string>("ETen-B5-V", "Adobe-CNS1-UCS2"));
				insert(std::pair<std::string, std::string>("ETenms-B5-H", "Adobe-CNS1-UCS2"));
				insert(std::pair<std::string, std::string>("ETenms-B5-V", "Adobe-CNS1-UCS2"));
				insert(std::pair<std::string, std::string>("CNS-EUC-H", "Adobe-CNS1-UCS2"));
				insert(std::pair<std::string, std::string>("CNS-EUC-V", "Adobe-CNS1-UCS2"));
				insert(std::pair<std::string, std::string>("UniCNS-UCS2-H", "Adobe-CNS1-UCS2"));
				insert(std::pair<std::string, std::string>("UniCNS-UCS2-V", "Adobe-CNS1-UCS2"));
				insert(std::pair<std::string, std::string>("UniCNS-UTF16-H", "Adobe-CNS1-UCS2"));
				insert(std::pair<std::string, std::string>("UniCNS-UTF16-V", "Adobe-CNS1-UCS2"));
				insert(std::pair<std::string, std::string>("83pv-RKSJ-H", "Adobe-Japan1-UCS2"));
				insert(std::pair<std::string, std::string>("90ms-RKSJ-H", "Adobe-Japan1-UCS2"));
				insert(std::pair<std::string, std::string>("90ms-RKSJ-V", "Adobe-Japan1-UCS2"));
				insert(std::pair<std::string, std::string>("90msp-RKSJ-H", "Adobe-Japan1-UCS2"));
				insert(std::pair<std::string, std::string>("90msp-RKSJ-V", "Adobe-Japan1-UCS2"));
				insert(std::pair<std::string, std::string>("90pv-RKSJ-H", "Adobe-Japan1-UCS2"));
				insert(std::pair<std::string, std::string>("Add-RKSJ-H", "Adobe-Japan1-UCS2"));
				insert(std::pair<std::string, std::string>("Add-RKSJ-V", "Adobe-Japan1-UCS2"));
				insert(std::pair<std::string, std::string>("EUC-H", "Adobe-Japan1-UCS2"));
				insert(std::pair<std::string, std::string>("EUC-V", "Adobe-Japan1-UCS2"));
				insert(std::pair<std::string, std::string>("Ext-RKSJ-H", "Adobe-Japan1-UCS2"));
				insert(std::pair<std::string, std::string>("Ext-RKSJ-V", "Adobe-Japan1-UCS2"));
				insert(std::pair<std::string, std::string>("H", "Adobe-Japan1-UCS2"));
				insert(std::pair<std::string, std::string>("V", "Adobe-Japan1-UCS2"));
				insert(std::pair<std::string, std::string>("UniJIS-UCS2-H", "Adobe-Japan1-UCS2"));
				insert(std::pair<std::string, std::string>("UniJIS-UCS2-V", "Adobe-Japan1-UCS2"));
				insert(std::pair<std::string, std::string>("UniJIS-UCS2-HW-H", "Adobe-Japan1-UCS2"));
				insert(std::pair<std::string, std::string>("UniJIS-UCS2-HW-V", "Adobe-Japan1-UCS2"));
				insert(std::pair<std::string, std::string>("UniJIS-UTF16-H", "Adobe-Japan1-UCS2"));
				insert(std::pair<std::string, std::string>("UniJIS-UTF16-V", "Adobe-Japan1-UCS2"));
				insert(std::pair<std::string, std::string>("KSC-EUC-H", "Adobe-Korea1-UCS2"));
				insert(std::pair<std::string, std::string>("KSC-EUC-V", "Adobe-Korea1-UCS2"));
				insert(std::pair<std::string, std::string>("KSCms-UHC-H", "Adobe-Korea1-UCS2"));
				insert(std::pair<std::string, std::string>("KSCms-UHC-V", "Adobe-Korea1-UCS2"));
				insert(std::pair<std::string, std::string>("KSCms-UHC-HW-H", "Adobe-Korea1-UCS2"));
				insert(std::pair<std::string, std::string>("KSCms-UHC-HW-V", "Adobe-Korea1-UCS2"));
				insert(std::pair<std::string, std::string>("KSCpc-EUC-H", "Adobe-Korea1-UCS2"));
				insert(std::pair<std::string, std::string>("UniKS-UCS2-H", "Adobe-Korea1-UCS2"));
				insert(std::pair<std::string, std::string>("UniKS-UCS2-V", "Adobe-Korea1-UCS2"));
				insert(std::pair<std::string, std::string>("UniKS-UTF16-H", "Adobe-Korea1-UCS2"));
				insert(std::pair<std::string, std::string>("UniKS-UTF16-V", "Adobe-Korea1-UCS2"));
			}
	};

	static CIDToUnicode m_pdf_cid_to_unicode;

	class CharacterNames : public std::map<std::string, unsigned int>
	{
		public:
			CharacterNames()
			{
				// warning TODO: List is incomplete. Can we find something better? Full list in PDFMiner counts about... 2000 names
				insert(std::pair<std::string, unsigned int>(".notdef", 0x0));
				insert(std::pair<std::string, unsigned int>("A", 0x41));
				insert(std::pair<std::string, unsigned int>("AE", 0xC386));
				insert(std::pair<std::string, unsigned int>("Aacute", 0xC381));
				insert(std::pair<std::string, unsigned int>("Acircumflex", 0xC382));
				insert(std::pair<std::string, unsigned int>("Adieresis", 0xC384));
				insert(std::pair<std::string, unsigned int>("Agrave", 0xC380));
				insert(std::pair<std::string, unsigned int>("Aring", 0xC385));
				insert(std::pair<std::string, unsigned int>("Aogonek", 0xC484));
				insert(std::pair<std::string, unsigned int>("Atilde", 0xC383));
				insert(std::pair<std::string, unsigned int>("B", 0x42));
				insert(std::pair<std::string, unsigned int>("C", 0x43));
				insert(std::pair<std::string, unsigned int>("Cacute", 0xC486));
				insert(std::pair<std::string, unsigned int>("Ccedilla", 0xC387));
				insert(std::pair<std::string, unsigned int>("D", 0x44));
				insert(std::pair<std::string, unsigned int>("E", 0x45));
				insert(std::pair<std::string, unsigned int>("Eacute", 0xC389));
				insert(std::pair<std::string, unsigned int>("Ecircumflex", 0xC38A));
				insert(std::pair<std::string, unsigned int>("Edieresis", 0xC38B));
				insert(std::pair<std::string, unsigned int>("Egrave", 0xC388));
				insert(std::pair<std::string, unsigned int>("Eogonek", 0xC498));
				insert(std::pair<std::string, unsigned int>("Eth", 0xC390));
				insert(std::pair<std::string, unsigned int>("Euro", 0xE282AC));
				insert(std::pair<std::string, unsigned int>("F", 0x46));
				insert(std::pair<std::string, unsigned int>("G", 0x47));
				insert(std::pair<std::string, unsigned int>("H", 0x48));
				insert(std::pair<std::string, unsigned int>("I", 0x49));
				insert(std::pair<std::string, unsigned int>("Iacute", 0xC38D));
				insert(std::pair<std::string, unsigned int>("Icircumflex", 0xC38E));
				insert(std::pair<std::string, unsigned int>("Idiereses", 0xC38F));
				insert(std::pair<std::string, unsigned int>("Igrave", 0xC38C));
				insert(std::pair<std::string, unsigned int>("J", 0x4A));
				insert(std::pair<std::string, unsigned int>("K", 0x4B));
				insert(std::pair<std::string, unsigned int>("L", 0x4C));
				insert(std::pair<std::string, unsigned int>("Lslash", 0xC581));
				insert(std::pair<std::string, unsigned int>("M", 0x4D));
				insert(std::pair<std::string, unsigned int>("N", 0x4E));
				insert(std::pair<std::string, unsigned int>("Nacute", 0xC583));
				insert(std::pair<std::string, unsigned int>("Ntilde", 0xC391));
				insert(std::pair<std::string, unsigned int>("O", 0x4F));
				insert(std::pair<std::string, unsigned int>("OE", 0xC592));
				insert(std::pair<std::string, unsigned int>("Oacute", 0xC393));
				insert(std::pair<std::string, unsigned int>("Ocircumflex", 0xC394));
				insert(std::pair<std::string, unsigned int>("Odieresis", 0xC396));
				insert(std::pair<std::string, unsigned int>("Ograve", 0xC392));
				insert(std::pair<std::string, unsigned int>("Oslash", 0xC398));
				insert(std::pair<std::string, unsigned int>("Otilde", 0xC395));
				insert(std::pair<std::string, unsigned int>("P", 0x50));
				insert(std::pair<std::string, unsigned int>("Q", 0x51));
				insert(std::pair<std::string, unsigned int>("R", 0x52));
				insert(std::pair<std::string, unsigned int>("S", 0x53));
				insert(std::pair<std::string, unsigned int>("Sacute", 0xC59A));
				insert(std::pair<std::string, unsigned int>("Scaron", 0xC5A0));
				insert(std::pair<std::string, unsigned int>("T", 0x54));
				insert(std::pair<std::string, unsigned int>("Thorn", 0xC3BE));
				insert(std::pair<std::string, unsigned int>("U", 0x55));
				insert(std::pair<std::string, unsigned int>("Uacute", 0xC39A));
				insert(std::pair<std::string, unsigned int>("Ucircumflex", 0xC39B));
				insert(std::pair<std::string, unsigned int>("Udieresis", 0xC39C));
				insert(std::pair<std::string, unsigned int>("Ugrave", 0xC399));
				insert(std::pair<std::string, unsigned int>("V", 0x56));
				insert(std::pair<std::string, unsigned int>("W", 0x57));
				insert(std::pair<std::string, unsigned int>("X", 0x58));
				insert(std::pair<std::string, unsigned int>("Y", 0x59));
				insert(std::pair<std::string, unsigned int>("Yacute", 0xC39D));
				insert(std::pair<std::string, unsigned int>("Ydieresis", 0xC5B8));
				insert(std::pair<std::string, unsigned int>("Z", 0x5A));
				insert(std::pair<std::string, unsigned int>("Zacute", 0xC5B9));
				insert(std::pair<std::string, unsigned int>("Zcaron", 0xC5BD));
				insert(std::pair<std::string, unsigned int>("Zdot", 0xC5BB));	//Im not sure about this one
				insert(std::pair<std::string, unsigned int>("a", 0x61));
				insert(std::pair<std::string, unsigned int>("aacute", 0xC3A1));
				insert(std::pair<std::string, unsigned int>("acircumflex", 0xC3A2));
				insert(std::pair<std::string, unsigned int>("acute", 0xC2B4));
				insert(std::pair<std::string, unsigned int>("adieresis", 0xC3A4));
				insert(std::pair<std::string, unsigned int>("ae", 0xC3A6));
				insert(std::pair<std::string, unsigned int>("agrave", 0xC3A0));
				insert(std::pair<std::string, unsigned int>("ampersand", 0x26));
				insert(std::pair<std::string, unsigned int>("aogonek", 0xC485));
				insert(std::pair<std::string, unsigned int>("aring", 0xC3A5));
				insert(std::pair<std::string, unsigned int>("asciicircum", 0xCB86));
				insert(std::pair<std::string, unsigned int>("asciitilde", 0xCB9C));
				insert(std::pair<std::string, unsigned int>("asterisk", 0x2A));
				insert(std::pair<std::string, unsigned int>("at", 0x40));
				insert(std::pair<std::string, unsigned int>("atilde", 0xC3A3));
				insert(std::pair<std::string, unsigned int>("b", 0x62));
				insert(std::pair<std::string, unsigned int>("backslash", 0x5C));
				insert(std::pair<std::string, unsigned int>("bar", 0x7C));
				insert(std::pair<std::string, unsigned int>("braceleft", 0x7B));
				insert(std::pair<std::string, unsigned int>("braceright", 0x7D));
				insert(std::pair<std::string, unsigned int>("bracketleft", 0x5B));
				insert(std::pair<std::string, unsigned int>("bracketright", 0x5D));
				insert(std::pair<std::string, unsigned int>("breve", 0xCB98));
				insert(std::pair<std::string, unsigned int>("brokenbar", 0xC2A6));
				insert(std::pair<std::string, unsigned int>("bullet", 0xE280A2));
				insert(std::pair<std::string, unsigned int>("c", 0x63));
				insert(std::pair<std::string, unsigned int>("caron", 0xCB87));
				insert(std::pair<std::string, unsigned int>("ccedilla", 0xC3A7));
				insert(std::pair<std::string, unsigned int>("cedilla", 0xC2B8));
				insert(std::pair<std::string, unsigned int>("cent", 0xC2A2));
				insert(std::pair<std::string, unsigned int>("circumflex", 0x5E));
				insert(std::pair<std::string, unsigned int>("cacute", 0xC487));
				insert(std::pair<std::string, unsigned int>("colon", 0x3A));
				insert(std::pair<std::string, unsigned int>("comma", 0x2C));
				insert(std::pair<std::string, unsigned int>("copyright", 0xC2A9));
				insert(std::pair<std::string, unsigned int>("currency", 0xC2A4));
				insert(std::pair<std::string, unsigned int>("d", 0x64));
				insert(std::pair<std::string, unsigned int>("dagger", 0xE280A0));
				insert(std::pair<std::string, unsigned int>("daggerdbl", 0xE280A1));
				insert(std::pair<std::string, unsigned int>("degree", 0xC2B0));
				insert(std::pair<std::string, unsigned int>("dieresis", 0xC2A8));
				insert(std::pair<std::string, unsigned int>("divide", 0xC3B7));
				insert(std::pair<std::string, unsigned int>("dollar", 0x24));
				insert(std::pair<std::string, unsigned int>("dotaccent", 0xCB99));
				insert(std::pair<std::string, unsigned int>("dotlessi", 0xC4B1));
				insert(std::pair<std::string, unsigned int>("e", 0x65));
				insert(std::pair<std::string, unsigned int>("eacute", 0xC3A9));
				insert(std::pair<std::string, unsigned int>("ecircumflex", 0xC3AA));
				insert(std::pair<std::string, unsigned int>("edieresis", 0xC3AB));
				insert(std::pair<std::string, unsigned int>("eogonek", 0xC499));
				insert(std::pair<std::string, unsigned int>("egrave", 0xC3A8));
				insert(std::pair<std::string, unsigned int>("eight", 0x38));
				insert(std::pair<std::string, unsigned int>("ellipsis", 0xE280A6));
				insert(std::pair<std::string, unsigned int>("emdash", 0xE28094));
				insert(std::pair<std::string, unsigned int>("endash", 0xE28093));
				insert(std::pair<std::string, unsigned int>("equal", 0x3D));
				insert(std::pair<std::string, unsigned int>("eth", 0xC3B0));
				insert(std::pair<std::string, unsigned int>("exclam", 0x21));
				insert(std::pair<std::string, unsigned int>("exclamdown", 0xC2A1));
				insert(std::pair<std::string, unsigned int>("f", 0x66));
				insert(std::pair<std::string, unsigned int>("fi", 0xEFAC81));
				insert(std::pair<std::string, unsigned int>("five", 0x35));
				insert(std::pair<std::string, unsigned int>("fl", 0xEFAC82));
				insert(std::pair<std::string, unsigned int>("florin", 0xC692));
				insert(std::pair<std::string, unsigned int>("four", 0x34));
				insert(std::pair<std::string, unsigned int>("fraction", 0xE281A4));
				insert(std::pair<std::string, unsigned int>("g", 0x67));
				insert(std::pair<std::string, unsigned int>("germandbls", 0xC39F));
				insert(std::pair<std::string, unsigned int>("grave", 0x60));
				insert(std::pair<std::string, unsigned int>("greater", 0x3E));
				insert(std::pair<std::string, unsigned int>("guillemotleft", 0xC2AB));
				insert(std::pair<std::string, unsigned int>("guillemotright", 0xC2BB));
				insert(std::pair<std::string, unsigned int>("guilsinglleft", 0xE280B9));
				insert(std::pair<std::string, unsigned int>("guilsinglright", 0xE280BA));
				insert(std::pair<std::string, unsigned int>("h", 0x68));
				insert(std::pair<std::string, unsigned int>("hungarumlaut", 0xCB9D));
				insert(std::pair<std::string, unsigned int>("hyphen", 0x2D));
				insert(std::pair<std::string, unsigned int>("i", 0x69));
				insert(std::pair<std::string, unsigned int>("iacute", 0xC3AD));
				insert(std::pair<std::string, unsigned int>("icircumflex", 0xC3AE));
				insert(std::pair<std::string, unsigned int>("idieresis", 0xC3AF));
				insert(std::pair<std::string, unsigned int>("igrave", 0xC3AC));
				insert(std::pair<std::string, unsigned int>("j", 0x6A));
				insert(std::pair<std::string, unsigned int>("k", 0x6B));
				insert(std::pair<std::string, unsigned int>("l", 0x6C));
				insert(std::pair<std::string, unsigned int>("less", 0x3C));
				insert(std::pair<std::string, unsigned int>("logicalnot", 0xC2AC));
				insert(std::pair<std::string, unsigned int>("lslash", 0xC582));
				insert(std::pair<std::string, unsigned int>("m", 0x6D));
				insert(std::pair<std::string, unsigned int>("macron", 0xC2AF));
				insert(std::pair<std::string, unsigned int>("minus", 0xE28892));
				insert(std::pair<std::string, unsigned int>("mu", 0xC2B5));
				insert(std::pair<std::string, unsigned int>("multiply", 0xC397));
				insert(std::pair<std::string, unsigned int>("n", 0x6E));
				insert(std::pair<std::string, unsigned int>("nine", 0x39));
				insert(std::pair<std::string, unsigned int>("nacute", 0xC584));
				insert(std::pair<std::string, unsigned int>("ntilde", 0xC3B1));
				insert(std::pair<std::string, unsigned int>("numbersign", 0x23));
				insert(std::pair<std::string, unsigned int>("o", 0x6F));
				insert(std::pair<std::string, unsigned int>("oacute", 0xC3B3));
				insert(std::pair<std::string, unsigned int>("ocircumflex", 0xC3B4));
				insert(std::pair<std::string, unsigned int>("odieresis", 0xC3B6));
				insert(std::pair<std::string, unsigned int>("oe", 0xC593));
				insert(std::pair<std::string, unsigned int>("ogonek", 0xCB9B));
				insert(std::pair<std::string, unsigned int>("ograve", 0xC3B2));
				insert(std::pair<std::string, unsigned int>("one", 0x31));
				insert(std::pair<std::string, unsigned int>("onehalf", 0xC2BD));
				insert(std::pair<std::string, unsigned int>("onequarter", 0xC2BC));
				insert(std::pair<std::string, unsigned int>("onesuperior", 0xC2B9));
				insert(std::pair<std::string, unsigned int>("ordfeminine", 0xC2AA));
				insert(std::pair<std::string, unsigned int>("ordmasculine", 0xC2BA));
				insert(std::pair<std::string, unsigned int>("oslash", 0xC3B8));
				insert(std::pair<std::string, unsigned int>("otilde", 0xC3B5));
				insert(std::pair<std::string, unsigned int>("p", 0x70));
				insert(std::pair<std::string, unsigned int>("paragraph", 0xC2B6));
				insert(std::pair<std::string, unsigned int>("parenleft", 0x28));
				insert(std::pair<std::string, unsigned int>("parenright", 0x29));
				insert(std::pair<std::string, unsigned int>("percent", 0x25));
				insert(std::pair<std::string, unsigned int>("period", 0x2E));
				insert(std::pair<std::string, unsigned int>("periodcentered", 0xC2B7));
				insert(std::pair<std::string, unsigned int>("perthousand", 0xE280B0));
				insert(std::pair<std::string, unsigned int>("plus", 0x2B));
				insert(std::pair<std::string, unsigned int>("plusminus", 0xC2B1));
				insert(std::pair<std::string, unsigned int>("q", 0x71));
				insert(std::pair<std::string, unsigned int>("question", 0x3F));
				insert(std::pair<std::string, unsigned int>("questiondown", 0xC2BF));
				insert(std::pair<std::string, unsigned int>("quotedbl", 0x22));
				insert(std::pair<std::string, unsigned int>("quotedblbase", 0xE2809E));
				insert(std::pair<std::string, unsigned int>("quotedblleft", 0xE2809C));
				insert(std::pair<std::string, unsigned int>("quotedblright", 0xE2809D));
				insert(std::pair<std::string, unsigned int>("quoteleft", 0xE28098));
				insert(std::pair<std::string, unsigned int>("quoteright", 0xE28099));
				insert(std::pair<std::string, unsigned int>("quotesinglbase", 0xE2809A));
				insert(std::pair<std::string, unsigned int>("quotesingle", 0x27));
				insert(std::pair<std::string, unsigned int>("r", 0x72));
				insert(std::pair<std::string, unsigned int>("registered", 0xC2AE));
				insert(std::pair<std::string, unsigned int>("rign", 0xCB9A));
				insert(std::pair<std::string, unsigned int>("s", 0x73));
				insert(std::pair<std::string, unsigned int>("sacute", 0xC59B));
				insert(std::pair<std::string, unsigned int>("scaron", 0xC5A1));
				insert(std::pair<std::string, unsigned int>("section", 0xC2A7));
				insert(std::pair<std::string, unsigned int>("semicolon", 0x3B));
				insert(std::pair<std::string, unsigned int>("seven", 0x37));
				insert(std::pair<std::string, unsigned int>("six", 0x36));
				insert(std::pair<std::string, unsigned int>("slash", 0x2F));
				insert(std::pair<std::string, unsigned int>("space", 0x20));
				insert(std::pair<std::string, unsigned int>("sterling", 0xC2A3));
				insert(std::pair<std::string, unsigned int>("t", 0x74));
				insert(std::pair<std::string, unsigned int>("thorn", 0xC39E));
				insert(std::pair<std::string, unsigned int>("three", 0x33));
				insert(std::pair<std::string, unsigned int>("threequarters", 0xC2BE));
				insert(std::pair<std::string, unsigned int>("threesuperior", 0xC2B3));
				insert(std::pair<std::string, unsigned int>("tilde", 0x7E));
				insert(std::pair<std::string, unsigned int>("trademark", 0xE284A2));
				insert(std::pair<std::string, unsigned int>("two", 0x32));
				insert(std::pair<std::string, unsigned int>("twosuperior", 0xC2B2));
				insert(std::pair<std::string, unsigned int>("u", 0x75));
				insert(std::pair<std::string, unsigned int>("uacute", 0xC3BA));
				insert(std::pair<std::string, unsigned int>("ucircumflex", 0xC3BB));
				insert(std::pair<std::string, unsigned int>("udieresis", 0xC3BC));
				insert(std::pair<std::string, unsigned int>("ugrave", 0xC3B9));
				insert(std::pair<std::string, unsigned int>("underscore", 0x5F));
				insert(std::pair<std::string, unsigned int>("v", 0x76));
				insert(std::pair<std::string, unsigned int>("w", 0x77));
				insert(std::pair<std::string, unsigned int>("x", 0x78));
				insert(std::pair<std::string, unsigned int>("y", 0x79));
				insert(std::pair<std::string, unsigned int>("yacute", 0xC3BD));
				insert(std::pair<std::string, unsigned int>("ydieresis", 0xC3BF));
				insert(std::pair<std::string, unsigned int>("yen", 0xC2A5));
				insert(std::pair<std::string, unsigned int>("z", 0x7A));
				insert(std::pair<std::string, unsigned int>("zacute", 0xC5BA));
				insert(std::pair<std::string, unsigned int>("zcaron", 0xC5BE));
				insert(std::pair<std::string, unsigned int>("zdot", 0xC5BC));	//not sure about this
				insert(std::pair<std::string, unsigned int>("zero", 0x30));
			}
	};

	static CharacterNames m_pdf_character_names;

	class PDFReader
	{
		public:
			enum PDFObjectTypes
			{
				dictionary,
				boolean,
				int_numeric,
				float_numeric,
				array,
				string,
				name,
				stream,
				null,
				//indirect object has structure:
				//A B obj
				//[object data of any kind (can be array, number, dictionary etc.)]
				//endobj
				//
				//where A is an index of this object, B is generation number
				indirect_object,
				//reference is a combination of two numbers and 'R' character like:
				//A B R.
				//A is an index of the object, B is generation number
				reference_call,
				operator_obj,
				unknown_obj
			};

			enum CompressionTypes
			{
				ascii_hex,
				lzw,
				rle,
				ascii_85,
				flat,
				crypt,
				unknown_compression
			};

			enum OperatorTypes
			{
				Tj,
				TJ,
				Td,
				TD,
				T_star,
				Tm,
				double_quote,
				quote,
				TL,
				BT,
				ET,
				Tf,
				TZ,
				cm,
				q,
				Q,
				Tc,
				Tw,
				Ts,
				// warning TODO: Add support for operator "Do".
				Do,
				usecmap,
				begincidrange,
				endcidrange,
				begincidchar,
				endcidchar,
				beginnotdefrange,
				endnotdefrange,
				beginnotdefchar,
				endnotdefchar,
				beginbfrange,
				endbfrange,
				beginbfchar,
				endbfchar,
				unknown_operator
			};

		private:
			class CompressionCodes : public std::map<std::string, CompressionTypes>
			{
				public:
					CompressionCodes()
					{
						insert(std::pair<std::string, CompressionTypes>("ASCIIHexDecode", ascii_hex));
						insert(std::pair<std::string, CompressionTypes>("LZWDecode", lzw));
						insert(std::pair<std::string, CompressionTypes>("RunLengthDecode", rle));
						insert(std::pair<std::string, CompressionTypes>("ASCII85Decode", ascii_85));
						insert(std::pair<std::string, CompressionTypes>("FlateDecode", flat));
						insert(std::pair<std::string, CompressionTypes>("Crypt", crypt));
					}
			};

			class OperatorCodes : public std::map<std::string, OperatorTypes>
			{
				public:
					OperatorCodes()
					{
						insert(std::pair<std::string, OperatorTypes>("Tj", Tj));
						insert(std::pair<std::string, OperatorTypes>("TJ", TJ));
						insert(std::pair<std::string, OperatorTypes>("Td", Td));
						insert(std::pair<std::string, OperatorTypes>("TD", TD));
						insert(std::pair<std::string, OperatorTypes>("T*", T_star));
						insert(std::pair<std::string, OperatorTypes>("Tm", Tm));
						insert(std::pair<std::string, OperatorTypes>("\"", double_quote));
						insert(std::pair<std::string, OperatorTypes>("\'", quote));
						insert(std::pair<std::string, OperatorTypes>("TL", TL));
						insert(std::pair<std::string, OperatorTypes>("BT", BT));
						insert(std::pair<std::string, OperatorTypes>("ET", ET));
						insert(std::pair<std::string, OperatorTypes>("Tf", Tf));
						insert(std::pair<std::string, OperatorTypes>("Do", Do));
						insert(std::pair<std::string, OperatorTypes>("Tz", TZ));
						insert(std::pair<std::string, OperatorTypes>("cm", cm));
						insert(std::pair<std::string, OperatorTypes>("q", q));
						insert(std::pair<std::string, OperatorTypes>("Q", Q));
						insert(std::pair<std::string, OperatorTypes>("Ts", Ts));
						insert(std::pair<std::string, OperatorTypes>("Tw", Tw));
						insert(std::pair<std::string, OperatorTypes>("Tc", Tc));
						insert(std::pair<std::string, OperatorTypes>("usecmap", usecmap));
						insert(std::pair<std::string, OperatorTypes>("begincidrange", begincidrange));
						insert(std::pair<std::string, OperatorTypes>("endcidrange", endcidrange));
						insert(std::pair<std::string, OperatorTypes>("begincidchar", begincidchar));
						insert(std::pair<std::string, OperatorTypes>("endcidchar", endcidchar));
						insert(std::pair<std::string, OperatorTypes>("beginnotdefrange", beginnotdefrange));
						insert(std::pair<std::string, OperatorTypes>("endnotdefrange", endnotdefrange));
						insert(std::pair<std::string, OperatorTypes>("beginnotdefchar", beginnotdefchar));
						insert(std::pair<std::string, OperatorTypes>("endnotdefchar", endnotdefchar));
						insert(std::pair<std::string, OperatorTypes>("beginbfrange", beginbfrange));
						insert(std::pair<std::string, OperatorTypes>("endbfrange", endbfrange));
						insert(std::pair<std::string, OperatorTypes>("beginbfchar", beginbfchar));
						insert(std::pair<std::string, OperatorTypes>("endbfchar", endbfchar));
					}
			};

			static CompressionCodes m_compression_codes;
			static OperatorCodes m_operator_codes;

		public:
			static CompressionTypes getCompressionCode(const std::string& compression_name)
			{
				CompressionCodes::const_iterator it = m_compression_codes.find(compression_name);
				if (it == m_compression_codes.end())
					return unknown_compression;
				return it->second;
			}

			static OperatorTypes getOperatorCode(const std::string& operator_name)
			{
				OperatorCodes::const_iterator it = m_operator_codes.find(operator_name);
				if (it == m_operator_codes.end())
					return unknown_operator;
				return it->second;
			}

		public:
			class ReferenceInfo;
			class PDFObject;
			class PDFDictionary;
			class PDFBoolean;
			class PDFNumericInteger;
			class PDFNumericFloat;
			class PDFArray;
			class PDFString;
			class PDFName;
			class PDFStream;
			class PDFNull;
			class PDFIndirectObject;
			class PDFReferenceCall;

			class PDFObject
			{
				public:
					virtual ~PDFObject()
					{
					}

					virtual PDFObjectTypes getType() = 0;

					virtual PDFString* getString()
					{
						return NULL;
					}

					bool isString()
					{
						return getType() == string;
					}

					virtual PDFDictionary* getDictionary()
					{
						return NULL;
					}

					bool isDictionary()
					{
						return getType() == dictionary;
					}

					virtual PDFBoolean* getBoolean()
					{
						return NULL;
					}

					bool isBoolean()
					{
						return getType() == boolean;
					}

					virtual PDFNumericFloat* getNumericFloat()
					{
						return NULL;
					}

					bool isNumericFloat()
					{
						return getType() == float_numeric;
					}

					virtual PDFNumericInteger* getNumericInteger()
					{
						return NULL;
					}

					bool isNumericInteger()
					{
						return getType() == int_numeric;
					}

					virtual PDFArray* getArray()
					{
						return NULL;
					}

					bool isArray()
					{
						return getType() == array;
					}

					virtual PDFName* getName()
					{
						return NULL;
					}

					bool isName()
					{
						return getType() == name;
					}

					virtual PDFStream* getStream()
					{
						return NULL;
					}

					bool isStream()
					{
						return getType() == stream;
					}

					virtual PDFNull* getNull()
					{
						return NULL;
					}

					bool isNull()
					{
						return getType() == null;
					}

					virtual PDFIndirectObject* getIndirectObject()
					{
						return NULL;
					}

					virtual bool isIndirectObject()
					{
						return false;
					}

					virtual PDFReferenceCall* getReferenceCall()
					{
						return NULL;
					}

					virtual bool isReferenceCall()
					{
						return false;
					}
			};

			class PDFDictionary : public PDFObject
			{
				public:
					std::map<std::string, PDFObject*> m_objects;

					~PDFDictionary()
					{
						clearDictionary();
					}

					void clearDictionary()
					{
						std::map<std::string, PDFObject*>::iterator it;
						for (it = m_objects.begin(); it != m_objects.end(); ++it)
							delete it->second;
						m_objects.clear();
					}

					PDFObject* operator [](const std::string& key)
					{
						std::map<std::string, PDFObject*>::iterator it = m_objects.find(key);
						if (it == m_objects.end())
							return NULL;
						return it->second;
					}

					PDFDictionary* getObjAsDictionary(const std::string& key)
					{
						std::map<std::string, PDFObject*>::iterator it = m_objects.find(key);
						if (it == m_objects.end())
							return NULL;
						return it->second->getDictionary();
					}

					PDFBoolean* getObjAsBoolean(const std::string& key)
					{
						std::map<std::string, PDFObject*>::iterator it = m_objects.find(key);
						if (it == m_objects.end())
							return NULL;
						return it->second->getBoolean();
					}

					bool getValAsBoolean(const std::string& key, bool def)
					{
						std::map<std::string, PDFObject*>::iterator it = m_objects.find(key);
						if (it == m_objects.end())
							return def;
						PDFBoolean* val = it->second->getBoolean();
						if (val)
							return val->m_value;
						return def;
					}

					PDFNumericInteger* getObjAsNumericInteger(const std::string& key)
					{
						std::map<std::string, PDFObject*>::iterator it = m_objects.find(key);
						if (it == m_objects.end())
							return NULL;
						return it->second->getNumericInteger();
					}

					PDFNumericFloat* getObjAsNumericFloat(const std::string& key)
					{
						std::map<std::string, PDFObject*>::iterator it = m_objects.find(key);
						if (it == m_objects.end())
							return NULL;
						return it->second->getNumericFloat();
					}

					double getValAsDouble(const std::string& key, double def)
					{
						std::map<std::string, PDFObject*>::iterator it = m_objects.find(key);
						if (it == m_objects.end())
							return def;
						PDFNumericFloat* float_val = it->second->getNumericFloat();
						if (float_val)
							return float_val->m_value;
						PDFNumericInteger* int_val = it->second->getNumericInteger();
						if (int_val)
							return (double)int_val->m_value;
						return def;
					}

					long getValAsInteger(const std::string& key, long def)
					{
						std::map<std::string, PDFObject*>::iterator it = m_objects.find(key);
						if (it == m_objects.end())
							return def;
						PDFNumericInteger* int_val = it->second->getNumericInteger();
						if (int_val)
							return int_val->m_value;
						return def;
					}

					PDFArray* getObjAsArray(const std::string& key)
					{
						std::map<std::string, PDFObject*>::iterator it = m_objects.find(key);
						if (it == m_objects.end())
							return NULL;
						return it->second->getArray();
					}

					PDFString* getObjAsString(const std::string& key)
					{
						std::map<std::string, PDFObject*>::iterator it = m_objects.find(key);
						if (it == m_objects.end())
							return NULL;
						return it->second->getString();
					}

					PDFName* getObjAsName(const std::string& key)
					{
						std::map<std::string, PDFObject*>::iterator it = m_objects.find(key);
						if (it == m_objects.end())
							return NULL;
						return it->second->getName();
					}

					std::string getValAsString(const std::string& key, const std::string& def)
					{
						std::map<std::string, PDFObject*>::iterator it = m_objects.find(key);
						if (it == m_objects.end())
							return def;
						PDFString* str_obj = it->second->getString();
						if (str_obj)
						{
							str_obj->ConvertToLiteral();
							return str_obj->m_value;
						}
						PDFName* name_obj = it->second->getName();
						if (name_obj)
						{
							return name_obj->m_value;
						}
						return def;
					}

					PDFNull* getObjAsNull(const std::string& key)
					{
						std::map<std::string, PDFObject*>::iterator it = m_objects.find(key);
						if (it == m_objects.end())
							return NULL;
						return it->second->getNull();
					}

					PDFReferenceCall* getObjAsReferenceCall(const std::string& key)
					{
						std::map<std::string, PDFObject*>::iterator it = m_objects.find(key);
						if (it == m_objects.end())
							return NULL;
						return it->second->getReferenceCall();
					}

					PDFStream* getObjAsStream(const std::string& key)
					{
						std::map<std::string, PDFObject*>::iterator it = m_objects.find(key);
						if (it == m_objects.end())
							return NULL;
						return it->second->getStream();
					}

					PDFObjectTypes getType()
					{
						return dictionary;
					}

					PDFDictionary* getDictionary()
					{
						return this;
					}
			};

			class PDFBoolean : public PDFObject
			{
				public:
					bool m_value;

					PDFBoolean()
					{
						m_value = false;
					}

					bool& operator ()()
					{
						return m_value;
					}

					PDFObjectTypes getType()
					{
						return boolean;
					}

					PDFBoolean* getBoolean()
					{
						return this;
					}
			};

			class PDFNumericInteger : public PDFObject
			{
				public:
					long m_value;

					PDFNumericInteger()
					{
						m_value = 0;
					}

					long& operator()()
					{
						return m_value;
					}

					PDFObjectTypes getType()
					{
						return int_numeric;
					}

					PDFNumericInteger* getNumericInteger()
					{
						return this;
					}
			};

			class PDFNumericFloat : public PDFObject
			{
				public:
					double m_value;

					PDFNumericFloat()
					{
						m_value = 0.0;
					}

					double& operator()()
					{
						return m_value;
					}

					PDFObjectTypes getType()
					{
						return float_numeric;
					}

					PDFNumericFloat* getNumericFloat()
					{
						return this;
					}
			};

			class PDFArray : public PDFObject
			{
				public:
					std::vector<PDFObject*> m_objects;

					~PDFArray()
					{
						for (size_t i = 0; i < m_objects.size(); ++i)
							delete m_objects[i];
						m_objects.clear();
					}

					PDFObject* operator [](unsigned long index)
					{
						return m_objects[index];
					}

					PDFDictionary* getObjAsDictionary(unsigned long index)
					{
						return m_objects[index]->getDictionary();
					}

					PDFBoolean* getObjAsBoolean(unsigned long index)
					{
						return m_objects[index]->getBoolean();
					}

					bool getValAsBoolean(unsigned long index, bool def)
					{
						PDFObject* obj = m_objects[index];
						PDFBoolean* val = obj->getBoolean();
						if (val)
							return val->m_value;
						return def;
					}

					PDFNumericInteger* getObjAsNumericInteger(unsigned long index)
					{
						return m_objects[index]->getNumericInteger();
					}

					PDFNumericFloat* getObjAsNumericFloat(unsigned long index)
					{
						return m_objects[index]->getNumericFloat();
					}

					PDFArray* getObjAsArray(unsigned long index)
					{
						return m_objects[index]->getArray();
					}

					double getValAsDouble(unsigned long index, double def)
					{
						PDFObject* obj = m_objects[index];
						PDFNumericFloat* float_val = obj->getNumericFloat();
						if (float_val)
							return float_val->m_value;
						PDFNumericInteger* int_val = obj->getNumericInteger();
						if (int_val)
							return (double)int_val->m_value;
						return def;
					}

					long getValAsInteger(unsigned long index, long def)
					{
						PDFObject* obj = m_objects[index];
						PDFNumericInteger* int_val = obj->getNumericInteger();
						if (int_val)
							return int_val->m_value;
						return def;
					}

					PDFStream* getObjAsStream(unsigned long index)
					{
						return m_objects[index]->getStream();
					}

					PDFString* getObjAsString(unsigned long index)
					{
						return m_objects[index]->getString();
					}

					PDFName* getObjAsName(unsigned long index)
					{
						return m_objects[index]->getName();
					}

					std::string getValAsString(unsigned long index, const std::string& def)
					{
						PDFObject* obj = m_objects[index];
						PDFString* str_obj = obj->getString();
						if (str_obj)
						{
							str_obj->ConvertToLiteral();
							return str_obj->m_value;
						}
						PDFName* name_obj = obj->getName();
						if (name_obj)
							return name_obj->m_value;
						return def;
					}

					PDFNull* getObjAsNull(unsigned long index)
					{
						return m_objects[index]->getNull();
					}

					PDFReferenceCall* getObjAsReferenceCall(unsigned long index)
					{
						return m_objects[index]->getReferenceCall();
					}

					PDFObjectTypes getType()
					{
						return array;
					}

					PDFArray* getArray()
					{
						return this;
					}

					size_t Size()
					{
						return m_objects.size();
					}
			};

			class PDFString : public PDFObject
			{
				public:
					bool m_is_hex;
					std::string m_value;

					PDFString()
					{
						m_is_hex = false;
					}

					std::string& operator()()
					{
						return m_value;
					}

					bool IsHex()
					{
						return m_is_hex;
					}

					bool IsLiteral()
					{
						return !m_is_hex;
					}

					void ConvertToHex()
					{
						if (!m_is_hex)
						{
							m_is_hex = true;
							std::string tmp;
							tmp.reserve(m_value.length() * 2);
							for (size_t i = 0; i < m_value.length(); ++i)
								tmp += char_to_hex_char(m_value[i]);
							m_value = tmp;
						}
					}

					void ConvertToLiteral()
					{
						if (m_is_hex)
						{
							m_is_hex = false;
							std::string tmp;
							tmp.reserve(m_value.length() / 2);
							for (size_t i = 0; i < m_value.length(); i += 2)
								tmp += hex_char_to_single_char(&m_value[i]);
							m_value = tmp;
						}
					}

					PDFObjectTypes getType()
					{
						return string;
					}

					PDFString* getString()
					{
						return this;
					}
			};

			class PDFName : public PDFObject
			{
				public:
					std::string m_value;

					std::string& operator ()()
					{
						return m_value;
					}

					PDFObjectTypes getType()
					{
						return name;
					}

					PDFName* getName()
					{
						return this;
					}
			};

			class PDFStream : public PDFObject
			{
				private:
					typedef std::vector<unsigned char> lzw_item;

					class Predictior
					{
						public:
							size_t m_predictor;
							size_t m_colors;
							size_t m_bpc;
							size_t m_columns;
							size_t m_early_change;
							bool m_next_byte_is_predictor;
							int m_current_predictor;
							int m_current_row_index;
							int m_bpp;
							std::vector<char> m_previos;

							Predictior(PDFDictionary& decode_params)
							{
								try
								{
									m_predictor = 1;
									PDFNumericInteger* pred_numeric = decode_params.getObjAsNumericInteger("Predictor");
									if (pred_numeric)
										m_predictor = (*pred_numeric)();

									m_colors = 1;
									PDFNumericInteger* color_numeric = decode_params.getObjAsNumericInteger("Colors");
									if (color_numeric)
										m_colors = (*color_numeric)();

									m_bpc = 8;
									PDFNumericInteger* bpc_numeric = decode_params.getObjAsNumericInteger("BitsPerComponent");
									if (bpc_numeric)
										m_colors = (*bpc_numeric)();

									m_columns = 1;
									PDFNumericInteger* columns_numeric = decode_params.getObjAsNumericInteger("Columns");
									if (columns_numeric)
										m_columns = (*columns_numeric)();

									m_early_change = 1;
									PDFNumericInteger* early_change_numeric = decode_params.getObjAsNumericInteger("EarlyChange");
									if (early_change_numeric)
										m_early_change = (*early_change_numeric)();
								}
								catch (const std::exception& e)
								{
									std::throw_with_nested(make_error("Error parsing predictor parameters"));
								}

								if (m_predictor >= 10)
								{
									m_next_byte_is_predictor = true;
									m_current_predictor = -1;
								}
								else
								{
									m_next_byte_is_predictor = false;
									m_current_predictor = m_predictor;
								}
								m_current_row_index = 0;
								m_bpp = (m_bpc * m_colors) >> 3;
								m_previos.resize((m_columns * m_colors * m_bpc) >> 3, 0);
							}

							void decode(unsigned char* src, unsigned int src_len, std::vector<unsigned char>& dest)
							{
								if (m_predictor == 1)
								{
									for (size_t i = 0; i < src_len; ++i)
										dest.push_back(src[i]);
									return;
								}
								size_t read_index = 0;
								while (read_index < src_len)
								{
									char ch = src[read_index++];
									if (m_next_byte_is_predictor)
									{
										m_current_predictor = ch + 10;
										m_next_byte_is_predictor = false;
									}
									else
									{
										switch (m_current_predictor)
										{
											case 2:
											{
												if (m_bpc == 8)
												{
													int tmp = 0;
													if (m_current_row_index - m_bpp >= 0)
														tmp = m_previos[m_current_row_index - m_bpp];
													m_previos[m_current_row_index++] = ch + tmp;
													break;
												}
												throw_if (m_bpc != 8, "Unsupported predictor parameters", m_bpc, errors::uninterpretable_data{});
											}
											case 10:
											{
												m_previos[m_current_row_index++] = ch;
												break;
											}
											case 11:
											{
												int tmp = 0;
												if (m_current_row_index - m_bpp >= 0)
													tmp = m_previos[m_current_row_index - m_bpp];
												m_previos[m_current_row_index++] = ch + tmp;
												break;
											}
											case 12:
											{
												m_previos[m_current_row_index++] += ch;
												break;
											}
											case 13:
											{
												int tmp = 0;
												if (m_current_row_index - m_bpp >= 0)
													tmp = m_previos[m_current_row_index - m_bpp];
												m_previos[m_current_row_index++] = ((tmp + m_previos[m_current_row_index]) >> 1) + ch;
												break;
											}
											case 14:
											case 15:
												throw_if (m_current_predictor == 14 || m_current_predictor == 15,
													"Unsupported predictor parameters", m_current_predictor, errors::uninterpretable_data{});
										}
									}

									if (m_current_row_index >= m_previos.size())
									{
										m_current_row_index = 0;
										m_next_byte_is_predictor = m_current_predictor >= 10;
										for (size_t i = 0; i < m_previos.size(); ++i)
											dest.push_back(m_previos[i]);
									}
								}
							}
					};

					struct CompressedObjectInfo
					{
						size_t m_offset;
						size_t m_index;
					};

				public:
					//iterates throw all elements in stream data. This kind of object can exist without PDFStream.
					//PDFStream only provides data for iterator.
					class PDFStreamIterator
					{
						private:
							struct Pointer
							{
								char* m_buffer;
								size_t m_element_size;
								size_t m_buffer_size;
								PDFObjectTypes m_type;
							};
							std::vector<Pointer> m_pointers_stack;
							size_t m_depth;

						public:
							void init(char* buffer, size_t buffer_size)
							{
								Pointer ptr;
								ptr.m_buffer = buffer;
								ptr.m_buffer_size = buffer_size;
								ptr.m_element_size = buffer_size;
								ptr.m_type = array;	//treat stream like a big array
								m_depth = 0;
								m_pointers_stack.push_back(ptr);
							}

							void seek(size_t offset)
							try
							{
								Pointer* ptr = &m_pointers_stack[m_depth];
								throw_if (offset > ptr->m_buffer_size);
								ptr->m_buffer_size -= offset;
								ptr->m_element_size = 0;
								ptr->m_type = null;
								ptr->m_buffer += offset;
							}
							catch (const std::exception&)
							{
								std::throw_with_nested(make_error(offset));
							}

							void backToRoot()
							{
								char* buffer = m_pointers_stack[0].m_buffer;
								size_t buffer_size = m_pointers_stack[0].m_buffer_size;
								m_pointers_stack.clear();
								init(buffer, buffer_size);
							}

							bool canDown()
							{
								PDFObjectTypes type = m_pointers_stack[m_depth].m_type;
								return type == array || type == dictionary;
							}

							void levelDown()
							{
								throw_if (!canDown(), "Can't go level down, it's not an array or dictionary",
									m_pointers_stack[m_depth].m_type, errors::program_logic{});
								Pointer* prev_ptr = &m_pointers_stack[m_depth];
								++m_depth;
								Pointer ptr;
								ptr.m_buffer = prev_ptr->m_buffer;
								if (prev_ptr->m_type == array)
								{
									ptr.m_buffer += 1;
									ptr.m_buffer_size = prev_ptr->m_element_size - 2;
								}
								else if (prev_ptr->m_type == dictionary)
								{
									ptr.m_buffer += 2;
									ptr.m_buffer_size = prev_ptr->m_element_size - 4;
								}
								ptr.m_element_size = 0;
								ptr.m_type = null;
								m_pointers_stack.push_back(ptr);
							}

							void getNextElement()
							{
								Pointer* ptr = &m_pointers_stack[m_depth];
								ptr->m_buffer += ptr->m_element_size;
								ptr->m_buffer_size -= ptr->m_element_size;
								ptr->m_element_size = 0;
								ptr->m_type = null;

								if (ptr->m_buffer_size == 0)
								{
									ptr->m_type = null;
									return;
								}
								while (ptr->m_buffer_size > 0)
								{
									switch (ptr->m_buffer[0])
									{
										case '/':
										{
											++ptr->m_element_size;
											ptr->m_type = name;
											readName(*ptr);
											return;
										}
										case '<':
										{
											++ptr->m_element_size;
											if (ptr->m_buffer_size > ptr->m_element_size)
											{
												if (ptr->m_buffer[1] == '<')
												{
													ptr->m_type = dictionary;
													++ptr->m_element_size;
													readDictionary(*ptr);
												}
												else
												{
													ptr->m_type = string;
													readHexString(*ptr);
												}
											}
											else
												ptr->m_type = null;
											return;
										}
										case '[':
										{
											++ptr->m_element_size;
											ptr->m_type = array;
											readArray(*ptr);
											return;
										}
										case '(':
										{
											++ptr->m_element_size;
											ptr->m_type = string;
											readLiteralString(*ptr);
											return;
										}
										case '+':
										case '-':
										case '.':
										{
											++ptr->m_element_size;
											if (ptr->m_buffer[0] == '.')
												ptr->m_type = float_numeric;
											else
												ptr->m_type = int_numeric;
											readNumeric(*ptr);
											return;
										}
										case '%':
										{
											//this is some comment we are not interested in.
											++ptr->m_buffer;
											--ptr->m_buffer_size;
											while (ptr->m_buffer_size > 0)
											{
												if (ptr->m_buffer[0] != 13 && ptr->m_buffer[0] != 10)
												{
													++ptr->m_buffer;
													--ptr->m_buffer_size;
												}
												else
													break;
											}
											break;
										}
										case '0':
										case '1':
										case '2':
										case '3':
										case '4':
										case '5':
										case '6':
										case '7':
										case '8':
										case '9':	//numeric or reference
										{
											size_t s = 0;
											size_t spaces = 0;
											//check if this object is numeric or reference
											while (s < ptr->m_buffer_size)
											{
												char ch = ptr->m_buffer[s++];
												switch (ch)
												{
													case ' ':
													{
														++spaces;
														if (spaces > 2)
														{
															ptr->m_type = int_numeric;
															readNumeric(*ptr);
															return;
														}
														break;
													}
													case 'R':
													{
														if (spaces == 2)
														{
															ptr->m_type = reference_call;
															readReferenceCall(*ptr);
															return;
														}
														ptr->m_type = int_numeric;
														readNumeric(*ptr);
														return;
													}
													case '0':
													case '1':
													case '2':
													case '3':
													case '4':
													case '5':
													case '6':
													case '7':
													case '8':
													case '9':
													{
														break;
													}
													default:
													{
														ptr->m_type = int_numeric;
														readNumeric(*ptr);
														return;
													}
												}
											}
											ptr->m_type = int_numeric;
											readNumeric(*ptr);
											return;
										}
										case 0:
										case 9:
										case 10:
										case 12:
										case 13:
										case 32:
										{
											break;
										}
										case 'f':
										{
											if (ptr->m_buffer_size >= 5 && memcmp(ptr->m_buffer, "false", 5) == 0)
											{
												ptr->m_type = boolean;
												ptr->m_element_size = 5;
												return;
											}
											ptr->m_type = operator_obj;
											readOperator(*ptr);
											return;
										}
										case 't':
										{
											if (ptr->m_buffer_size >= 4 && memcmp(ptr->m_buffer, "true", 4) == 0)
											{
												ptr->m_type = boolean;
												ptr->m_element_size = 4;
												return;
											}
											ptr->m_type = operator_obj;
											readOperator(*ptr);
											return;
										}
										case 'n':
										{
											if (ptr->m_buffer_size >= 4 && memcmp(ptr->m_buffer, "null", 4) == 0)
											{
												ptr->m_type = null;
												ptr->m_element_size = 4;
												return;
											}
											ptr->m_type = operator_obj;
											readOperator(*ptr);
											return;
										}
										default:
										{
											++ptr->m_element_size;
											ptr->m_type = operator_obj;
											readOperator(*ptr);
											return;
										}
									}
									++ptr->m_buffer;
									--ptr->m_buffer_size;
								}
								ptr->m_type = null;
							}

							bool canUp()
							{
								return m_depth > 0;
							}

							void levelUp()
							{
								throw_if (!canUp(), "Can't go level up, the current level is 0",
									m_depth, errors::program_logic{});
								--m_depth;
								m_pointers_stack.pop_back();
							}

							bool hasNext()
							{
								Pointer* ptr = &m_pointers_stack[m_depth];
								return ptr->m_buffer_size - ptr->m_element_size > 0;
							}

							const char* getData()
							{
								return m_pointers_stack[m_depth].m_buffer;
							}

							size_t getDataLength()
							{
								return m_pointers_stack[m_depth].m_element_size;
							}

							PDFObjectTypes getType()
							{
								return m_pointers_stack[m_depth].m_type;
							}

							std::string toPlainText()
							{
								Pointer* ptr = &m_pointers_stack[m_depth];
								return std::string(ptr->m_buffer, ptr->m_element_size);
							}

							void toHexString(std::string& val)
							{
								val.clear();
								Pointer* ptr = &m_pointers_stack[m_depth];
								throw_if (ptr->m_type != string, "not a string", ptr->m_type, errors::program_logic{});
								if (ptr->m_element_size == 0)
								{
									val = "00";
									return;
								}
								//already hex
								if (ptr->m_buffer[0] == '<')
								{
									for (int i = 1; i < ptr->m_element_size - 1; ++i)	//skip < and >
									{
										if (hex_char_is_valid(ptr->m_buffer[i]))
											val += ptr->m_buffer[i];
									}
									if (val.length() % 2 == 1)
										val += '0';
								}
								//convert from literal to hex
								else
								{
									for (int i = 1; i < ptr->m_element_size - 1; ++i)	//skip ( and )
									{
										if (ptr->m_buffer[i] == '\\')
										{
											++i;
											if (i < ptr->m_element_size - 1)
											{
												switch (ptr->m_buffer[i])
												{
													case 'n':
													case 10:
													{
														val += "0A";
														break;
													}
													case 't':
													case 9:
													{
														val += "09";
														break;
													}
													case 'f':
													case 12:
													{
														val += "0C";
														break;
													}
													case 8:
													case 'b':
													{
														val += "08";
														break;
													}
													case 'r':
													case 13:
													{
														val += "0D";
														break;
													}
													case '\\':
													{
														val += "5C";
														break;
													}
													case '(':
													{
														val += "28";
														break;
													}
													case ')':
													{
														val += "29";
														break;
													}
													case '0':
													case '1':
													case '2':
													case '3':
													case '4':
													case '5':
													case '6':
													case '7':
													case '8':
													case '9':
													{
														if (i < ptr->m_element_size - 3)
														{
															char octal[3];
															octal[0] = ptr->m_buffer[i];
															octal[1] = ptr->m_buffer[i + 1];
															octal[2] = ptr->m_buffer[i + 2];
															i += 2;
															char res = ((octal[0] - '0') << 6);
															res = res | ((octal[1] - '0') << 3);
															res = res | (octal[2] - '0');
															val += char_to_hex_char(res);
														}
														break;
													}
												}
											}
										}
										else
											val += char_to_hex_char(ptr->m_buffer[i]);
									}
								}
							}

							double toDouble()
							{
								Pointer* ptr = &m_pointers_stack[m_depth];
								throw_if (ptr->m_type != int_numeric && ptr->m_type != float_numeric,
									ptr->m_type, "not a numeric or float", errors::program_logic{});
								return strtod(ptr->m_buffer, NULL);
							}

							long toLong()
							{
								Pointer* ptr = &m_pointers_stack[m_depth];
								throw_if (ptr->m_type != int_numeric,
									"not a long integer", errors::program_logic{});
								return strtol(ptr->m_buffer, NULL, 10);
							}

						private:
							void readOperator(Pointer& ptr)
							{
								while (ptr.m_element_size < ptr.m_buffer_size)
								{
									switch (ptr.m_buffer[ptr.m_element_size])
									{
										case 0:
										case 9:
										case 10:
										case 13:
										case 32:
										case '[':
										case '{':
										case '<':
										case '(':
										case '%':
										{
											return;
										}
									}
									++ptr.m_element_size;
								}
							}

							void readName(Pointer& ptr)
							{
								while (ptr.m_element_size < ptr.m_buffer_size)
								{
									switch (ptr.m_buffer[ptr.m_element_size])
									{
										case 0:
										case 9:
										case 10:
										case 12:
										case 13:
										case 32:
										case '(':
										case ')':
										case '<':
										case '>':
										case '[':
										case ']':
										case '/':
										case '%':
										case '{':
										case '}':
										{
											return;
										}
									}
									++ptr.m_element_size;
								}
							}

							void readDictionary(Pointer& ptr)
							{
								char ch = 0, prev;
								int count = 0;
								int parentheses = 0;
								bool inside_comment = false;
								while (ptr.m_element_size < ptr.m_buffer_size)
								{
									prev = ch;
									ch = ptr.m_buffer[ptr.m_element_size++];
									switch (ch)
									{
										case '<':
										{
											if (parentheses == 0 && !inside_comment)
												++count;
											break;
										}
										case '>':
										{
											if (parentheses == 0 && !inside_comment)
											{
												--count;
												if (count == -2)	//we have reached closing '>>'
													return;
											}
											break;
										}
										case '%':
										{
											if (parentheses == 0)
												inside_comment = true;
											break;
										}
										case 10:
										case 13:
										{
											inside_comment = false;
											break;
										}
										case '(':
										{
											if (!inside_comment && (parentheses == 0 || prev != '\\'))
												++parentheses;
											break;
										}
										case ')':
										{
											if (!inside_comment && prev != '\\' && parentheses > 0)
												--parentheses;
											break;
										}
										case '\\':
										{
											if (prev == '\\')
											{
												prev = 0;
												ch = 0;
											}
											break;
										}
									}
								}
								throw make_error("Error parsing dictionary", errors::uninterpretable_data{});
							}

							void readHexString(Pointer& ptr)
							{
								char ch;
								while (ptr.m_element_size < ptr.m_buffer_size)
								{
									ch = ptr.m_buffer[ptr.m_element_size++];
									if (ch == '>')
										return;
									if (ch >= 'a' && ch <= 'f')
									{
										ptr.m_buffer[ptr.m_element_size - 1] -= ('a' - 'A');
										ch -= ('a' - 'A');
									}
								}
								throw make_error("Error parsing hex string", errors::uninterpretable_data{});
							}

							void readLiteralString(Pointer& ptr)
							{
								char ch = 0, prev;
								int count = 0;
								while (ptr.m_element_size < ptr.m_buffer_size)
								{
									prev = ch;
									ch = ptr.m_buffer[ptr.m_element_size++];
									switch (ch)
									{
										case '(':
										{
											if (prev != '\\')
												++count;
											break;
										}
										case ')':
										{
											if (prev != '\\')
												--count;
											if (count == -1)	//we have reached closing ')'
												return;
											break;
										}
										case '\\':
										{
											if (prev == '\\')
											{
												prev = 0;
												ch = 0;
											}
											break;
										}
									}
								}
								throw make_error("Error parsing literal string", errors::uninterpretable_data{});
							}

							void readNumeric(Pointer& ptr)
							{
								while (ptr.m_element_size < ptr.m_buffer_size)
								{
									switch (ptr.m_buffer[ptr.m_element_size])
									{
										case '.':
										{
											ptr.m_type = float_numeric;
											break;
										}
										case '0':
										case '1':
										case '2':
										case '3':
										case '4':
										case '5':
										case '6':
										case '7':
										case '8':
										case '9':
										{
											break;
										}
										default:
										{
											return;
										}
									}
									++ptr.m_element_size;
								}
							}

							void readReferenceCall(Pointer& ptr)
							{
								while (ptr.m_element_size < ptr.m_buffer_size)
								{
									if (ptr.m_buffer[ptr.m_element_size++] == 'R')
										return;
								}
								throw make_error("Error parsing reference call", errors::uninterpretable_data{});
							}

							void readArray(Pointer& ptr)
							{
								char ch = 0, prev;
								int count = 0;
								int parentheses = 0;
								bool inside_comment = false;
								while (ptr.m_element_size < ptr.m_buffer_size)
								{
									prev = ch;
									ch = ptr.m_buffer[ptr.m_element_size++];
									switch (ch)
									{
										case '[':
										{
											if (parentheses == 0 && !inside_comment)
												++count;
											break;
										}
										case ']':
										{
											if (parentheses == 0 && !inside_comment)
											{
												--count;
												if (count == -1)	//we have reached closing ']'
													return;
											}
											break;
										}
										case '%':
										{
											if (parentheses == 0)
												inside_comment = true;
											break;
										}
										case 10:
										case 13:
										{
											inside_comment = false;
											break;
										}
										case '(':
										{
											if (!inside_comment && prev != '\\')
												++parentheses;
											break;
										}
										case ')':
										{
											if (!inside_comment && prev != '\\' && parentheses > 0)
												--parentheses;
											break;
										}
										case '\\':
										{
											if (prev == '\\')
											{
												prev = 0;
												ch = 0;
											}
											break;
										}
									}
								}
								throw make_error("Error parsing array", errors::uninterpretable_data{});
							}
					};

					PDFDictionary* m_dictionary;
					char* m_stream_data_buffer;			//buffer for decoded stream
					size_t m_stream_data_buffer_len;
					size_t m_position;
					size_t m_size;
					bool m_is_in_external_file;									//are the data for this stream outside of this file?
					bool m_is_obj_stream;										//is this stream a collection of compressed object?
					bool m_is_decoded;											//is this stream decoded?
					bool m_loaded_compressed_objects;							//if this stream has compressed objects, are they loaded?

				private:
					std::vector<CompressedObjectInfo> m_compressed_objects;
					PDFStreamIterator m_stream_iterator;
					PDFReader* m_reader;

				public:
					PDFStream(PDFReader& reader, PDFDictionary& dictionary)
					{
						m_is_in_external_file = false;
						m_loaded_compressed_objects = false;
						m_is_decoded = true;
						m_is_obj_stream = false;
						m_stream_data_buffer = NULL;
						m_stream_data_buffer_len = 0;
						m_dictionary = &dictionary;
						m_position = 0;
						m_size = 0;
						m_reader = &reader;
					}

					~PDFStream()
					{
						if (m_stream_data_buffer)
							delete[] m_stream_data_buffer;
						if (m_dictionary)
							delete m_dictionary;
					}

					PDFObjectTypes getType()
					{
						return stream;
					}

					PDFStream* getStream()
					{
						return this;
					}

					PDFObject* getCompressedObject(size_t index)
					{
						try
						{
							load();
							throw_if (!m_is_obj_stream, "Stream is not an object stream", errors::program_logic{});
							if (!m_loaded_compressed_objects)
							{
								PDFNumericInteger* num_obj_count = m_dictionary->getObjAsNumericInteger("N");
								throw_if (!num_obj_count, "\"N\" entry not found in stream dictionary.", errors::uninterpretable_data{});
								PDFNumericInteger* offset_for_first_obj = m_dictionary->getObjAsNumericInteger("First");
								throw_if (!offset_for_first_obj, "\"First\" entry not found in stream dictionary.", errors::uninterpretable_data{});
								size_t first_offset = (*offset_for_first_obj)();
								size_t compressed_objects_count = (*num_obj_count)();
								m_stream_iterator.backToRoot();
								m_stream_iterator.levelDown();
								for (size_t i = 0; i < compressed_objects_count; ++i)
								{
									CompressedObjectInfo obj_info;
									m_stream_iterator.getNextElement();
									obj_info.m_index = m_stream_iterator.toLong();
									m_stream_iterator.getNextElement();
									obj_info.m_offset = m_stream_iterator.toLong() + first_offset;
									m_compressed_objects.push_back(obj_info);
								}
								m_loaded_compressed_objects = true;
							}
							m_stream_iterator.backToRoot();
							m_stream_iterator.levelDown();
							throw_if (index >= m_compressed_objects.size(),
								"Compressed object not found", index, m_compressed_objects.size() - 1, errors::uninterpretable_data{});
							m_stream_iterator.seek(m_compressed_objects[index].m_offset);
							return createNewObjectFromStream();
						}
						catch (const std::exception& e)
						{
							std::throw_with_nested(make_error(index));
						}
					}

					PDFStreamIterator& getIterator()
					{
						load();
						return m_stream_iterator;
					}

				private:
					static void ascii_85_decode(std::vector<unsigned char>& src, std::vector<unsigned char>& dest)
					{
						const unsigned long powers_85[5] = { 85 * 85 * 85 * 85, 85 * 85 * 85, 85 * 85, 85, 1 };
						int count = 0;
						unsigned int tuple = 0;
						size_t index_read = 0;
						size_t len = src.size();
						dest.clear();

						try
						{
							while (index_read < len)
							{
								unsigned char ch = src[index_read++];
								switch (ch)
								{
									case 'z':
										throw_if (count != 0, "Unexpected count parameter", count, errors::uninterpretable_data{});
										dest.push_back(0);
										dest.push_back(0);
										dest.push_back(0);
										dest.push_back(0);
										break;
									case '~':
										throw_if (index_read < len && src[index_read] != '>', errors::uninterpretable_data{});
										return;
									case '\n':
									case '\r':
									case '\t':
									case ' ':
									case '\0':
									case '\f':
									case '\b':
									case 0177:
										break;
									default:
										throw_if (ch < '!' || ch > 'u', errors::uninterpretable_data{});
										tuple += (ch - '!') * powers_85[count++];
										if (count == 5)
										{
											dest.push_back(tuple >> 24);
											dest.push_back((tuple & 0x00FF0000) >> 16);
											dest.push_back((tuple & 0x0000FF00) >> 8);
											dest.push_back(tuple & 0x000000FF);
											count = 0;
											tuple = 0;
										}
										break;
								}
							}
							if (count > 0)
							{
								tuple += powers_85[--count];
								int offset = 24;
								unsigned int mask = 0xFF000000;
								while (count > 0)
								{
									dest.push_back((tuple && mask) >> offset);
									offset -= 8;
									mask = mask >> 8;
									--count;
								}
							}
						}
						catch (const std::exception& e)
						{
							std::throw_with_nested(make_error("Error in ascii-85 decoding"));
						}
					}

					static void lzw_decode(std::vector<unsigned char>& src, std::vector<unsigned char>& dest, PDFDictionary* decode_params)
					{
						const unsigned short masks[4] = { 0x01FF, 0x03FF, 0x07FF, 0x0FFF };
						unsigned int mask = 0;
						unsigned int code_len = 9;
						unsigned char ch = src[0];
						unsigned int buffer_size = 0;
						unsigned int old = 0;
						unsigned int code = 0;
						unsigned int buffer = 0;
						unsigned int read_index = 0;
						std::unique_ptr<Predictior> predictor = nullptr;
						try
						{
							if (decode_params)
								predictor = std::make_unique<Predictior>(*decode_params);
							size_t len = src.size();
							dest.clear();
							dest.reserve(len);
							lzw_item item;
							std::vector<lzw_item> items_table;
							items_table.reserve(4096);
							lzw_item data;
							for (int i = 0; i < 256; ++i)
							{
								item.clear();
								item.push_back((unsigned char)i);
								items_table.push_back(item);
							}
							item.clear();
							items_table.push_back(item);

							while (read_index < len)
							{
								while (buffer_size <= 16 && read_index < len)
								{
									buffer <<= 8;
									buffer |= (unsigned int)src[read_index];
									buffer_size += 8;
									++read_index;
								}
								while (buffer_size >= code_len)
								{
									code = (buffer >> (buffer_size - code_len)) & masks[mask];
									buffer_size -= code_len;

									if (code == 0x0100)
									{
										mask = 0;
										code_len = 9;
										items_table.clear();
										for (int i = 0; i < 256; ++i)
										{
											item.clear();
											item.push_back((unsigned char)i);
											items_table.push_back(item);
										}
										item.clear();
										items_table.push_back(item);
									}
									else if (code == 0x0101)
									{
										return;
									}
									else
									{
										if (code >= items_table.size())
										{
											throw_if (old >= items_table.size(),
												"Index of old and current code are bigger than size of table",
												old, items_table.size(), errors::uninterpretable_data{});
											data = items_table[old];
											data.push_back(ch);
										}
										else
											data = items_table[code];
										if (predictor)
										{
											try
											{
												predictor->decode(&data[0], data.size(), dest);
											}
											catch (const std::exception& e)
											{
												std::throw_with_nested(make_error("Predictor::decode() failed"));
											}
										}
										else
										{
											for (size_t i = 0; i < data.size(); ++i)
												dest.push_back(data[i]);
										}
										ch = data[0];
										if (old < items_table.size())
											data = items_table[old];
										data.push_back(ch);
										items_table.push_back(data);
										data.pop_back();
										old = code;
										switch ((int)items_table.size())
										{
											case 511:
											case 1023:
											case 2047:
												++code_len;
												++mask;
										}
									}
								}
							}
						}
						catch (const std::exception& e)
						{
							std::throw_with_nested(make_error("Error in lzw decoding"));
						}
					}

					static void ascii_hex_decode(std::vector<unsigned char>& src, std::vector<unsigned char>& dest)
					{
						char hex_char[2];
						int got = 0;
						size_t len = src.size();
						size_t read_index = 0;
						dest.clear();
						try
						{
							while (read_index != len)
							{
								char ch = src[read_index++];
								if (ch >= 'a' && ch <= 'f')
									ch -= ('a' - 'A');
								if (ch < '0' || (ch > '9' && ch < 'A') || ch > 'F')
									continue;
								hex_char[got++] = ch;
								if (got == 2)
								{
									got = 0;
									dest.push_back(hex_char_to_single_char(hex_char));
								}
							}
							if (got == 1)
							{
								hex_char[1] = '0';
								dest.push_back(hex_char_to_single_char(hex_char));
							}
						}
						catch (const std::exception& e)
						{
							std::throw_with_nested(make_error("Error in ascii hex decoding"));
						}
					}

					static void flat_decode(std::vector<unsigned char>& src, std::vector<unsigned char>& dest, PDFDictionary* decode_params)
					{
						z_stream stream;
						unsigned char buffer[4096];
						Predictior* predictor = NULL;
						try
						{
							dest.clear();
							if (decode_params)
								predictor = new Predictior(*decode_params);
							stream.zalloc = Z_NULL;
							stream.zfree = Z_NULL;
							stream.opaque = Z_NULL;
							if (inflateInit(&stream) != Z_OK)
							{
								if (predictor)
									delete predictor;
								predictor = NULL;
								std::throw_with_nested(make_error("inflateInit() failed"));
							}
							int err;
							int written;
							stream.avail_in = src.size();
							stream.next_in  = &src[0];
							do
							{
								stream.avail_out = 4096;
								stream.next_out  = (Bytef*)buffer;
								switch ((err = inflate(&stream, Z_NO_FLUSH)))
								{
									case Z_NEED_DICT:
									case Z_DATA_ERROR:
									case Z_MEM_ERROR:
									{
										// warning TODO: Should I ignore this error and continue? One of the files I had was corrupted, but most data was readable.
										(void)inflateEnd(&stream);
										if (predictor)
											delete predictor;
										predictor = NULL;
										return;
									}
								}
								written = 4096 - stream.avail_out;
								if (predictor)
								{
									try
									{
										predictor->decode(buffer, written, dest);
									}
									catch (const std::exception& e)
									{
										delete predictor;
										predictor = NULL;
										std::throw_with_nested(make_error("Predictor::decode() failed"));
									}
								}
								else
								{
									for (size_t i = 0; i < written; ++i)
										dest.push_back(buffer[i]);
								}
							}
							while (stream.avail_out == 0);
							if (predictor)
								delete predictor;
							predictor = NULL;
						}
						catch (std::bad_alloc& ba)
						{
							if (predictor)
								delete predictor;
							predictor = NULL;
							throw;
						}
						catch (const std::exception& e)
						{
							if (predictor)
								delete predictor;
							predictor = NULL;
							std::throw_with_nested(make_error("Error in flat decoding"));
						}
					}

					static void run_length_decode(std::vector<unsigned char>& src, std::vector<unsigned char>& dest)
					{
						int code_len = 0;
						dest.clear();
						size_t read_index = 0;
						size_t len = src.size();
						try
						{
							while (read_index != len)
							{
								char ch = src[read_index++];
								if (!code_len)
									code_len = ch;
								else if (code_len == 128)
									break;
								else if (code_len <= 127)
								{
									dest.push_back(ch);
									--code_len;
								}
								else if (code_len >= 129)
								{
									code_len = 257 - code_len;
									while (code_len--)
										dest.push_back(ch);
								}
							}
						}
						catch (const std::exception& e)
						{
							std::throw_with_nested(make_error("Error in run length decoding"));
						}
					}

					void decode()
					{
						if (m_is_decoded)
							return;
						throw_if (m_is_in_external_file,
							"Stream data inside external file is not supported", errors::uninterpretable_data{});

						std::vector<PDFName*> filters;
						std::vector<PDFDictionary*> filter_options;
						auto load_filter_and_decode_params = [&]()
						{
							PDFObject* filter_entry = (*m_dictionary)["Filter"];
							PDFObject* decode_params_entry = (*m_dictionary)["DecodeParms"];
							if (decode_params_entry)
							{
								if (decode_params_entry->isArray())
								{
									PDFArray* array_decode_params = decode_params_entry->getArray();
									for (size_t i = 0; i < array_decode_params->Size(); ++i)
										filter_options.push_back(array_decode_params->getObjAsDictionary(i));
								}
								else
									filter_options.push_back(decode_params_entry->getDictionary());
							}
							else
								filter_options.push_back(NULL);

							if (filter_entry->isArray())
							{
								PDFArray* array_filters = filter_entry->getArray();
								for (size_t i = 0; i < array_filters->Size(); ++i)
									filters.push_back(array_filters->getObjAsName(i));
							}
							else
								filters.push_back(filter_entry->getName());
						};
						try
						{
							load_filter_and_decode_params();
						}
						catch (const std::exception& e)
						{
							std::throw_with_nested(make_error("load_filter_and_decode_params() failed"));
						}

						throw_if (filters.size() != filter_options.size(), errors::uninterpretable_data{});

						std::vector<unsigned char> stream_first_content(m_size);
						std::vector<unsigned char> stream_second_content;
						stream_second_content.reserve(m_size);
						size_t current_pos = m_reader->tell();
						throw_if (!m_reader->seek(m_position, std::ios_base::beg), "PDFReader seek error", m_position);
						throw_if (!m_reader->read((char*)&stream_first_content[0], m_size), "PDFReader read error", m_size);
						try
						{
							for (size_t i = 0; i < filters.size(); ++i)
							{
								CompressionTypes compression_type = PDFReader::getCompressionCode((*filters[i])());
								switch (compression_type)
								{
									case ascii_85:
									{
										if (i % 2 == 0)
											ascii_85_decode(stream_first_content, stream_second_content);
										else
											ascii_85_decode(stream_second_content, stream_first_content);
										break;
									}
									case lzw:
									{
										if (i % 2 == 0)
											lzw_decode(stream_first_content, stream_second_content, filter_options[i]);
										else
											lzw_decode(stream_second_content, stream_first_content, filter_options[i]);
										break;
									}
									case ascii_hex:
									{
										if (i % 2 == 0)
											ascii_hex_decode(stream_first_content, stream_second_content);
										else
											ascii_hex_decode(stream_second_content, stream_first_content);
										break;
									}
									case flat:
									{
										if (i % 2 == 0)
											flat_decode(stream_first_content, stream_second_content, filter_options[i]);
										else
											flat_decode(stream_second_content, stream_first_content, filter_options[i]);
										break;
									}
									case rle:
									{
										if (i % 2 == 0)
											run_length_decode(stream_first_content, stream_second_content);
										else
											run_length_decode(stream_second_content, stream_first_content);
										break;
									}
									case crypt:
									{
										throw make_error(errors::file_encrypted{});
										break;
									}
									default:
									{
										throw make_error("Unsupported compression type",
											compression_type, errors::uninterpretable_data{});
									}
								}
							}
							if (filters.size() % 2 == 1)
							{
								m_stream_data_buffer = new char[stream_second_content.size() + 2];
								m_stream_data_buffer[0] = '[';
								m_stream_data_buffer_len = stream_second_content.size() + 2;
								memcpy(m_stream_data_buffer + 1, &stream_second_content[0], stream_second_content.size());
								m_stream_data_buffer[m_stream_data_buffer_len - 1] = ']';
								m_stream_iterator.init(m_stream_data_buffer, m_stream_data_buffer_len);
							}
							else
							{
								m_stream_data_buffer = new char[stream_first_content.size() + 2];
								m_stream_data_buffer[0] = '[';
								m_stream_data_buffer_len = stream_first_content.size() + 2;
								memcpy(m_stream_data_buffer + 1, &stream_first_content[0], stream_first_content.size());
								m_stream_data_buffer[m_stream_data_buffer_len - 1] = ']';
								m_stream_iterator.init(m_stream_data_buffer, m_stream_data_buffer_len);
							}
							throw_if (!m_reader->seek(current_pos, std::ios_base::beg), "PDFReader seek error", current_pos);
							m_is_decoded = true;
						}
						catch (const std::exception& e)
						{
							std::throw_with_nested(make_error("Decoding failed"));
						}
					}

					void load()
					{
						try
						{
							if (m_stream_data_buffer)
								return;
							throw_if (m_is_in_external_file,
								"Stream data inside external file is not supported",
								errors::uninterpretable_data{});
							if (!m_is_decoded)
								decode();
							else
							{
								m_stream_data_buffer = new char[m_size + 2];
								m_stream_data_buffer[0] = '[';					//make array
								m_stream_data_buffer_len = m_size + 2;
								size_t current_pos = m_reader->tell();
								throw_if (!m_reader->seek(m_position, std::ios_base::beg), "PDFReader seek error", m_position);
								throw_if (!m_reader->read(m_stream_data_buffer + 1, m_size), m_size);
								m_stream_data_buffer[m_stream_data_buffer_len - 1] = ']';
								throw_if (!m_reader->seek(current_pos, std::ios_base::beg), "PDFReader seek error", current_pos);
								m_stream_iterator.init(m_stream_data_buffer, m_stream_data_buffer_len);
							}
						}
						catch (const std::exception& e)
						{
							std::throw_with_nested(make_error(m_position, m_size));
						}
					}

					PDFObject* createNewObjectFromStream()
					{
						m_stream_iterator.getNextElement();
						PDFObject* obj = NULL;
						try
						{
							switch (m_stream_iterator.getType())
							{
								case array:
								{
									obj = new PDFArray;
									m_stream_iterator.levelDown();
									while (m_stream_iterator.hasNext())
										((PDFArray*)(obj))->m_objects.push_back(createNewObjectFromStream());
									m_stream_iterator.levelUp();
									return obj;
								}
								case boolean:
								{
									obj = new PDFBoolean;
									if (m_stream_iterator.getData()[0] == 't')
										((PDFBoolean*)(obj))->m_value = true;
									else
										((PDFBoolean*)(obj))->m_value = false;
									return obj;
								}
								case dictionary:
								{
									obj = new PDFDictionary;
									m_stream_iterator.levelDown();
									while (m_stream_iterator.hasNext())
									{
										m_stream_iterator.getNextElement();
										throw_if (m_stream_iterator.getType() != name, errors::uninterpretable_data{});
										std::string name = std::string(m_stream_iterator.getData() + 1, m_stream_iterator.getDataLength() - 1);
										((PDFDictionary*)obj)->m_objects[name] = createNewObjectFromStream();
									}
									m_stream_iterator.levelUp();
									return obj;
								}
								case int_numeric:
								{
									obj = new PDFNumericInteger;
									((PDFNumericInteger*)obj)->m_value = strtol(m_stream_iterator.getData(), NULL, 10);
									return obj;
								}
								case float_numeric:
								{
									obj = new PDFNumericFloat;
									((PDFNumericFloat*)obj)->m_value = strtod(m_stream_iterator.getData(), NULL);
									return obj;
								}
								case string:
								{
									obj = new PDFString;
									((PDFString*)obj)->m_value = std::string(m_stream_iterator.getData() + 1, m_stream_iterator.getDataLength() - 2);
									if (m_stream_iterator.getData()[0] == '(')
									{
										((PDFString*)obj)->m_is_hex = false;
									}
									else
									{
										((PDFString*)obj)->m_is_hex = true;
									}
									return obj;
								}
								case name:
								{
									obj = new PDFName;
									((PDFName*)obj)->m_value = std::string(m_stream_iterator.getData() + 1, m_stream_iterator.getDataLength() - 1);
									return obj;
								}
								case null:
								{
									obj = new PDFNull;
									return obj;
								}
								case reference_call:
								{
									obj = new PDFReferenceCall(*m_reader);
									char* ptr_begin = (char*)m_stream_iterator.getData();
									char* ptr_end = ptr_begin;
									((PDFReferenceCall*)obj)->m_index = strtol(ptr_begin, &ptr_end, 10);
									((PDFReferenceCall*)obj)->m_generation = strtol(ptr_end, NULL, 10);
									return obj;
								}
								default:
								{
									throw make_error("Unsupported object type", m_stream_iterator.getType(),
										errors::uninterpretable_data{});
								}
							}
						}
						catch (const std::exception& e)
						{
							if (obj)
								delete obj;
							obj = NULL;
							std::throw_with_nested(make_error("Error creating new object from stream"));
						}
					}
			};

			class PDFNull : public PDFObject
			{
				public:
					PDFObjectTypes getType()
					{
						return null;
					}

					PDFNull* getNull()
					{
						return this;
					}
			};

			class PDFIndirectObject : public PDFObject
			{
				public:
					size_t m_generation;
					size_t m_index;
					PDFObject* m_object;
					PDFReader* m_reader;
					ReferenceInfo* m_owner;

				private:
					void loadObject()
					{
						if (!m_object)
						{
							try
							{
								m_object = m_reader->readIndirectObject(m_index);
							}
							catch (const std::exception& e)
							{
								std::throw_with_nested(make_error(m_index, m_generation));
							}
						}
					}

				public:
					PDFIndirectObject(PDFReader& reader, ReferenceInfo* owner)
					{
						m_generation = 0;
						m_index = 0;
						m_object = NULL;
						m_reader = &reader;
						m_owner = owner;
					}

					~PDFIndirectObject()
					{
						if (m_object)
							delete m_object;
					}

					PDFObjectTypes getType()
					{
						if (!m_object)
						{
							loadObject();
						}
						return m_object->getType();
					}

					PDFString* getString()
					{
						if (!m_object)
						{
							loadObject();
						}
						return m_object->getString();
					}

					PDFDictionary* getDictionary()
					{
						if (!m_object)
						{
							loadObject();
						}
						return m_object->getDictionary();
					}

					PDFBoolean* getBoolean()
					{
						if (!m_object)
						{
							loadObject();
						}
						return m_object->getBoolean();
					}

					PDFNumericFloat* getNumericFloat()
					{
						if (!m_object)
						{
							loadObject();
						}
						return m_object->getNumericFloat();
					}

					PDFNumericInteger* getNumericInteger()
					{
						if (!m_object)
						{
							loadObject();
						}
						return m_object->getNumericInteger();
					}

					PDFArray* getArray()
					{
						if (!m_object)
						{
							loadObject();
						}
						return m_object->getArray();
					}

					PDFName* getName()
					{
						if (!m_object)
						{
							loadObject();
						}
						return m_object->getName();
					}

					PDFNull* getNull()
					{
						if (!m_object)
						{
							loadObject();
						}
						return m_object->getNull();
					}

					PDFStream* getStream()
					{
						if (!m_object)
						{
							loadObject();
						}
						return m_object->getStream();
					}

					PDFIndirectObject* getIndirectObject()
					{
						return this;
					}

					bool isIndirectObject()
					{
						return true;
					}
			};

			class PDFReferenceCall : public PDFObject
			{
				public:
					size_t m_index;
					size_t m_generation;
					PDFObject* m_object;
					PDFReader* m_reader;

				private:
					void loadObject()
					{
						if (!m_object)
						{
							try
							{
								m_object = m_reader->readIndirectObject(m_index);
							}
							catch (const std::exception& e)
							{
								std::throw_with_nested(make_error(m_index, m_generation));
							}
						}
					}

				public:
					PDFReferenceCall(PDFReader& reader)
					{
						m_index = 0;
						m_generation = 0;
						m_object = NULL;
						m_reader = &reader;
					}

					PDFObjectTypes getType()
					{
						if (!m_object)
						{
							loadObject();
						}
						return m_object->getType();
					}

					PDFString* getString()
					{
						if (!m_object)
						{
							loadObject();
						}
						return m_object->getString();
					}

					PDFDictionary* getDictionary()
					{
						if (!m_object)
						{
							loadObject();
						}
						return m_object->getDictionary();
					}

					PDFBoolean* getBoolean()
					{
						if (!m_object)
						{
							loadObject();
						}
						return m_object->getBoolean();
					}

					PDFNumericFloat* getNumericFloat()
					{
						if (!m_object)
						{
							loadObject();
						}
						return m_object->getNumericFloat();
					}

					PDFNumericInteger* getNumericInteger()
					{
						if (!m_object)
						{
							loadObject();
						}
						return m_object->getNumericInteger();
					}

					PDFArray* getArray()
					{
						if (!m_object)
						{
							loadObject();
						}
						return m_object->getArray();
					}

					PDFName* getName()
					{
						if (!m_object)
						{
							loadObject();
						}
						return m_object->getName();
					}

					PDFNull* getNull()
					{
						if (!m_object)
						{
							loadObject();
						}
						return m_object->getNull();
					}

					PDFStream* getStream()
					{
						if (!m_object)
						{
							loadObject();
						}
						return m_object->getStream();
					}

					PDFReferenceCall* getReferenceCall()
					{
						return this;
					}

					bool isReferenceCall()
					{
						return true;
					}
			};

			class ReferenceInfo
			{
				public:
					enum ReferenceType
					{
						free,
						in_use,
						compressed
					};

					ReferenceType m_type;
					size_t m_generation;
					size_t m_offset;
					bool m_read;
					PDFIndirectObject* m_object;

					ReferenceInfo()
					{
						m_type = free;
						m_generation = 0;
						m_offset = 0;
						m_object = NULL;
						m_read = false;
					}

					ReferenceInfo& operator = (const ReferenceInfo& info)
					{
						m_read = info.m_read;
						m_offset = info.m_offset;
						m_generation = info.m_generation;
						m_type = info.m_type;
						m_object = info.m_object;
						if (m_object)
							m_object->m_owner = this;
						return *this;
					}

					ReferenceInfo(const ReferenceInfo& info)
					{
						m_read = info.m_read;
						m_offset = info.m_offset;
						m_generation = info.m_generation;
						m_type = info.m_type;
						m_object = info.m_object;
						if (m_object)
							m_object->m_owner = this;
					}

					~ReferenceInfo()
					{
						if (m_object && m_object->m_owner == this)
							delete m_object;
					}
			};

		private:
			std::shared_ptr<std::istream> m_data_stream;
			std::vector<ReferenceInfo> m_references;
			PDFDictionary m_trailer_dict;
			bool m_got_root;
			bool m_got_info;
			PDFReferenceCall* m_root_ref;
			PDFReferenceCall* m_info_ref;

		public:
			PDFDictionary* m_root_dictionary;
			PDFDictionary* m_info;
			PDFStream* m_metadata;

		public:
			PDFReader(std::shared_ptr<std::istream> data_stream)
				: m_data_stream(data_stream), m_root_dictionary(NULL), m_info(NULL), m_metadata(NULL), m_got_info(false), m_got_root(false)
			{
				m_root_ref = NULL;
				m_info_ref = NULL;
				try
				{
					m_root_ref = new PDFReferenceCall(*this);
					m_info_ref = new PDFReferenceCall(*this);
					readReferenceData();
					if (m_got_info)
						m_info = m_info_ref->getDictionary();
					if (m_got_root)
						m_root_dictionary = m_root_ref->getDictionary();
					throw_if (!m_root_dictionary);
					m_metadata = m_root_dictionary->getObjAsStream("Metadata");
				}
				catch (std::bad_alloc& ba)
				{
					if (m_root_ref)
						delete m_root_ref;
					if (m_info_ref)
						delete m_info_ref;
					throw;
				}
				catch (const std::exception& e)
				{
					if (m_root_ref)
						delete m_root_ref;
					if (m_info_ref)
						delete m_info_ref;
					throw;
				}
			}

			~PDFReader()
			{
				delete m_root_ref;
				delete m_info_ref;
			}

			void readLine(std::string& line)
			{
				int ch;
				line.clear();
				while (true)
				{
					ch = m_data_stream->get();
					switch (ch)
					{
						case 13:
						{
							ch = m_data_stream->get();
							if (ch != 10)
								m_data_stream->unget();
							return;
						}
						case '%':
						case 10:
						{
							return;
						}
						case EOF:
						{
							throw make_error("Unexpected EOF", errors::uninterpretable_data{});
						}
						default:
						{
							line.push_back(ch);
						}
					}
				}
			}

			void skipComment()
			{
				int ch;
				while (true)
				{
					ch = m_data_stream->get();
					switch (ch)
					{
						case 13:
						{
							ch = m_data_stream->get();
							if (ch != 10)
								m_data_stream->unget();
							return;
						}
						case 10:
						{
							return;
						}
						case EOF:
						{
							throw make_error("Unexpected EOF", errors::uninterpretable_data{});
						}
					}
				}
			}

			void skipKeyword(const std::string& keyword)
			{
				size_t found = 0;
				size_t len = keyword.length();
				while (true)
				{
					int ch = m_data_stream->get();
					if (ch == EOF)
						throw make_error("Unexpected EOF", errors::uninterpretable_data{});
					if (keyword[found] == ch)
					{
						++found;
						if (found == len)
							return;
					}
					else
						found = 0;
				}
			}

			void readName(PDFName& name)
			{
				char ch;
				//go to the name. Character (/) marks beggining of the name
				while (true)
				{
					throw_if (!m_data_stream->read(&ch, 1), "Unexpected EOF");
					if (ch == '/')
						break;
				}
				name.m_value.clear();
				while (true)
				{
					ch = m_data_stream->get();
					throw_if (ch == EOF, "Unexpected EOF", errors::uninterpretable_data{});
					switch (ch)
					{
						case 0:
						case 9:
						case 10:
						case 12:
						case 13:
						case 32:
						case '(':
						case ')':
						case '<':
						case '>':
						case '[':
						case ']':
						case '/':
						case '%':
						case '{':
						case '}':
						{
							//this character is not part of the name
							m_data_stream->unget();
							return;
						}
						case '#':
						{
							char hex_char[2];
							throw_if (!m_data_stream->read(hex_char, 2), "Unexpected EOF");
							name.m_value += hex_char_to_single_char(hex_char);
							break;
						}
						default:
						{
							name.m_value += ch;
						}
					}
				}
			}

			void readString(PDFString& string)
			{
				char ch;
				string.m_is_hex = false;
				string.m_value.clear();
				//search for string content
				while (true)
				{
					throw_if (!m_data_stream->read(&ch, 1), "Unexpected EOF");
					if (ch == '(')	//literal
						break;
					if (ch == '<')	//hex
					{
						string.m_is_hex = true;
						break;
					}
				}
				if (string.m_is_hex)
				{
					char hex_char[2];
					unsigned int got = 0;
					while (true)
					{
						throw_if (!m_data_stream->read(&ch, 1), "Unexpected EOF");
						if (ch == '>')
						{
							if (got == 1)
							{
								hex_char[1] = '0';
								string.m_value += hex_char_to_single_char(hex_char);
							}
							return;
						}
						normalize_hex_char(ch);
						if (!hex_char_is_valid(ch))
							continue;
						hex_char[got++] = ch;
						if (got == 2)
						{
							got = 0;
							string.m_value += hex_char_to_single_char(hex_char);
						}
					}
				}
				else
				{
					int parentheses_depth = 0;
					while (true)
					{
						throw_if (!m_data_stream->read(&ch, 1), "Unexpected EOF");
						switch (ch)
						{
							case '\\':
							{
								throw_if (!m_data_stream->read(&ch, 1), "Unexpected EOF");
								switch (ch)
								{
									case 10:
									case 'n':
									{
										string.m_value += '\n';
										break;
									}
									case 'r':
									{
										string.m_value += '\r';
										break;
									}
									case 't':
									{
										string.m_value += '\t';
										break;
									}
									case 'b':
									{
										string.m_value += '\b';
										break;
									}
									case 'f':
									{
										string.m_value += '\f';
										break;
									}
									case '(':
									{
										string.m_value += '(';
										break;
									}
									case ')':
									{
										string.m_value += ')';
										break;
									}
									case 13:
									{
										ch = m_data_stream->get();
										throw_if (ch == EOF, "Unexpected EOF", errors::uninterpretable_data{});
										if (ch != 10)
											m_data_stream->unget();
										break;
									}
									case '0':
									case '1':
									case '2':
									case '3':
									case '4':
									case '5':
									case '6':
									case '7':
									case '8':
									case '9':
									{
										char octal[3];
										octal[0] = ch;
										throw_if (!m_data_stream->read(octal + 1, 2), "Unexpected EOF");
										char res = ((octal[0] - '0') << 6);
										res = res | ((octal[1] - '0') << 3);
										res = res | (octal[2] - '0');
										string.m_value += res;
										break;
									}
									case '\\':
									{
										string.m_value += '\\';
										break;
									}
								}
								break;
							}
							case 10:
							{
								string.m_value += '\n';
								break;
							}
							case 13:
							{
								ch = m_data_stream->get();
								throw_if (ch == EOF, "Unexpected EOF", errors::uninterpretable_data{});
								if (ch != 10)
									m_data_stream->unget();
								string.m_value += '\n';
								break;
							}
							case '(':
							{
								++parentheses_depth;
								string.m_value += '(';
								break;
							}
							case ')':
							{
								if (parentheses_depth == 0)
									return;
								--parentheses_depth;
								string.m_value += ')';
								break;
							}
							default:
							{
								string.m_value += ch;
							}
						}
					}
				}
			}

			void readBoolean(PDFBoolean& boolean)
			{
				char buffer[4];
				char ch = m_data_stream->get();
				throw_if (ch == EOF, "Unexpected EOF", errors::uninterpretable_data{});
				if (ch == 't')
				{
					boolean.m_value = true;
					//read rest of the string (true)
					throw_if (!m_data_stream->read(buffer, 3), "Unexpected EOF");
				}
				else	//false
				{
					boolean.m_value = false;
					//read rest of the string (false)
					throw_if (!m_data_stream->read(buffer, 4), "Unexpected EOF");
				}
			}

			void readArray(PDFArray& array)
			{
				char ch;
				while (true)
				{
					throw_if (!m_data_stream->read(&ch, 1), "Unexpected EOF");
					if (ch == '[')
						break;
				}
				while (true)
				{
					ch = m_data_stream->get();
					throw_if (ch == EOF, "Unexpected EOF", errors::uninterpretable_data{});
					if (ch == ']')
						return;
					PDFObject* value_object = NULL;
					try
					{
						switch (ch)
						{
							case '/':	//value is a name
							{
								value_object = new PDFName;
								m_data_stream->unget();
								readName(*(PDFName*)value_object);
								array.m_objects.push_back(value_object);
								break;
							}
							case '<':	//value is a hexadecimal string or dictionary
							{
								ch = m_data_stream->get();
								throw_if (ch == EOF, "Unexpected EOF", errors::uninterpretable_data{});
								if (ch == '<')	//dictionary
								{
									value_object = new PDFDictionary;
									m_data_stream->unget();
									m_data_stream->unget();
									readDictionary(*(PDFDictionary*)value_object);
									array.m_objects.push_back(value_object);
								}
								else	//hexadecimal string
								{
									value_object = new PDFString;
									m_data_stream->unget();
									m_data_stream->unget();
									readString(*(PDFString*)value_object);
									array.m_objects.push_back(value_object);
								}
								break;
							}
							case '(':	//value is a literal string
							{
								value_object = new PDFString;
								m_data_stream->unget();
								readString(*(PDFString*)value_object);
								array.m_objects.push_back(value_object);
								break;
							}
							case '%':
							{
								skipComment();
								break;
							}
							case 'f':
							case 't':	//value is a boolean
							{
								value_object = new PDFBoolean;
								m_data_stream->unget();
								readBoolean(*(PDFBoolean*)value_object);
								array.m_objects.push_back(value_object);
								break;
							}
							case '[':	//value is an array
							{
								value_object = new PDFArray;
								m_data_stream->unget();
								readArray(*(PDFArray*)value_object);
								array.m_objects.push_back(value_object);
								break;
							}
							case 'n':	//value is a null
							{
								value_object = new PDFNull;
								m_data_stream->unget();
								readNull(*(PDFNull*)value_object);
								array.m_objects.push_back(value_object);
								break;
							}
							case '+':
							case '-':
							case '.':	//value is a numeric
							{
								m_data_stream->unget();
								value_object = readNumeric();
								array.m_objects.push_back(value_object);
								break;
							}
							case '0':
							case '1':
							case '2':
							case '3':
							case '4':
							case '5':
							case '6':
							case '7':
							case '8':
							case '9':	//value is an indirect reference or number
							{
								int to_seek_backward = 1;
								int spaces = 0;
								bool is_reference = false;
								while (true)
								{
									//number -> just a number
									//indirect reference: two numbers and 'R' character with spaces
									ch = m_data_stream->get();
									++to_seek_backward;
									throw_if (ch == EOF, "Unexpected EOF", errors::uninterpretable_data{});
									if (ch == ' ')
									{
										++spaces;
										if (spaces > 2)	//indirect reference contain only two spaces
											break;
									}
									else if (ch == 'R' && spaces == 2)	//indirect reference
									{
										is_reference = true;
										break;
									}
									else if (ch < '0' || ch > '9')	//something beyond a digit (and this is not a space)
									{
										break;
									}
								}
								throw_if (!m_data_stream->seekg(-to_seek_backward, std::ios_base::cur), "std::istream::seekg() failed", -to_seek_backward);
								if (is_reference)
								{
									value_object = new PDFReferenceCall(*this);
									readIndirectReference(*(PDFReferenceCall*)value_object);
								}
								else
									value_object = readNumeric();
								array.m_objects.push_back(value_object);
								break;
							}
						}
					}
					catch (std::bad_alloc& ba)
					{
						if (value_object)
							delete value_object;
						value_object = NULL;
						throw;
					}
					catch (const std::exception& e)
					{
						if (value_object)
							delete value_object;
						value_object = NULL;
						std::throw_with_nested(make_error("Error reading array"));
					}
				}
			}

			PDFObject* readNumeric()
			{
				bool negative = false;
				bool is_float = false;
				std::string number_str;
				while (true)
				{
					char ch = m_data_stream->get();
					switch (ch)
					{
						case EOF:
						{
							throw make_error("Unexpected EOF", errors::uninterpretable_data{});
						}
						case '-':
						{
							negative = true;
							break;
						}
						case '+':
						{
							break;
						}
						case '.':
						{
							if (number_str.length() == 0)
								number_str += "0.";
							else
								number_str += ".";
							is_float = true;
							break;
						}
						case '0':
						case '1':
						case '2':
						case '3':
						case '4':
						case '5':
						case '6':
						case '7':
						case '8':
						case '9':
						{
							number_str += ch;
							break;
						}
						default:
						{
							m_data_stream->unget();
							char* begin = (char*)number_str.c_str();
							char* end = begin;
							PDFObject* object;
							if (is_float)
							{
								double value = strtod(begin, &end);
								throw_if (value == 0.0 && begin == end,
									"Conversion to double failed", number_str, errors::uninterpretable_data{});
								if (negative)
									value = -value;
								object = new PDFNumericFloat;
								((PDFNumericFloat*)object)->m_value = value;
							}
							else
							{
								long value = strtol(begin, &end, 10);
								throw_if (value == 0 && begin == end,
									"Conversion to long int failed", number_str, errors::uninterpretable_data{});
								if (negative)
									value = -value;
								object = new PDFNumericInteger;
								((PDFNumericInteger*)object)->m_value = value;
							}
							return object;
						}
					}
				}
			}

			void readNull(PDFNull& null)
			{
				char buffor[4];
				try
				{
					throw_if (!m_data_stream->read(buffor, 4));
					throw_if (memcmp(buffor, "null", 4) != 0, errors::uninterpretable_data{});
				}
				catch (std::exception&)
				{
					std::throw_with_nested(make_error("Error reading null"));
				}
			}

			void readStream(PDFStream& stream)
			{
				try
				{
					char ch;
					PDFDictionary* stream_dict = stream.m_dictionary;
					PDFNumericInteger* len = stream_dict->getObjAsNumericInteger("Length");
					throw_if (!len, "\"Length\" object not found in stream dictionary", errors::uninterpretable_data{});
					stream.m_size = (*len)();
					//check if stream is encoded.
					if (stream_dict->getObjAsName("Filter") || stream_dict->getObjAsArray("Filter"))
						stream.m_is_decoded = false;
					if (stream_dict->getObjAsNumericInteger("N"))
						stream.m_is_obj_stream = true;

					skipKeyword("stream");
					//skip EOL
					ch = m_data_stream->get();
					if (ch == 13)
						ch = m_data_stream->get();
					throw_if (ch != 10, ch, errors::uninterpretable_data{});
					stream.m_position = m_data_stream->tellg();
					//Stream data can be included in external file.
					if ((*stream_dict)["F"])
					{
						// warning TODO: Add support for reading from external files
						stream.m_is_in_external_file = true;
					}
					else
					{
						throw_if (!m_data_stream->seekg(stream.m_size, std::ios_base::cur), "std::istream::seekg failed", stream.m_size);
					}
					skipKeyword("endstream");
				}
				catch (const std::exception& e)
				{
					std::throw_with_nested(make_error("Error reading stream"));
				}
			}

			void readIndirectReference(PDFReferenceCall& reference)
			{
				char ch;
				reference.m_generation = 0;
				reference.m_index = 0;
				std::string text;
				text.reserve(25);
				int stage = 0;	//0 = reading reference index, 1 = reading reference generation, 2 = reading 'R' character
				while (true)
				{
					throw_if (!m_data_stream->read(&ch, 1), "Unexpected EOF");
					switch (ch)
					{
						case '0':
						case '1':
						case '2':
						case '3':
						case '4':
						case '5':
						case '6':
						case '7':
						case '8':
						case '9':
						{
							if (stage < 2)
								text += ch;
							break;
						}
						case 'R':
						{
							if (stage == 2)
								return;
							break;
						}
						default:
						{
							char* begin = (char*)text.c_str();
							char* end = begin;
							if (stage == 0 && text.length() > 0)
							{
								reference.m_index = strtol(begin, &end, 10);
								throw_if (reference.m_index == 0 || end == begin,
									"Conversion to long int failed", text, errors::uninterpretable_data{});
								text.clear();
								++stage;
							}
							else if (stage == 1 && text.length() > 0)
							{
								reference.m_generation = strtol(begin, &end, 10);
								throw_if (reference.m_index == 0 || end == begin,
									"Conversion to long int failed", text, errors::uninterpretable_data{});
								text.clear();
								++stage;
							}
						}
					}
				}
			}

			void readDictionary(PDFDictionary& dictionary)
			{
				int ch = 0, prev_ch = 0;
				bool reading_key = false;
				bool reading_value = false;
				PDFName key_name;
				//search for dictionary
				while (true)
				{
					prev_ch = ch;
					ch = m_data_stream->get();
					throw_if (ch == EOF, "Unexpected EOF", errors::uninterpretable_data{});
					if (prev_ch == '<' && ch == '<')
					{
						reading_key = true;
						break;
					}
				}
				while (true)
				{
					prev_ch = ch;
					ch = m_data_stream->get();
					throw_if (ch == EOF, "Unexpected EOF", errors::uninterpretable_data{});
					if (ch == '>' && prev_ch == '>')
						return;
					if (ch == '%')
					{
						skipComment();
						break;
					}
					else if (reading_key && ch == '/')
					{
						m_data_stream->unget();
						key_name.m_value.clear();
						try
						{
							readName(key_name);
						}
						catch (const std::exception& e)
						{
							std::throw_with_nested(make_error("readName() failed"));
						}
						reading_value = true;
						reading_key = false;
					}
					else if (reading_value)
					{
						PDFObject* value_object = NULL;
						try
						{
							switch (ch)
							{
								case '/':	//value is a name
								{
									value_object = new PDFName;
									m_data_stream->unget();
									readName(*(PDFName*)value_object);
									reading_value = false;
									reading_key = true;
									dictionary.m_objects[key_name.m_value] = value_object;
									break;
								}
								case '<':	//value is a hexadecimal string or dictionary
								{
									ch = m_data_stream->get();
									throw_if (ch == EOF, "Unexpected EOF", errors::uninterpretable_data{});
									if (ch == '<')	//dictionary
									{
										value_object = new PDFDictionary;
										m_data_stream->unget();
										m_data_stream->unget();
										readDictionary(*(PDFDictionary*)value_object);
										reading_value = false;
										reading_key = true;
										dictionary.m_objects[key_name.m_value] = value_object;
									}
									else	//hexadecimal string
									{
										value_object = new PDFString;
										m_data_stream->unget();
										m_data_stream->unget();
										readString(*(PDFString*)value_object);
										reading_value = false;
										reading_key = true;
										dictionary.m_objects[key_name.m_value] = value_object;
									}
									break;
								}
								case '(':	//value is a literal string
								{
									value_object = new PDFString;
									m_data_stream->unget();
									readString(*(PDFString*)value_object);
									reading_value = false;
									reading_key = true;
									dictionary.m_objects[key_name.m_value] = value_object;
									break;
								}
								case 'f':
								case 't':	//value is a boolean
								{
									value_object = new PDFBoolean;
									m_data_stream->unget();
									readBoolean(*(PDFBoolean*)value_object);
									reading_value = false;
									reading_key = true;
									dictionary.m_objects[key_name.m_value] = value_object;
									break;
								}
								case '[':	//value is an array
								{
									value_object = new PDFArray;
									m_data_stream->unget();
									readArray(*(PDFArray*)value_object);
									reading_value = false;
									reading_key = true;
									dictionary.m_objects[key_name.m_value] = value_object;
									break;
								}
								case 'n':	//value is a null
								{
									value_object = new PDFNull;
									m_data_stream->unget();
									readNull(*(PDFNull*)value_object);
									reading_value = false;
									reading_key = true;
									dictionary.m_objects[key_name.m_value] = value_object;
									break;
								}
								case '+':
								case '-':
								case '.':	//value is a numeric
								{
									m_data_stream->unget();
									value_object = readNumeric();
									reading_value = false;
									reading_key = true;
									dictionary.m_objects[key_name.m_value] = value_object;
									break;
								}
								case '0':
								case '1':
								case '2':
								case '3':
								case '4':
								case '5':
								case '6':
								case '7':
								case '8':
								case '9':	//value is an indirect reference or number
								{
									int seek_backward = 1;
									int spaces = 0;
									bool is_reference = false;
									while (true)
									{
										//number -> just a number
										//indirect reference: two integers and 'R' character with spaces
										ch = m_data_stream->get();
										++seek_backward;
										throw_if (ch == EOF, "Unexpected EOF", errors::uninterpretable_data{});
										if (ch == ' ')
										{
											++spaces;
											if (spaces > 2)	//indirect reference contain only two spaces
												break;
										}
										else if (ch == 'R' && spaces == 2)	//indirect reference
										{
											is_reference = true;
											break;
										}
										else if (ch < '0' || ch > '9')	//something beyond a digit (and this is not space)
										{
											break;
										}
									}
									throw_if (!m_data_stream->seekg(-seek_backward, std::ios_base::cur), "std::istream::seekg() failed", -seek_backward);
									if (is_reference)
									{
										value_object = new PDFReferenceCall(*this);
										readIndirectReference(*(PDFReferenceCall*)value_object);
									}
									else
										value_object = readNumeric();
									reading_value = false;
									reading_key = true;
									dictionary.m_objects[key_name.m_value] = value_object;
									break;
								}
							}
						}
						catch (std::bad_alloc& ba)
						{
							if (value_object)
								delete value_object;
							value_object = NULL;
							throw;
						}
						catch (const std::exception& e)
						{
							if (value_object)
								delete value_object;
							value_object = NULL;
							std::throw_with_nested(make_error(key_name.m_value));
						}
					}
				}
			}

			PDFObject* readIndirectObject(size_t index)
			{
				try
				{
					throw_if (index >= m_references.size(), index, m_references.size() - 1,
						"Indirect object index is out of range", errors::uninterpretable_data{});
					ReferenceInfo* reference_info = &m_references[index];
					if (reference_info->m_object)
					{
						if (reference_info->m_object->m_object)
							return reference_info->m_object->m_object;
					}
					else
					{
						reference_info->m_object = new PDFIndirectObject(*this, reference_info);
						reference_info->m_object->m_generation = reference_info->m_generation;
						reference_info->m_object->m_index = index;
					}
					switch (reference_info->m_type)
					{
						case ReferenceInfo::free:
						{
							reference_info->m_object->m_object = new PDFNull;
							return reference_info->m_object->m_object;
						}
						case ReferenceInfo::compressed:	//in use, but compressed
						{
							//object is compressed in another stream, m_offset is an index here.
							throw_if (reference_info->m_offset >= m_references.size(),
								reference_info->m_offset, m_references.size() - 1, errors::uninterpretable_data{});
							ReferenceInfo* object_stream_reference = &m_references[reference_info->m_offset];
							if (!object_stream_reference->m_object)
							{
								object_stream_reference->m_object = new PDFIndirectObject(*this, object_stream_reference);
								object_stream_reference->m_object->m_index = reference_info->m_offset;
								object_stream_reference->m_object->m_generation = object_stream_reference->m_generation;
							}
							PDFStream* object_stream = NULL;
							auto get_compressed_objects_stream = [&]()
							{
								if (!object_stream_reference->m_object->m_object)
									object_stream_reference->m_object->m_object = readIndirectObject(object_stream_reference->m_object->m_index);
								object_stream = object_stream_reference->m_object->getStream();
								throw_if (!object_stream, "PDFObject::getStream() failed");
							};
							try
							{
								get_compressed_objects_stream();
							}
							catch (const std::exception& e)
							{
								std::throw_with_nested(make_error("get_compressed_objects_stream() failed"));
							}
							//generation is an index in compressed object.
							reference_info->m_object->m_object = object_stream->getCompressedObject(reference_info->m_generation);
							return reference_info->m_object->m_object;
						}
						case ReferenceInfo::in_use:
						{
							size_t current_position = m_data_stream->tellg();
							throw_if (current_position == std::streampos{-1}, "Failed to get stream position");
							throw_if (!seek(reference_info->m_offset, std::ios_base::beg), "PDFReader::seek() failed", reference_info->m_offset);
							try
							{
								skipKeyword("obj");
							}
							catch (const std::exception& e)
							{
								std::throw_with_nested(make_error("skipKeyword() failed", std::make_pair("keyword", "obj")));
							}
							PDFObject* value_object = NULL;
							try
							{
								while (true)
								{
									char ch = m_data_stream->get();
									throw_if (ch == EOF, "Unexpected EOF", errors::uninterpretable_data{});
									switch (ch)
									{
										case 'e':	//endobj
										{
											char buffer[5];
											throw_if (!m_data_stream->read(buffer, 5), "Unexpected EOF");
											throw_if (memcmp(buffer, "ndobj", 5) != 0, errors::uninterpretable_data{});
											if (!reference_info->m_object->m_object)
												reference_info->m_object->m_object = new PDFNull;
											throw_if (!m_data_stream->seekg(current_position, std::ios_base::beg), "std::istream::seekg", current_position);
											return reference_info->m_object->m_object;
										}
										case 's':	//stream
										{
											value_object = reference_info->m_object->m_object;
											reference_info->m_object->m_object = NULL;
											throw_if (!value_object || !value_object->isDictionary(), errors::uninterpretable_data{});
											reference_info->m_object->m_object = new PDFStream(*this, *value_object->getDictionary());
											value_object = NULL;
											m_data_stream->unget();
											readStream(*((PDFStream*)reference_info->m_object->m_object));
											break;
										}
										case '/':	//name
										{
											throw_if (reference_info->m_object->m_object,
												"Only one object allowed inside indirect object", errors::uninterpretable_data{});
											value_object = new PDFName;
											m_data_stream->unget();
											readName(*(PDFName*)value_object);
											reference_info->m_object->m_object = value_object;
											value_object = NULL;
											break;
										}
										case '<':	//hexadecimal string or dictionary
										{
											throw_if (reference_info->m_object->m_object,
												"Only one object allowed inside indirect object", errors::uninterpretable_data{});
											ch = m_data_stream->get();
											throw_if (ch == EOF, "Unexpected EOF", errors::uninterpretable_data{});
											if (ch == '<')	//dictionary
											{
												value_object = new PDFDictionary;
												m_data_stream->unget();
												m_data_stream->unget();
												readDictionary(*(PDFDictionary*)value_object);
												reference_info->m_object->m_object = value_object;
												value_object = NULL;
											}
											else	//hexadecimal string
											{
												value_object = new PDFString;
												m_data_stream->unget();
												m_data_stream->unget();
												readString(*(PDFString*)value_object);
												reference_info->m_object->m_object = value_object;
												value_object = NULL;
											}
											break;
										}
										case '(':	//value is a literal string
										{
											throw_if (reference_info->m_object->m_object,
												"Only one object allowed inside indirect object", errors::uninterpretable_data{});
											value_object = new PDFString;
											m_data_stream->unget();
											readString(*(PDFString*)value_object);
											reference_info->m_object->m_object = value_object;
											value_object = NULL;
											break;
										}
										case 'f':
										case 't':	//value is a boolean
										{
											throw_if (reference_info->m_object->m_object,
												"Only one object allowed inside indirect object", errors::uninterpretable_data{});
											value_object = new PDFBoolean;
											m_data_stream->unget();
											readBoolean(*(PDFBoolean*)value_object);
											reference_info->m_object->m_object = value_object;
											value_object = NULL;
											break;
										}
										case '[':	//value is an array
										{
											throw_if (reference_info->m_object->m_object,
												"Only one object allowed inside indirect object", errors::uninterpretable_data{});
											value_object = new PDFArray;
											m_data_stream->unget();
											readArray(*(PDFArray*)value_object);
											reference_info->m_object->m_object = value_object;
											value_object = NULL;
											break;
										}
										case 'n':	//value is a null
										{
											throw_if (reference_info->m_object->m_object,
												"Only one object allowed inside indirect object", errors::uninterpretable_data{});
											value_object = new PDFNull;
											m_data_stream->unget();
											readNull(*(PDFNull*)value_object);
											reference_info->m_object->m_object = value_object;
											value_object = NULL;
											break;
										}
										case '%':
										{
											skipComment();
											break;
										}
										case '+':
										case '-':
										case '.':
										case '0':
										case '1':
										case '2':
										case '3':
										case '4':
										case '5':
										case '6':
										case '7':
										case '8':
										case '9':	//value is a numeric
										{
											throw_if (reference_info->m_object->m_object,
												"Only one object allowed inside indirect object", errors::uninterpretable_data{});
											m_data_stream->unget();
											value_object = readNumeric();
											reference_info->m_object->m_object = value_object;
											value_object = NULL;
											break;
										}
									}
								}
							}
							catch (std::bad_alloc& ba)
							{
								if (value_object)
									delete value_object;
								value_object = NULL;
								throw;
							}
							catch (const std::exception& e)
							{
								if (value_object)
									delete value_object;
								value_object = NULL;
								throw;
							}
						}
						default:
						{
							throw make_error("Unexpected reference type", reference_info->m_type, errors::uninterpretable_data{});
						}
					}
				}
				catch (const std::exception& e)
				{
					std::throw_with_nested(make_error(index));
				}
			}

			inline std::istream& seek(int offset, std::ios_base::seekdir whence)
			{
				 return m_data_stream->seekg(offset, whence);
			}

			inline std::istream& read(char* buffer, std::streamsize n)
			{
				return m_data_stream->read(buffer, n);
			}

			inline std::streampos tell()
			{
				return m_data_stream->tellg();
			}

		private:
			void readReferenceData()
			{
				try
				{
					char start_xref_buffer[25];
					size_t xref_data_position;
					throw_if (!m_data_stream->seekg(-25, std::ios_base::end), "Error seeking to start xref position");
					throw_if (!m_data_stream->read(start_xref_buffer, 25), "Can't read start xref position");
					int index = 0;
					while (start_xref_buffer[index] > '9' || start_xref_buffer[index] < '0')
					{
						++index;
						throw_if (index == 25, errors::uninterpretable_data{});
					}
					bool backward_compatibility = false;
					std::set<size_t> start_xref_positions;
					xref_data_position = strtol(start_xref_buffer + index, NULL, 10);
					start_xref_positions.insert(xref_data_position);
					while (true)
					{
						throw_if (!m_data_stream->seekg(xref_data_position, std::ios_base::beg), "Error seeking to xref position");
						char ch = m_data_stream->get();
						throw_if (ch == EOF, "Unexpected EOF", errors::uninterpretable_data{});
						if (ch == 'x')	//xref line
						{
							//xref table
							std::string line;
							readLine(line);
							throw_if (line.length() < 3 || line.substr(0, 3) != "ref",
								errors::uninterpretable_data{});
							readXrefTable();
							m_trailer_dict.clearDictionary();
							readDictionary(m_trailer_dict);
							throw_if (m_trailer_dict.getObjAsReferenceCall("Encrypt"), errors::file_encrypted{});
							if (!m_got_root && m_trailer_dict.getObjAsReferenceCall("Root"))
							{
								m_got_root = true;
								*m_root_ref = *m_trailer_dict.getObjAsReferenceCall("Root");
							}
							if (!m_got_info && m_trailer_dict.getObjAsReferenceCall("Info"))
							{
								m_got_info = true;
								*m_info_ref = *m_trailer_dict.getObjAsReferenceCall("Info");
							}
							PDFNumericInteger* xref_stm = m_trailer_dict.getObjAsNumericInteger("XRefStm");
							if (xref_stm)
							{
								xref_data_position = (*xref_stm)();
								backward_compatibility = true;
							}
							else
							{
								PDFNumericInteger* prev = m_trailer_dict.getObjAsNumericInteger("Prev");
								if (prev)
									xref_data_position = (*prev)();
								else
									return;	//no more cross reference data
							}
							if (start_xref_positions.find(xref_data_position) != start_xref_positions.end())
								return;
							start_xref_positions.insert(xref_data_position);
						}
						else
						{
							//xref stream
							m_data_stream->unget();
							PDFNumericInteger* num_index = readNumeric()->getNumericInteger();
							throw_if (!num_index, "Error getting XRef stream index");
							size_t index = (*num_index)();
							delete num_index;
							//initialize obj:
							if (m_references.size() < index + 1)
								m_references.resize(index + 1);
							m_references[index].m_type = ReferenceInfo::in_use;
							m_references[index].m_offset = xref_data_position;
							m_references[index].m_read = true;
							PDFStream* xref_stream = readIndirectObject(index)->getStream();
							throw_if (!xref_stream, "Error getting XRef stream");
							readXRefStream(*xref_stream);
							throw_if (xref_stream->m_dictionary->getObjAsReferenceCall("Encrypt"), errors::file_encrypted{});
							if (!m_got_root && xref_stream->m_dictionary->getObjAsReferenceCall("Root"))
							{
								m_got_root = true;
								*m_root_ref = *xref_stream->m_dictionary->getObjAsReferenceCall("Root");
							}
							if (!m_got_info && xref_stream->m_dictionary->getObjAsReferenceCall("Info"))
							{
								m_got_info = true;
								*m_info_ref = *xref_stream->m_dictionary->getObjAsReferenceCall("Info");
							}
							PDFNumericInteger* prev = NULL;
							if (backward_compatibility)
								prev = m_trailer_dict.getObjAsNumericInteger("Prev");
							else
								prev = xref_stream->m_dictionary->getObjAsNumericInteger("Prev");
							if (prev)
								xref_data_position = (*prev)();
							else
								return;	//no more cross reference data
							if (start_xref_positions.find(xref_data_position) != start_xref_positions.end())
								return;
							start_xref_positions.insert(xref_data_position);
						}
					}
				}
				catch (const std::exception& e)
				{
					std::throw_with_nested(make_error("Error reading xref data"));
				}
			}

			void readXrefTable()
			{
				try
				{
					std::string line;
					line.reserve(256);
					char* ptr_start = NULL;
					char* ptr_end = NULL;
					ReferenceInfo* reference;
					do
					{
						readLine(line);
						ptr_start = (char*)line.c_str();
						ptr_end = ptr_start;
						if (ptr_start[0] != 't')	//trailer
						{
							size_t start = strtol(ptr_start, &ptr_end, 10);
							throw_if (start == 0 && ptr_start == ptr_end,
								"Conversion to long int failed", line, errors::uninterpretable_data{});
							ptr_start = ptr_end;
							size_t count = strtol(ptr_start, &ptr_end, 10);
							throw_if (count == 0 && ptr_start == ptr_end,
								"Conversion to long int failed", line, errors::uninterpretable_data{});
							if (start + count > m_references.size())
								m_references.resize(start + count);
							for (size_t i = 0; i < count; ++i)
							{
								readLine(line);
								throw_if (line.length() < 18, line.length(), errors::uninterpretable_data{});
								reference = &m_references[start + i];
								if (!reference->m_read)
								{
									ptr_start = (char*)line.c_str();
									ptr_end = ptr_start;
									reference->m_offset = strtol(ptr_start, &ptr_end, 10);
									throw_if (reference->m_offset == 0 && ptr_start == ptr_end,
										"Conversion to long int failed", line, errors::uninterpretable_data{});
									ptr_start = (char*)line.c_str() + 11;
									ptr_end = ptr_start;
									reference->m_generation = strtol(ptr_start, &ptr_end, 10);
									throw_if (reference->m_generation == 0 && ptr_start == ptr_end,
										"Conversion to long int failed", line, errors::uninterpretable_data{});
									if (line[17] == 'f')
										reference->m_type = ReferenceInfo::free;
									else
										reference->m_type = ReferenceInfo::in_use;
									reference->m_read = true;
								}
							}
						}
					}
					while (ptr_start[0] != 't');	//trailer
				}
				catch (const std::exception& e)
				{
					std::throw_with_nested(make_error("Error reading xref table"));
				}
			}

			void readXRefStream(PDFStream& stream)
			{
				try
				{
					size_t entries_count = 0;
					std::vector<int> start_positions;
					std::vector<int> sizes;
					size_t w_sizes[3];
					PDFNumericInteger* num_size = stream.m_dictionary->getObjAsNumericInteger("Size");
					throw_if (!num_size, "\"Size\" object not found in XRef stream dictionary");
					size_t size = (*num_size)();
					PDFArray* index_array = stream.m_dictionary->getObjAsArray("Index");
					if (index_array)
					{
						for (size_t i = 0; i < index_array->Size(); ++i)
						{
							PDFNumericInteger* element = index_array->getObjAsNumericInteger(i);
							if (element)
							{
								if (start_positions.size() == sizes.size())
								{
									start_positions.push_back((*element)());
								}
								else
								{
									sizes.push_back((*element)());
									entries_count += (*element)();
								}
							}
						}
					}
					else
					{
						start_positions.push_back(0);
						sizes.push_back(size);
					}
					throw_if (sizes.size() != start_positions.size(), errors::uninterpretable_data{});
					PDFArray* w_array = stream.m_dictionary->getObjAsArray("W");
					throw_if (!w_array || w_array->Size() != 3, errors::uninterpretable_data{});
					for (int i = 0; i < 3; ++i)
					{
						PDFNumericInteger* element = w_array->getObjAsNumericInteger(i);
						throw_if (!element, errors::uninterpretable_data{});
						w_sizes[i] = (*element)();
					}
					ReferenceInfo* reference;
					PDFStream::PDFStreamIterator iterator = stream.getIterator();
					iterator.backToRoot();
					const unsigned char* data = (const unsigned char*)iterator.getData() + 1;	//skip '[' character
					size_t record_size = w_sizes[0] + w_sizes[1] + w_sizes[2];
					throw_if (iterator.getDataLength() - 2 < record_size * entries_count,
						iterator.getDataLength() - 2, record_size * entries_count, errors::uninterpretable_data{});
					size_t read_index = 0;
					for (size_t i = 0; i < sizes.size(); ++i)
					{
						size_t elements_count = sizes[i];
						size_t element_start = start_positions[i];
						if (element_start + elements_count > m_references.size())
							m_references.resize(element_start + elements_count);
						for (size_t j = 0; j < elements_count; ++j)
						{
							reference = &m_references[element_start + j];
							if (reference->m_read)
							{
								read_index += record_size;
								continue;
							}
							reference->m_type = ReferenceInfo::in_use;		//default value
							for (int k = 0; k < w_sizes[0]; ++k)
							{
								if (data[read_index] > 2)
								{
									//invalid objct, mark as free
									reference->m_type = ReferenceInfo::free;
									++read_index;
								}
								else
									reference->m_type = (ReferenceInfo::ReferenceType)(data[read_index++]);	//0 -> free, 1 -> in_ise, 2 -> compressed
							}
							reference->m_offset = 0;
							for (int k = 0; k < w_sizes[1]; ++k)
							{
								reference->m_offset = reference->m_offset << 8;
								reference->m_offset += data[read_index++];
							}
							reference->m_generation = 0;
							for (int k = 0; k < w_sizes[2]; ++k)
							{
								reference->m_generation = reference->m_generation << 8;
								reference->m_generation += data[read_index++];
							}
							reference->m_read = true;
						}
					}
				}
				catch (const std::exception& e)
				{
					std::throw_with_nested(make_error("Error reading xref stream"));
				}
			}
	};

	struct PDFContent
	{
		//object responsible for fast character mapping, implemented as a tree.
		/*
		 *	Simple description:
		 *	First, read about CMaps in PDF reference (beginbfchar, begincidchar etc.)
		 *	For simplicity, I convert everything to the "range":
		 *
		 *	1 beginbfchar
		 *	<01> <20>
		 *	endbfchar
		 *
		 *	The code above I treat like:
		 *	1 beginbfrange
		 *	<01> <01> <20>
		 *	endbfrange
		 *	It simplifies the problem.
		 *
		 *	I have created this CMap structure to face main problem: iterating through thousands of lines
		 *	(beginbfrange, begincidrange etc.) for each character will slow down parsing file. So I have
		 *	decided to use some kind of tree algorithm. All ranges (beginbfrange, begincidrange) are
		 *	converted to the tree. Each node can have up to 16 childs (one for '0', one for '1', etc.).
		 *	So, consider an example:
		 *
		 *	beginbfrange
		 *	<8140> <8200> <10234>
		 *	<9220> <925F> <189AA>
		 *	endbfrange
		 *
		 *	I repeat, all "bfchar" are treated like "bfrange", the same is for "cid" (for simplicity)
		 *
		 *	As the result of the example above, the tree will look like:
		 *
		 *	root
		 *	|	\
		 *	8	 9
		 *	|\	 |
		 *	1 2  2
		 *	| |	 |\
		 *	4 0	 2 5
		 *	| |	 | |
		 *	0 0	 0 F
		 *	| |	 | |
		 *	d d	 d d
		 *
		 *	(where 'd' is a pointer to the NodeData)
		 *
		 *	Lets take some number: for example we have to convert number 8155 to the Unicode.
		 *
		 *	First digit is '8'. We look at the root and we can see that slot 8 is allocated. Then, we jump
		 *	to '8' node. We check if this node has 'NodeData'. It hasnt, so we should look further.
		 *	Next digit is 1. Node '8' has a child at index '1'. So, jump deeper to the next node.
		 *	Unfortunately, this node also has NULL pointer to the NodeData. We need to look deeper. Next digit is '5'.
		 *	But current node doesnt have slot '5'. We look on the left. There is a '4', so go to the '4'. Next digit
		 *	in the stream (number 8155) is '5', but this time we dont look for 5, because we have descended from the
		 *	path before (on the left). So we look for the child with the biggest index. It is '0' (only '0' remains). So we
		 *	jump to the '0'. This time we have a pointer to the NodeData. Using this data, we can check how to convert
		 *	8155 to the Unicode/CID. Parameters:
		 *	m_utf8 -> "\xF0\x90\x88\xB4"	(0x10234 in UTF8)
		 *	m_first_codepoint -> 0x10234
		 *	m_is_not_def -> false
		 *	m_min_range -> 0x8140
		 *	m_max_range -> 0x8200
		 *	0x8155 is a number in a proper range (min and max above). So we are in correct place. 0x8155 is different
		 *	than m_min_range, so we wont check m_utf8. We will use m_first_codepoint:
		 *	result = m_first_codepoint + (0x8155 - m_min_range)
		 *	result is a unichar already. We are done. We go back to the root. And now we can get next number from the stream.
		 *
		 *	Summing, we have fast enough method to obtain Unicode/CID values from the character code.
		 *
		 *	Note that in the tree above there are 4 pointers to the NodeData, but only two instances of them exist in fact.
		*/
		struct CMap
		{
			struct NodeData
			{
				std::string m_utf8;
				unsigned int m_max_range;
				unsigned int m_min_range;
				unsigned int m_first_codepoint;
				bool m_is_not_def;

				NodeData()
				{
					m_is_not_def = false;
					m_first_codepoint = 0;
				}
			};

			struct Node
			{
				Node** m_childs;
				NodeData* m_node_data;

				Node()
				{
					m_childs = NULL;
					m_node_data = NULL;
				}

				~Node()
				{
					if (m_childs)
					{
						for (int i = 0; i < 16; ++i)
							if (m_childs[i])
								delete m_childs[i];
						delete[] m_childs;
					}
				}
			};

			enum SearchState
			{
				equal,
				less,
				more
			};

			Node m_root;
			std::list<NodeData> m_node_datas;
			CMap* m_parent;
			bool m_ready;

			CMap()
			{
				m_parent = NULL;
				m_ready = false;
			}

			~CMap()
			{
				if (m_parent)
					delete m_parent;
			}

			void getCidString(const char* str, size_t len, std::string& cid_string)
			{
				Node* current_node = &m_root;
				unsigned int codepoint = 0;
				size_t codepoint_len = 0;
				SearchState state = equal;

				for (size_t i = 0; i < len; ++i)
				{
					int index = str[i];
					if (index <= '9')
						index -= '0';
					else
						index -= ('A' - 10);
					codepoint = codepoint << 4;
					codepoint += index;
					++codepoint_len;

					if (!current_node->m_childs)
					{
						if (m_parent)
							m_parent->getCidString(str + i + 1 - codepoint_len, codepoint_len, cid_string);
						current_node = &m_root;
						codepoint = 0;
						codepoint_len = 0;
						state = equal;
						continue;
					}
					switch (state)
					{
						case equal:
						{
							if (current_node->m_childs[index])
							{
								current_node = current_node->m_childs[index];
							}
							else
							{
								int left_index = index - 1;
								while (left_index >= 0)
								{
									if (current_node->m_childs[left_index])
									{
										current_node = current_node->m_childs[left_index];
										state = less;
										break;
									}
									--left_index;
								}
								if (state == equal)
								{
									int right_index = index + 1;
									while (right_index <= 15)
									{
										if (current_node->m_childs[right_index])
										{
											current_node = current_node->m_childs[right_index];
											state = more;
											break;
										}
										++right_index;
									}
								}
								if (state == equal)
								{
									if (m_parent)
										m_parent->getCidString(str + i + 1 - codepoint_len, codepoint_len, cid_string);
									current_node = &m_root;
									codepoint = 0;
									codepoint_len = 0;
									continue;
								}
							}
							break;
						}
						case less:
						{
							int left_index = 15;
							while (left_index >= 0)
							{
								if (current_node->m_childs[left_index])
								{
									current_node = current_node->m_childs[left_index];
									break;
								}
								--left_index;
							}
							if (left_index == -1)
							{
								if (m_parent)
									m_parent->getCidString(str + i + 1 - codepoint_len, codepoint_len, cid_string);
								current_node = &m_root;
								codepoint = 0;
								state = equal;
								codepoint_len = 0;
								continue;
							}
							break;
						}
						case more:
						{
							int right_index = 0;
							while (right_index <= 15)
							{
								if (current_node->m_childs[right_index])
								{
									current_node = current_node->m_childs[right_index];
									break;
								}
								++right_index;
							}
							if (right_index == 16)
							{
								if (m_parent)
									m_parent->getCidString(str + i + 1 - codepoint_len, codepoint_len, cid_string);
								current_node = &m_root;
								codepoint = 0;
								state = equal;
								codepoint_len = 0;
								continue;
							}
							break;
						}
					}

					if (current_node->m_node_data)
					{
						NodeData* data = current_node->m_node_data;
						if (codepoint <= data->m_max_range && codepoint >= data->m_min_range)
						{
							unsigned int res_code = data->m_first_codepoint;
							if (codepoint != data->m_min_range && !data->m_is_not_def)
								res_code += (codepoint - data->m_min_range);
							if (res_code <= 0xFF)
								cid_string += "00";	//each CIDs length must be 4.
							uint_to_hex_string(res_code, cid_string);
						}
						else if (m_parent)
							m_parent->getCidString(str + i + 1 - codepoint_len, codepoint_len, cid_string);
						state = equal;
						current_node = &m_root;
						codepoint = 0;
						codepoint_len = 0;
					}
				}
			}

			void addCodeRange(std::string& min, std::string& max, unsigned int first_code_point, const std::string& utf8, bool is_not_def)
			{
				Node* current_node = &m_root;
				m_node_datas.push_back(NodeData());
				NodeData* data = &(*m_node_datas.rbegin());

				data->m_first_codepoint = first_code_point;
				data->m_utf8 = utf8;
				data->m_is_not_def = is_not_def;

				unsigned int min_codepoint = 0;
				for (size_t i = 0; i < min.length(); ++i)
				{
					int index = min[i];
					if (index <= '9')
						index -= '0';
					else
						index -= ('A' - 10);
					min_codepoint = min_codepoint << 4;
					min_codepoint += index;
					if (!current_node->m_childs)
					{
						current_node->m_childs = new Node*[16];
						for (int i = 0; i < 16; ++i)
							current_node->m_childs[i] = NULL;
					}
					if (!current_node->m_childs[index])
						current_node->m_childs[index] = new Node;
					current_node = current_node->m_childs[index];
				}
				data->m_min_range = min_codepoint;
				current_node->m_node_data = data;

				if (min != max)
				{
					current_node = &m_root;
					unsigned int max_codepoint = 0;
					for (size_t i = 0; i < max.length(); ++i)
					{
						int index = max[i];
						if (index <= '9')
							index -= '0';
						else
							index -= ('A' - 10);
						max_codepoint = max_codepoint << 4;
						max_codepoint += index;
						if (!current_node->m_childs)
						{
							current_node->m_childs = new Node*[16];
							for (int i = 0; i < 16; ++i)
								current_node->m_childs[i] = NULL;
						}
						if (!current_node->m_childs[index])
							current_node->m_childs[index] = new Node;
						current_node = current_node->m_childs[index];
					}
					data->m_max_range = max_codepoint;
					current_node->m_node_data = data;
				}
				else
					data->m_max_range = min_codepoint;
			}

			bool parseNextCID(char* str, size_t str_len, unsigned int& cid_len, std::string& output, unsigned int& cid)
			{
				Node* current_node = &m_root;
				SearchState state = equal;

				cid_len = 0;
				if (str_len == 0)
					return true;
				for (size_t i = 0; i < str_len; ++i)
				{
					int index = str[i];
					if (index <= '9')
						index -= '0';
					else
						index -= ('A' - 10);
					cid = cid << 4;
					cid += index;
					++cid_len;

					if (!current_node->m_childs)
						return false;
					switch (state)
					{
						case equal:
						{
							if (current_node->m_childs[index])
								current_node = current_node->m_childs[index];
							else
							{
								int left_index = index - 1;
								while (left_index >= 0)
								{
									if (current_node->m_childs[left_index])
									{
										current_node = current_node->m_childs[left_index];
										state = less;
										break;
									}
									--left_index;
								}
								if (state == equal)
								{
									int right_index = index + 1;
									while (right_index <= 15)
									{
										if (current_node->m_childs[right_index])
										{
											current_node = current_node->m_childs[right_index];
											state = more;
											break;
										}
										++right_index;
									}
								}
								if (state == equal)
									return false;
							}
							break;
						}
						case less:
						{
							int left_index = 15;
							while (left_index >= 0)
							{
								if (current_node->m_childs[left_index])
								{
									current_node = current_node->m_childs[left_index];
									break;
								}
								--left_index;
							}
							if (left_index == -1)
								return false;
							break;
						}
						case more:
						{
							int right_index = 0;
							while (right_index <= 15)
							{
								if (current_node->m_childs[right_index])
								{
									current_node = current_node->m_childs[right_index];
									break;
								}
								++right_index;
							}
							if (right_index == 16)
								return false;
						}
					}
					if (current_node->m_node_data)
					{
						NodeData* data = current_node->m_node_data;
						if (cid <= data->m_max_range && cid >= data->m_min_range)
						{
							if (cid != data->m_min_range && !data->m_is_not_def)
								output += unicode_codepoint_to_utf8(data->m_first_codepoint + (cid - data->m_min_range));
							else
								output += data->m_utf8;
							return true;
						}
						else
							return false;
					}
				}
				return false;
			}
		};

		struct FontMetrics
		{
			enum FontWeight
			{
				medium,
				bold,
				roman
			};

			std::string m_font_name;
			std::string m_font_family;
			unsigned int m_first_char;
			unsigned int m_last_char;
			double m_descent;
			double m_font_bbox[4];
			FontWeight m_font_weight;
			double m_cap_height;
			unsigned int m_flags;
			double m_x_height;
			double m_italic_angle;
			double m_ascent;
			std::vector<unsigned int> m_widths;
			unsigned int m_missing_width;
			unsigned int m_leading;
			double m_vscale, m_hscale;
			double m_font_matrix[6];

			FontMetrics()
			{
				m_font_matrix[0] = 0.001;
				m_font_matrix[1] = 0.0;
				m_font_matrix[2] = 0.0;
				m_font_matrix[3] = 0.001;
				m_font_matrix[4] = 0.0;
				m_font_matrix[5] = 0.0;
				m_widths.reserve(256);
				m_descent = 0.0;
				m_first_char = 0;
				m_last_char = 0;
				m_font_bbox[0] = 0.0;
				m_font_bbox[1] = 0.0;
				m_font_bbox[2] = 0.0;
				m_font_bbox[3] = 0.0;
				m_font_weight = medium;
				m_cap_height = 0.0;
				m_flags = 0;
				m_x_height = 0.0;
				m_italic_angle = 0.0;
				m_ascent = 0.0;
				m_missing_width = 0;
				m_leading = 0;
				m_vscale = m_hscale = 0.001;
			}

			void log_to_record_stream(log_record_stream& s) const
			{
				s << docwire_log_streamable_obj(*this, m_font_name, m_font_family,
					//m_first_char)
					//m_last_char
					m_descent,
					//m_font_bbox
					//m_font_weight
					m_cap_height, m_flags, m_x_height, m_italic_angle, m_ascent,
					//m_widths
					m_missing_width, m_leading, m_vscale, m_hscale
					//m_font_matrix
				);
			}
		};

		/* Font metrics for the Adobe core 14 fonts.

		Font metrics are used to compute the boundary of each character
		written with a proportional font.

		The following data were extracted from the AFM files:

		  http://www.ctan.org/tex-archive/fonts/adobe/afm/

		*/

		//  BEGIN Verbatim copy of the license part
		//
		//
		// Adobe Core 35 AFM Files with 229 Glyph Entries - ReadMe
		//
		// This file and the 35 PostScript(R) AFM files it accompanies may be
		// used, copied, and distributed for any purpose and without charge,
		// with or without modification, provided that all copyright notices
		// are retained; that the AFM files are not distributed without this
		// file; that all modifications to this file or any of the AFM files
		// are prominently noted in the modified file(s); and that this
		// paragraph is not modified. Adobe Systems has no responsibility or
		// obligation to support the use of the AFM files.
		//
		//
		//  END Verbatim copy of the license part

		class FontMetricsMap : public std::map<std::string, FontMetrics>
		{
			public:
				FontMetricsMap()
				{
					FontMetrics* font_metrics = &((*this)["Courier-Oblique"]);
					font_metrics->m_font_name = "Courier-Oblique";
					font_metrics->m_descent = -194.0;
					font_metrics->m_font_bbox[0] = -49.0;
					font_metrics->m_font_bbox[1] = -249.0;
					font_metrics->m_font_bbox[2] = 749.0;
					font_metrics->m_font_bbox[3] = 803.0;
					font_metrics->m_font_weight = FontMetrics::medium;
					font_metrics->m_cap_height = 572.0;
					font_metrics->m_font_family = "Courier";
					font_metrics->m_flags = 64;
					font_metrics->m_x_height = 434.0;
					font_metrics->m_italic_angle = -11.0;
					font_metrics->m_ascent = 627.0;
					font_metrics->m_first_char = 0;
					font_metrics->m_last_char = 0;
					font_metrics->m_missing_width = 600;

					font_metrics = &((*this)["Times-BoldItalic"]);
					font_metrics->m_font_name = "Times-BoldItalic";
					font_metrics->m_descent = -217.0;
					font_metrics->m_font_bbox[0] = -200.0;
					font_metrics->m_font_bbox[1] = -218.0;
					font_metrics->m_font_bbox[2] = 996.0;
					font_metrics->m_font_bbox[3] = 921.0;
					font_metrics->m_font_weight = FontMetrics::bold;
					font_metrics->m_cap_height = 669.0;
					font_metrics->m_font_family = "Times";
					font_metrics->m_flags = 0;
					font_metrics->m_x_height = 462.0;
					font_metrics->m_italic_angle = -15.0;
					font_metrics->m_ascent = 683.0;
					font_metrics->m_first_char = 0;
					font_metrics->m_last_char = 255;
					for (size_t i = 0; i < 32; ++i)
						font_metrics->m_widths.push_back(0);
					unsigned int times_bold_italic_widths[] = {250, 389, 555, 500, 500, 833, 778, 333, 333, 333, 500, 570, 250, 333,
															 250, 278, 500, 500, 500, 500, 500, 500, 500, 500, 500, 500, 333, 333,
															 570, 570, 570, 500, 832, 667, 667, 667, 722, 667, 667, 722, 778, 389,
															 500, 667, 611, 889, 722, 722, 611, 722, 667, 556, 611, 722, 667, 889,
															 667, 611, 611, 333, 278, 333, 570, 500, 333, 500, 500, 444, 500, 444,
															 333, 500, 556, 278, 278, 500, 278, 778, 556, 500, 500, 500, 389, 389,
															 278, 556, 444, 667, 500, 444, 389, 348, 220, 348, 570, 0, 0, 0, 0, 0,
															 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
															 0, 0, 0, 0, 0, 0, 389, 500, 500, 167, 500, 500, 500, 500, 278, 500,
															 500, 333, 333, 556, 556, 0, 500, 500, 500, 250, 0, 500, 350, 333, 500,
															 500, 500, 1000, 1000, 0, 500, 0, 333, 333, 333, 333, 333, 333, 333,
															 333, 0, 333, 333, 0, 333, 333, 333, 1000, 0, 0, 0, 0, 0, 0, 0, 0, 0,
															 0, 0, 0, 0, 0, 0, 0, 944, 0, 266, 0, 0, 0, 0, 611, 722, 944, 300, 0,
															 0, 0, 0, 0, 722, 0, 0, 0, 278, 0, 0, 278, 500, 722, 500, 0, 0, 0};
					font_metrics->m_widths.insert(font_metrics->m_widths.end(), std::begin(times_bold_italic_widths), std::end(times_bold_italic_widths));

					font_metrics = &((*this)["Helvetica-Bold"]);
					font_metrics->m_font_name = "Helvetica-Bold";
					font_metrics->m_descent = -207.0;
					font_metrics->m_font_bbox[0] = -170.0;
					font_metrics->m_font_bbox[1] = -228.0;
					font_metrics->m_font_bbox[2] = 1003.0;
					font_metrics->m_font_bbox[3] = 962.0;
					font_metrics->m_font_weight = FontMetrics::bold;
					font_metrics->m_cap_height = 718.0;
					font_metrics->m_font_family = "Helvetica";
					font_metrics->m_flags = 0;
					font_metrics->m_x_height = 532.0;
					font_metrics->m_italic_angle = 0.0;
					font_metrics->m_ascent = 718.0;
					font_metrics->m_first_char = 0;
					font_metrics->m_last_char = 255;
					for (size_t i = 0; i < 32; ++i)
						font_metrics->m_widths.push_back(0);
					unsigned int helvetica_bold_widths[] = {278, 333, 474, 556, 556, 889, 722, 278, 333, 333, 389, 584, 278, 333,
															278, 278, 556, 556, 556, 556, 556, 556, 556, 556, 556, 556, 333, 333,
															584, 584, 584, 611, 975, 722, 722, 722, 722, 667, 611, 778, 722, 278,
															556, 722, 611, 833, 722, 778, 667, 778, 722, 667, 611, 722, 667, 944,
															667, 667, 611, 333, 278, 333, 584, 556, 278, 556, 611, 556, 611, 556,
															333, 611, 611, 278, 278, 556, 278, 889, 611, 611, 611, 611, 389, 556,
															333, 611, 556, 778, 556, 556, 500, 389, 280, 389, 584, 0, 0, 0, 0, 0,
															0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
															0, 0, 0, 0, 0, 0, 333, 556, 556, 167, 556, 556, 556, 556, 238, 500,
															556, 333, 333, 611, 611, 0, 556, 556, 556, 278, 0, 556, 350, 278, 500,
															500, 556, 1000, 1000, 0, 611, 0, 333, 333, 333, 333, 333, 333, 333,
															333, 0, 333, 333, 0, 333, 333, 333, 1000, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
															0, 0, 0, 0, 0, 0, 1000, 0, 370, 0, 0, 0, 0, 611, 778, 1000, 365, 0, 0,
															0, 0, 0, 889, 0, 0, 0, 278, 0, 0, 278, 611, 944, 611, 0, 0, 0, 0};
					font_metrics->m_widths.insert(font_metrics->m_widths.end(), std::begin(helvetica_bold_widths), std::end(helvetica_bold_widths));

					font_metrics = &((*this)["Courier"]);
					font_metrics->m_font_name = "Courier";
					font_metrics->m_descent = -194.0;
					font_metrics->m_font_bbox[0] = -6.0;
					font_metrics->m_font_bbox[1] = -249.0;
					font_metrics->m_font_bbox[2] = 639.0;
					font_metrics->m_font_bbox[3] = 803.0;
					font_metrics->m_font_weight = FontMetrics::medium;
					font_metrics->m_cap_height = 572.0;
					font_metrics->m_font_family = "Courier";
					font_metrics->m_flags = 64;
					font_metrics->m_x_height = 434.0;
					font_metrics->m_italic_angle = 0.0;
					font_metrics->m_ascent = 627.0;
					font_metrics->m_first_char = 0;
					font_metrics->m_last_char = 0;
					font_metrics->m_missing_width = 600;

					font_metrics = &((*this)["Courier-BoldOblique"]);
					font_metrics->m_font_name = "Courier-BoldOblique";
					font_metrics->m_descent = -194.0;
					font_metrics->m_font_bbox[0] = -49.0;
					font_metrics->m_font_bbox[1] = -249.0;
					font_metrics->m_font_bbox[2] = 758.0;
					font_metrics->m_font_bbox[3] = 811.0;
					font_metrics->m_font_weight = FontMetrics::bold;
					font_metrics->m_cap_height = 572.0;
					font_metrics->m_font_family = "Courier";
					font_metrics->m_flags = 64;
					font_metrics->m_x_height = 434.0;
					font_metrics->m_italic_angle = -11.0;
					font_metrics->m_ascent = 627.0;
					font_metrics->m_first_char = 0;
					font_metrics->m_last_char = 0;
					font_metrics->m_missing_width = 600;

					font_metrics = &((*this)["Times-Bold"]);
					font_metrics->m_font_name = "Times-Bold";
					font_metrics->m_descent = -217.0;
					font_metrics->m_font_bbox[0] = -168.0;
					font_metrics->m_font_bbox[1] = -218.0;
					font_metrics->m_font_bbox[2] = 1000.0;
					font_metrics->m_font_bbox[3] = 935.0;
					font_metrics->m_font_weight = FontMetrics::bold;
					font_metrics->m_cap_height = 676.0;
					font_metrics->m_font_family = "Times";
					font_metrics->m_flags = 0;
					font_metrics->m_x_height = 461.0;
					font_metrics->m_italic_angle = 0.0;
					font_metrics->m_ascent = 683.0;
					font_metrics->m_first_char = 0;
					font_metrics->m_last_char = 255;
					for (size_t i = 0; i < 32; ++i)
						font_metrics->m_widths.push_back(0);
					unsigned int times_bold_widths[] = {250, 333, 555, 500, 500, 1000, 833, 333, 333, 333, 500, 570, 250, 333, 250,
														278, 500, 500, 500, 500, 500, 500, 500, 500, 500, 500, 333, 333, 570, 570,
														570, 500, 930, 722, 667, 722, 722, 667, 611, 778, 778, 389, 500, 778, 667,
														944, 722, 778, 611, 778, 722, 556, 667, 722, 722, 1000, 722, 722, 667, 333,
														278, 333, 581, 500, 333, 500, 556, 444, 556, 444, 333, 500, 556, 278, 333,
														556, 278, 833, 556, 500, 556, 556, 444, 389, 333, 556, 500, 722, 500, 500,
														444, 394, 220, 394, 520, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
														0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 333, 500, 500, 167, 500,
														500, 500, 500, 278, 500, 500, 333, 333, 556, 556, 0, 500, 500, 500, 250, 0,
														540, 350, 333, 500, 500, 500, 1000, 1000, 0, 500, 0, 333, 333, 333, 333,
														333, 333, 333, 333, 0, 333, 333, 0, 333, 333, 333, 1000, 0, 0, 0, 0, 0, 0, 0,
														0, 0, 0, 0, 0, 0, 0, 0, 0, 1000, 0, 300, 0, 0, 0, 0, 667, 778, 1000, 330, 0,
														0, 0, 0, 0, 722, 0, 0, 0, 278, 0, 0, 278, 500, 722, 556, 0, 0, 0, 0};
					font_metrics->m_widths.insert(font_metrics->m_widths.end(), std::begin(times_bold_widths), std::end(times_bold_widths));

					font_metrics = &((*this)["Symbol"]);
					font_metrics->m_font_name = "Symbol";
					font_metrics->m_descent = 0.0;
					font_metrics->m_font_bbox[0] = -180.0;
					font_metrics->m_font_bbox[1] = -293.0;
					font_metrics->m_font_bbox[2] = 1090.0;
					font_metrics->m_font_bbox[3] = 1010.0;
					font_metrics->m_font_weight = FontMetrics::medium;
					font_metrics->m_cap_height = 676.0;
					font_metrics->m_font_family = "Symbol";
					font_metrics->m_flags = 0;
					font_metrics->m_x_height = 0.0;
					font_metrics->m_italic_angle = 0.0;
					font_metrics->m_ascent = 0.0;
					font_metrics->m_first_char = 0;
					font_metrics->m_last_char = 255;
					for (size_t i = 0; i < 32; ++i)
						font_metrics->m_widths.push_back(0);
					unsigned int symbol_widths[] = {250, 333, 713, 500, 549, 833, 778, 439, 333, 333, 500, 549, 250, 549, 250, 278, 500,
													500, 500, 500, 500, 500, 500, 500, 500, 500, 278, 278, 549, 549, 549, 444, 549, 722,
													667, 722, 612, 611, 763, 603, 722, 333, 631, 722, 686, 889, 722, 722, 768, 741, 556,
													592, 611, 690, 439, 768, 645, 795, 611, 333, 863, 333, 658, 500, 500, 631, 549, 549,
													494, 439, 521, 411, 603, 329, 603, 549, 549, 576, 521, 549, 549, 521, 549, 603, 439,
													576, 713, 686, 493, 686, 494, 480, 200, 480, 549, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
													0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 750, 620, 247, 549,
													167, 713, 500, 753, 753, 753, 753, 1042, 987, 603, 987, 603, 400, 549, 411, 549, 549,
													713, 494, 460, 549, 549, 549, 549, 1000, 603, 1000, 658, 823, 686, 795, 987, 768, 768,
													823, 768, 768, 713, 713, 713, 713, 713, 713, 713, 768, 713, 790, 790, 890, 823, 549,
													250, 713, 603, 603, 1042, 987, 603, 987, 603, 494, 329, 790, 790, 786, 713, 384,
													384, 384, 384, 384, 384, 494, 494, 494, 494, 329, 274, 0, 686, 686, 686, 384, 384,
													384, 384, 384, 384, 494, 494, 494, 0};
					font_metrics->m_widths.insert(font_metrics->m_widths.end(), std::begin(symbol_widths), std::end(symbol_widths));

					font_metrics = &((*this)["Helvetica"]);
					font_metrics->m_font_name = "Helvetica";
					font_metrics->m_descent = -207.0;
					font_metrics->m_font_bbox[0] = -166.0;
					font_metrics->m_font_bbox[1] = -225.0;
					font_metrics->m_font_bbox[2] = 1000.0;
					font_metrics->m_font_bbox[3] = 931.0;
					font_metrics->m_font_weight = FontMetrics::medium;
					font_metrics->m_cap_height = 718.0;
					font_metrics->m_font_family = "Helvetica";
					font_metrics->m_flags = 0;
					font_metrics->m_x_height = 523.0;
					font_metrics->m_italic_angle = 0.0;
					font_metrics->m_ascent = 718.0;
					font_metrics->m_first_char = 0;
					font_metrics->m_last_char = 255;
					for (size_t i = 0; i < 32; ++i)
						font_metrics->m_widths.push_back(0);
					unsigned int helvetica_widths[] = {278, 278, 355, 556, 556, 889, 667, 222, 333, 333, 389, 584, 278, 333, 278, 278,
													   556, 556, 556, 556, 556, 556, 556, 556, 556, 556, 278, 278, 584, 584, 584, 556,
													   1015, 667, 667, 722, 722, 667, 611, 778, 722, 278, 500, 667, 556, 833, 722, 778,
													   667, 778, 722, 667, 611, 722, 667, 944, 667, 667, 611, 278, 278, 278, 469, 556,
													   222, 556, 556, 500, 556, 556, 278, 556, 556, 222, 222, 500, 222, 833, 556, 556,
													   556, 556, 333, 500, 278, 556, 500, 722, 500, 500, 500, 334, 260, 334, 584, 0, 0,
													   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
													   0, 0, 0, 0, 0, 333, 556, 556, 167, 556, 556, 556, 556, 191, 333, 556, 333, 333,
													   500, 500, 0, 556, 556, 556, 278, 0, 537, 350, 222, 333, 333, 556, 1000, 1000, 0,
													   611, 0, 333, 333, 333, 333, 333, 333, 333, 333, 0, 333, 333, 0, 333, 333, 333,
													   1000, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1000, 0, 370, 0, 0, 0, 0,
													   556, 778, 1000, 365, 0, 0, 0, 0, 0, 889, 0, 0, 0, 278, 0, 0, 222, 611, 944, 611,
													   0, 0, 0, 0};
					font_metrics->m_widths.insert(font_metrics->m_widths.end(), std::begin(helvetica_widths), std::end(helvetica_widths));

					font_metrics = &((*this)["Helvetica-BoldOblique"]);
					font_metrics->m_font_name = "Helvetica-BoldOblique";
					font_metrics->m_descent = -207.0;
					font_metrics->m_font_bbox[0] = -175.0;
					font_metrics->m_font_bbox[1] = -228.0;
					font_metrics->m_font_bbox[2] = 1114.0;
					font_metrics->m_font_bbox[3] = 962.0;
					font_metrics->m_font_weight = FontMetrics::bold;
					font_metrics->m_cap_height = 718.0;
					font_metrics->m_font_family = "Helvetica";
					font_metrics->m_flags = 0;
					font_metrics->m_x_height = 532.0;
					font_metrics->m_italic_angle = -12.0;
					font_metrics->m_ascent = 718.0;
					font_metrics->m_first_char = 0;
					font_metrics->m_last_char = 255;
					for (size_t i = 0; i < 32; ++i)
						font_metrics->m_widths.push_back(0);
					unsigned int helvetica_bold_oblique_widths[] = {278, 333, 474, 556, 556, 889, 722, 278, 333, 333, 389, 584, 278, 333,
																  278, 278, 556, 556, 556, 556, 556, 556, 556, 556, 556, 556, 333, 333,
																  584, 584, 584, 611, 975, 722, 722, 722, 722, 667, 611, 778, 722, 278,
																  556, 722, 611, 833, 722, 778, 667, 778, 722, 667, 611, 722, 667, 944,
																  667, 667, 611, 333, 278, 333, 584, 556, 278, 556, 611, 556, 611, 556,
																  333, 611, 611, 278, 278, 556, 278, 889, 611, 611, 611, 611, 389, 556,
																  333, 611, 556, 778, 556, 556, 500, 389, 280, 389, 584, 0, 0, 0, 0, 0,
																  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
																  0, 0, 0, 0, 0, 0, 333, 556, 556, 167, 556, 556, 556, 556, 238, 500,
																  556, 333, 333, 611, 611, 0, 556, 556, 556, 278, 0, 556, 350, 278, 500,
																  500, 556, 1000, 1000, 0, 611, 0, 333, 333, 333, 333, 333, 333, 333, 333,
																  0, 333, 333, 0, 333, 333, 333, 1000, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
																  0, 0, 0, 0, 1000, 0, 370, 0, 0, 0, 0, 611, 778, 1000, 365, 0, 0, 0, 0,
																  0, 889, 0, 0, 0, 278, 0, 0, 278, 611, 944, 611, 0, 0, 0, 0};
					font_metrics->m_widths.insert(font_metrics->m_widths.end(), std::begin(helvetica_bold_oblique_widths), std::end(helvetica_bold_oblique_widths));

					font_metrics = &((*this)["ZapfDingbats"]);
					font_metrics->m_font_name = "ZapfDingbats";
					font_metrics->m_descent = 0.0;
					font_metrics->m_font_bbox[0] = -1.0;
					font_metrics->m_font_bbox[1] = -143.0;
					font_metrics->m_font_bbox[2] = 981.0;
					font_metrics->m_font_bbox[3] = 820.0;
					font_metrics->m_font_weight = FontMetrics::medium;
					font_metrics->m_cap_height = 718.0;
					font_metrics->m_font_family = "ITC";
					font_metrics->m_flags = 0;
					font_metrics->m_x_height = 0.0;
					font_metrics->m_italic_angle = 0.0;
					font_metrics->m_ascent = 0.0;
					font_metrics->m_first_char = 0;
					font_metrics->m_last_char = 255;
					for (size_t i = 0; i < 32; ++i)
						font_metrics->m_widths.push_back(0);
					unsigned int zapf_dingbats_widths[] = {278, 974, 961, 974, 980, 719, 789, 790, 791, 690, 960, 939, 549, 855, 911, 933,
														 911, 945, 974, 755, 846, 762, 761, 571, 677, 763, 760, 759, 754, 494, 552, 537,
														 577, 692, 786, 788, 788, 790, 793, 794, 816, 823, 789, 841, 823, 833, 816, 831,
														 923, 744, 723, 749, 790, 792, 695, 776, 768, 792, 759, 707, 708, 682, 701, 826,
														 815, 789, 789, 707, 687, 696, 689, 786, 787, 713, 791, 785, 791, 873, 761, 762,
														 762, 759, 759, 892, 892, 788, 784, 438, 138, 277, 415, 392, 392, 668, 668, 0,
														 390, 390, 317, 317, 276, 276, 509, 509, 410, 410, 234, 234, 334, 334, 0, 0, 0,
														 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 732, 544, 544, 910, 667, 760, 760,
														 776, 595, 694, 626, 788, 788, 788, 788, 788, 788, 788, 788, 788, 788, 788, 788,
														 788, 788, 788, 788, 788, 788, 788, 788, 788, 788, 788, 788, 788, 788, 788, 788,
														 788, 788, 788, 788, 788, 788, 788, 788, 788, 788, 788, 788, 894, 838, 1016, 458,
														 748, 924, 748, 918, 927, 928, 928, 834, 873, 828, 924, 924, 917, 930, 931, 463,
														 883, 836, 836, 867, 867, 696, 696, 874, 0, 874, 760, 946, 771, 865, 771, 888, 967,
														 888, 831, 873, 927, 970, 918, 0};
					font_metrics->m_widths.insert(font_metrics->m_widths.end(), std::begin(zapf_dingbats_widths), std::end(zapf_dingbats_widths));

					font_metrics = &((*this)["Courier-Bold"]);
					font_metrics->m_font_name = "Courier-Bold";
					font_metrics->m_descent = -194.0;
					font_metrics->m_font_bbox[0] = -88.0;
					font_metrics->m_font_bbox[1] = -249.0;
					font_metrics->m_font_bbox[2] = 697.0;
					font_metrics->m_font_bbox[3] = 811.0;
					font_metrics->m_font_weight = FontMetrics::bold;
					font_metrics->m_cap_height = 572.0;
					font_metrics->m_font_family = "Courier";
					font_metrics->m_flags = 64;
					font_metrics->m_x_height = 434.0;
					font_metrics->m_italic_angle = 0.0;
					font_metrics->m_ascent = 627.0;
					font_metrics->m_first_char = 0;
					font_metrics->m_last_char = 0;
					font_metrics->m_missing_width = 600;

					font_metrics = &((*this)["Times-Italic"]);
					font_metrics->m_font_name = "Times-Italic";
					font_metrics->m_descent = -217.0;
					font_metrics->m_font_bbox[0] = -169.0;
					font_metrics->m_font_bbox[1] = -217.0;
					font_metrics->m_font_bbox[2] = 1010.0;
					font_metrics->m_font_bbox[3] = 883.0;
					font_metrics->m_font_weight = FontMetrics::medium;
					font_metrics->m_cap_height = 653.0;
					font_metrics->m_font_family = "Times";
					font_metrics->m_flags = 0;
					font_metrics->m_x_height = 441.0;
					font_metrics->m_italic_angle = -15.5;
					font_metrics->m_ascent = 683.0;
					font_metrics->m_first_char = 0;
					font_metrics->m_last_char = 255;
					for (size_t i = 0; i < 32; ++i)
						font_metrics->m_widths.push_back(0);
					unsigned int times_italic_widths[] = {250, 333, 420, 500, 500, 833, 778, 333, 333, 333, 500, 675, 250, 333, 250,
														  278, 500, 500, 500, 500, 500, 500, 500, 500, 500, 500, 333, 333, 675, 675,
														  675, 500, 920, 611, 611, 667, 722, 611, 611, 722, 722, 333, 444, 667, 556,
														  833, 667, 722, 611, 722, 611, 500, 556, 722, 611, 833, 611, 556, 556, 389,
														  278, 389, 422, 500, 333, 500, 500, 444, 500, 444, 278, 500, 500, 278, 278,
														  444, 278, 722, 500, 500, 500, 500, 389, 389, 278, 500, 444, 667, 444, 444,
														  389, 400, 275, 400, 541, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
														  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 389, 500, 500, 167, 500,
														  500, 500, 500, 214, 556, 500, 333, 333, 500, 500, 0, 500, 500, 500, 250, 0,
														  523, 350, 333, 556, 556, 500, 889, 1000, 0, 500, 0, 333, 333, 333, 333, 333,
														  333, 333, 333, 0, 333, 333, 0, 333, 333, 333, 889, 0, 0, 0, 0, 0, 0, 0, 0,
														  0, 0, 0, 0, 0, 0, 0, 0, 889, 0, 276, 0, 0, 0, 0, 556, 722, 944, 310, 0, 0,
														  0, 0, 0, 667, 0, 0, 0, 278, 0, 0, 278, 500, 667, 500, 0, 0, 0, 0};
					font_metrics->m_widths.insert(font_metrics->m_widths.end(), std::begin(times_italic_widths), std::end(times_italic_widths));

					font_metrics = &((*this)["Times-Roman"]);
					font_metrics->m_font_name = "Times-Roman";
					font_metrics->m_descent = -217.0;
					font_metrics->m_font_bbox[0] = -168.0;
					font_metrics->m_font_bbox[1] = -218.0;
					font_metrics->m_font_bbox[2] = 1000.0;
					font_metrics->m_font_bbox[3] = 898.0;
					font_metrics->m_font_weight = FontMetrics::roman;
					font_metrics->m_cap_height = 662.0;
					font_metrics->m_font_family = "Times";
					font_metrics->m_flags = 0;
					font_metrics->m_x_height = 450.0;
					font_metrics->m_italic_angle = 0.0;
					font_metrics->m_ascent = 683.0;
					font_metrics->m_first_char = 0;
					font_metrics->m_last_char = 255;
					for (size_t i = 0; i < 32; ++i)
						font_metrics->m_widths.push_back(0);
					unsigned int times_roman_widths[] = {250, 333, 408, 500, 500, 833, 778, 333, 333, 333, 500, 564, 250, 333, 250, 278,
													   500, 500, 500, 500, 500, 500, 500, 500, 500, 500, 278, 278, 564, 564, 564, 444,
													   921, 722, 667, 667, 722, 611, 556, 722, 722, 333, 389, 722, 611, 889, 722, 722,
													   556, 722, 667, 556, 611, 722, 722, 944, 722, 722, 611, 333, 278, 333, 469, 500,
													   333, 444, 500, 444, 500, 444, 333, 500, 500, 278, 278, 500, 278, 778, 500, 500,
													   500, 500, 333, 389, 278, 500, 500, 722, 500, 500, 444, 480, 200, 480, 541, 0, 0,
													   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
													   0, 0, 0, 0, 0, 333, 500, 500, 167, 500, 500, 500, 500, 180, 444, 500, 333, 333,
													   556, 556, 0, 500, 500, 500, 250, 0, 453, 350, 333, 444, 444, 500, 1000, 1000, 444,
													   0, 333, 333, 333, 333, 333, 333, 333, 333, 0, 333, 333, 0, 333, 333, 333, 1000,
													   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 889, 0, 276, 0, 0, 0, 0, 611, 722,
													   889, 310, 0, 0, 0, 0, 0, 667, 0, 0, 0, 278, 0, 0, 278, 500, 722, 500, 0, 0, 0, 0};
					font_metrics->m_widths.insert(font_metrics->m_widths.end(), std::begin(times_roman_widths), std::end(times_roman_widths));

					font_metrics = &((*this)["Helvetica-Oblique"]);
					font_metrics->m_font_name = "Helvetica-Oblique";
					font_metrics->m_descent = -207.0;
					font_metrics->m_font_bbox[0] = -171.0;
					font_metrics->m_font_bbox[1] = -225.0;
					font_metrics->m_font_bbox[2] = 1116.0;
					font_metrics->m_font_bbox[3] = 931.0;
					font_metrics->m_font_weight = FontMetrics::medium;
					font_metrics->m_cap_height = 718.0;
					font_metrics->m_font_family = "Helvetica";
					font_metrics->m_flags = 0;
					font_metrics->m_x_height = 523.0;
					font_metrics->m_italic_angle = -12.0;
					font_metrics->m_ascent = 718.0;
					font_metrics->m_first_char = 0;
					font_metrics->m_last_char = 255;
					for (size_t i = 0; i < 32; ++i)
						font_metrics->m_widths.push_back(0);
					unsigned int helvetica_oblique_widths[] = {278, 278, 355, 556, 556, 889, 667, 222, 333, 333, 389, 584, 278, 333, 278,
															   278, 556, 556, 556, 556, 556, 556, 556, 556, 556, 556, 278, 278, 584, 584,
															   584, 556, 1015, 667, 667, 722, 722, 667, 611, 778, 722, 278, 500, 667, 556,
															   833, 722, 778, 667, 778, 722, 667, 611, 722, 667, 944, 667, 667, 611, 278,
															   278, 278, 469, 556, 222, 556, 556, 500, 556, 556, 278, 556, 556, 222, 222,
															   500, 222, 833, 556, 556, 556, 556, 333, 500, 278, 556, 500, 722, 500, 500,
															   500, 334, 260, 334, 584, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
															   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 333, 556, 556, 167, 556,
															   556, 556, 556, 191, 333, 556, 333, 333, 500, 500, 0, 556, 556, 556, 278, 0,
															   537, 350, 222, 333, 333, 556, 1000, 1000, 0, 611, 0, 333, 333, 333, 333, 333,
															   333, 333, 333, 0, 333, 333, 0, 333, 333, 333, 1000, 0, 0, 0, 0, 0, 0, 0, 0,
															   0, 0, 0, 0, 0, 0, 0, 0, 1000, 0, 370, 0, 0, 0, 0, 556, 778, 1000, 365, 0, 0,
															   0, 0, 0, 889, 0, 0, 0, 278, 0, 0, 222, 611, 944, 611, 0, 0, 0, 0};
					font_metrics->m_widths.insert(font_metrics->m_widths.end(), std::begin(helvetica_oblique_widths), std::end(helvetica_oblique_widths));
				}
		};

		static FontMetricsMap pdf_font_metrics_map;

		struct Font
		{
			const PoDoFo::PdfDictionary* m_font_dictionary;
			std::string m_font_encoding;
			bool m_predefined_simple_encoding;
			bool m_predefined_cmap;
			CMap m_cmap;
			CMap m_to_cid_cmap;
			unsigned int* m_simple_encoding_table;
			bool m_own_simple_encoding_table;
			std::string m_font_type;
			std::string m_base_font;
			FontMetrics m_font_metrics;
			bool m_multibyte;
			const PoDoFo::PdfDictionary* m_font_descriptor;
			char* m_cid_begin;
			size_t m_cid_len;

			Font()
			{
				m_multibyte = false;
				m_font_encoding = "unknown";
				m_font_dictionary = NULL;
				m_predefined_cmap = false;
				m_predefined_simple_encoding = false;
				m_simple_encoding_table = NULL;
				m_own_simple_encoding_table = false;
				m_font_descriptor = NULL;
				m_cid_begin = NULL;
				m_cid_len = 0;
			}

			~Font()
			{
				if (m_own_simple_encoding_table && m_simple_encoding_table)
					delete[] m_simple_encoding_table;
			}

			double getWidth(unsigned int cid)
			{
				if (cid < m_font_metrics.m_first_char)
					return m_font_metrics.m_missing_width * m_font_metrics.m_hscale;
				cid -= m_font_metrics.m_first_char;
				if (cid >= m_font_metrics.m_widths.size())
					return m_font_metrics.m_missing_width * m_font_metrics.m_hscale;
				return m_font_metrics.m_widths[cid] * m_font_metrics.m_hscale;
			}

			double getHeight()
			{
				double height = m_font_metrics.m_font_bbox[3] == m_font_metrics.m_font_bbox[1] ?
								m_font_metrics.m_ascent - m_font_metrics.m_descent : m_font_metrics.m_font_bbox[3] - m_font_metrics.m_font_bbox[1];
				return height * m_font_metrics.m_vscale;
			}

			double getDescent()
			{
				return m_font_metrics.m_descent * m_font_metrics.m_vscale;
			}

			unsigned int getNextCIDandDecode(std::string& output)
			{
				unsigned int cid = 0;
				bool parsed_cid = false;
				if (m_cmap.m_ready)
				{
					unsigned int cid_len;
					parsed_cid = m_cmap.parseNextCID(m_cid_begin, m_cid_len, cid_len, output, cid);
					if (parsed_cid)
					{
						m_cid_begin += cid_len;
						m_cid_len -= cid_len;
					}
				}
				if (m_predefined_simple_encoding && !parsed_cid)
				{
					cid = hex_char_to_single_char(m_cid_begin);
					output += utf8_codepoint_to_utf8(m_simple_encoding_table[cid]);
					m_cid_begin += 2;
					m_cid_len -= 2;
					parsed_cid = true;
				}
				if (!parsed_cid)
				{
					cid = *m_cid_begin;
					output += *m_cid_begin;
					m_cid_len--;
					m_cid_begin++;
				}
				return cid;
			}

			bool hasNextCid()
			{
				return m_cid_len > 0;
			}

			void setCidString(const std::string& cid_str)
			{
				m_cid_begin = (char*)cid_str.c_str();
				m_cid_len = cid_str.length();
			}

			void convertToCidString(std::string& str)
			{
				if (m_predefined_cmap)
				{
					std::string cid_string;
					m_to_cid_cmap.getCidString(str.c_str(), str.length(), cid_string);
					str = cid_string;
				}
			}

			void log_to_record_stream(log_record_stream& s) const
			{
				s << docwire_log_streamable_obj(*this,
					//m_font_dictionary
					m_font_encoding, m_predefined_simple_encoding, m_predefined_cmap,
					//m_cmap, m_to_cid_cmap, m_simple_encoding_table
					m_own_simple_encoding_table, m_font_type, m_base_font, m_font_metrics, m_multibyte);
					//m_font_descriptor, m_cid_begin, m_cid_len
			}
		};

		struct TJArrayElement
		{
			bool m_is_number;
			std::string m_text;
			std::string m_utf_text;
			PoDoFo::PdfString m_pdf_string;
			double m_value;

			void log_to_record_stream(log_record_stream& s) const
			{
				s << docwire_log_streamable_obj(*this, m_is_number);
				if (m_is_number)
					s << docwire_log_streamable_obj(*this, m_is_number, m_value);
				else
					s << docwire_log_streamable_obj(*this, m_is_number, m_text, m_utf_text, m_pdf_string);
			}
		};

		struct TransformationMatrix
		{
			double m_scale_x;
			double m_shear_x;
			double m_shear_y;
			double m_scale_y;
			double m_offset_x;
			double m_offset_y;

			TransformationMatrix()
				: m_scale_x(1.0),
				m_shear_x(0.0),
				m_shear_y(0.0),
				m_scale_y(1.0),
				m_offset_x(0.0),
				m_offset_y(0.0)
			{
			}

			TransformationMatrix(const std::vector<double>& args)
				: m_scale_x(args[0]),
				m_shear_x(args[1]),
				m_shear_y(args[2]),
				m_scale_y(args[3]),
				m_offset_x(args[4]),
				m_offset_y(args[5])
			{
			}

			const TransformationMatrix combinedWith(const TransformationMatrix& matrix) const
			{
				TransformationMatrix result;
				result.m_scale_x = m_scale_x * matrix.m_scale_x + m_shear_y * matrix.m_shear_x;
				result.m_shear_x = m_shear_x * matrix.m_scale_x + m_scale_y * matrix.m_shear_x;
				result.m_shear_y = m_scale_x * matrix.m_shear_y + m_shear_y * matrix.m_scale_y;
				result.m_scale_y = m_shear_x * matrix.m_shear_y + m_scale_y * matrix.m_scale_y;
				result.m_offset_x = m_offset_x + m_scale_x * matrix.m_offset_x + m_shear_y * matrix.m_offset_y;
				result.m_offset_y = m_offset_y + m_shear_x * m_offset_x + m_scale_y * matrix.m_offset_y;
				return result;
			}

			double transformX(double x, double y)
			{
				return m_offset_x + m_scale_x * x + m_shear_y * y;
			}

			double transformY(double x, double y)
			{
				return m_offset_y + m_shear_x * m_offset_x + m_scale_y * y;
			}
		};

		struct PageText
		{
			struct TextState
			{
				TransformationMatrix m_ctm;
				TransformationMatrix m_matrix;
				TransformationMatrix m_line_matrix;
				double m_font_size;
				double m_scaling;
				double m_leading;
				double m_rise;
				double m_word_space;
				double m_char_space;

				TextState()
				{
					reset();
				}

				void reset()
				{
					m_ctm = TransformationMatrix();
					m_font_size = 0.0;
					m_scaling = 100.0;
					m_leading = 0.0;
					m_matrix = TransformationMatrix();
					m_line_matrix = TransformationMatrix();
					m_word_space = 0.0;
					m_char_space = 0.0;
					m_rise = 0.0;
				}

				void log_to_record_stream(log_record_stream& s) const
				{
					s << docwire_log_streamable_obj(*this, /*m_ctm, m_matrix, m_line_matrix,*/ m_font_size, m_scaling, m_leading, m_rise, m_word_space, m_char_space);
				}
			};

			struct TextElement
			{
				std::string m_text;
				double m_x, m_y, m_width, m_height;
				double m_space_size;

				TextElement(double x, double y, double w, double h, double space_size, const std::string& text)
				{
					docwire_log_func_with_args(x, y, w, h, space_size, text);
					// warning TODO: We have position and size for each string. We can use those values to improve parser
					m_x = correctSize(x);
					m_y = correctSize(y);
					m_text = text;
					m_width = correctSize(w);
					m_height = correctSize(h);
					m_space_size = space_size;
				}

				double correctSize(double value)
				{
					//file may be corrupted, we should set some maximum values.
					// warning TODO: Check MediaBox entry (defines page area)
					if (value < 0)
						value = 0.0;
					if (value > 5000)
						value = 5000;
					return value;
				}

				bool operator == (const TextElement& compared) const
				{
					return compared.m_y == m_y && compared.m_x == m_x;
				}

				bool operator < (const TextElement& compared) const
				{
					if (abs(int(m_y - compared.m_y)) > 4.0)	//tolerace
					{
						return m_y > compared.m_y;
					}
					return m_x < compared.m_x;
				}

				bool operator > (const TextElement& compared) const
				{
					if (abs(int(m_y - compared.m_y)) > 4.0) //tolerace
					{
						return m_y < compared.m_y;
					}
					return m_x > compared.m_x;
				}

				void log_to_record_stream(log_record_stream& s) const
				{
					s << docwire_log_streamable_obj(*this, m_text, m_x, m_y, m_width, m_height, m_space_size);
				}
			};

			Font* m_font;
			//PoDoFo::PdfTextState m_state;
			std::list<TextState> m_text_states;
			TextState m_current_state;
			std::multiset<TextElement> m_text_elements;

			void reset()
			{
				docwire_log_func();
				m_font = NULL;
				m_text_states.clear();
				m_current_state.reset();
			}

			void pushState()
			{
				docwire_log_func();
				m_text_states.push_back(m_current_state);
			}

			void popState()
			{
				docwire_log_func();
				if (m_text_states.size() > 0)
				{
					m_current_state = m_text_states.back();
					m_text_states.pop_back();
				}
			}

			void executeTm(const std::vector<double>& args)
			{
				docwire_log_func_with_args(args);
				m_current_state.m_matrix = TransformationMatrix(args);
				m_current_state.m_line_matrix = TransformationMatrix();
			}

			void executeTs(const std::vector<double>& args)
			{
				docwire_log_func_with_args(args);
				m_current_state.m_rise = args[0];
			}

			void executeTc(const std::vector<double>& args)
			{
				docwire_log_func_with_args(args);
				m_current_state.m_char_space = args[0];
			}

			void executeTw(const std::vector<double>& args)
			{
				docwire_log_func_with_args(args);
				m_current_state.m_word_space = args[0];
			}

			void executeTd(const std::vector<double>& args)
			{
				docwire_log_func_with_args(args);
				m_current_state.m_matrix.m_offset_x += args[0] * m_current_state.m_matrix.m_scale_x + args[1] * m_current_state.m_matrix.m_shear_y;
				m_current_state.m_matrix.m_offset_y += args[0] * m_current_state.m_matrix.m_shear_x + args[1] * m_current_state.m_matrix.m_scale_y;
				m_current_state.m_line_matrix = TransformationMatrix();
			}

			void executeTD(const std::vector<double>& args)
			{
				docwire_log_func_with_args(args);
				executeTd(args);
				m_current_state.m_leading = args[1];
			}

			void executeTstar()
			{
				docwire_log_func();
				executeTd(std::vector<double>{ 0, m_current_state.m_leading });
			}

			void executeTf(double font_size, Font& font)
			{
				docwire_log_func_with_args(font_size, font);
				m_current_state.m_font_size = font_size;
				m_font = &font;
			}

			void executeTL(const std::vector<double>& args)
			{
				docwire_log_func_with_args(args);
				m_current_state.m_leading = -args[0];
			}

			void executeTZ(double scale)
			{
				docwire_log_func_with_args(scale);
				m_current_state.m_scaling = scale;
			}

			void executeQuote(const std::string& str, const PoDoFo::PdfFont* pCurFont, double curFontSize)
			{
				docwire_log_func_with_args(str, pCurFont);
				executeTstar();
				executeTj(str, pCurFont, curFontSize);
			}

			void executeDoubleQuote(const std::string& str, std::vector<double> args, const PoDoFo::PdfFont* pCurFont, double curFontSize)
			{
				docwire_log_func_with_args(str, args, pCurFont);
				executeTw(args);
				args[0] = args[1];
				args.pop_back();
				executeTc(args);
				executeTj(str, pCurFont, curFontSize);
			}

			void executeCm(const std::vector<double>& args)
			{
				docwire_log_func_with_args(args);
				m_current_state.m_ctm = m_current_state.m_ctm.combinedWith(TransformationMatrix(args));
			}

			void executeBT()
			{
				docwire_log_func();
				m_current_state.m_matrix = TransformationMatrix();
				m_current_state.m_line_matrix = TransformationMatrix();
			}

			std::u32string utf8_to_utf32(const std::string& str)
			{
				docwire_log_func_with_args(str);
				std::wstring_convert<std::codecvt_utf8<char32_t>, char32_t> converter;
				return converter.from_bytes(str);
			}

			std::string utf32_to_utf8(char32_t ch)
			{
				docwire_log_func();
				std::wstring_convert<std::codecvt_utf8<char32_t>, char32_t> converter;
				return converter.to_bytes(ch);
			}

			double
			charWidth(const std::u32string& u32_str, const PoDoFo::PdfFont &font, double curFontSize, unsigned int idx)
			{
				docwire_log_func_with_args(u32_str, font, curFontSize, idx);
				throw_if (idx > u32_str.size(), idx, u32_str.size(), errors::program_logic{});
				char32_t ch = u32_str[idx];
				std::string ch_s = utf32_to_utf8(ch);
				docwire_log_vars(ch, ch_s);
				PoDoFo::PdfTextState text_state;
				text_state.FontSize = curFontSize;
				return font.GetStringLength(ch_s, text_state);
			}

			void executeTJ(std::vector<TJArrayElement>& tj_array, const PoDoFo::PdfFont* pCurFont, double curFontSize)
			{
				docwire_log_func_with_args(tj_array, pCurFont, curFontSize);
				if (!m_font)
					return;
				docwire_log_var(m_current_state);
				TransformationMatrix tmp_matrix, cid_matrix;
				cid_matrix = tmp_matrix = m_current_state.m_ctm.combinedWith(m_current_state.m_matrix);
				double scale = m_current_state.m_scaling / 100.0;
				double x_scale = (m_current_state.m_font_size * scale) / 1000.0;
				double char_space = m_current_state.m_char_space * scale;
				double word_space = m_font->m_multibyte ? 0 : m_current_state.m_word_space * scale;
				docwire_log_vars(scale, x_scale, char_space, word_space);

				bool add_charspace = false;
				double str_width = 0.0, str_height = 0.0;
				double x_pos = 0.0, y_pos = 0.0;
				std::string output;
				bool first = true, last = false;
				double space_size = 1.5;	//default space size

				for (size_t i = 0; i < tj_array.size(); ++i)
				{
					docwire_log_var(i);
					if (tj_array[i].m_is_number)
					{
						docwire_log(debug) << "Processing TJ char space" << docwire_log_streamable_var(tj_array[i].m_value);
						double distance = (-tj_array[i].m_value * x_scale);
						m_current_state.m_line_matrix.m_offset_x += distance;
						docwire_log_vars(distance, space_size);
						if (distance >= space_size)
						{
							docwire_log(debug) << "Adding space to output because distance >= space_size" << docwire_log_streamable_vars(distance, space_size);
							output += ' ';
						}
						add_charspace = true;
					}
					else
					{
						docwire_log(debug) << "Processing TJ text" << docwire_log_streamable_var(tj_array[i].m_utf_text);
						int idx = 0;
						std::u32string u32_text = utf8_to_utf32(tj_array[i].m_utf_text);
						for (const auto &c : u32_text)
						{
							docwire_log_vars(c, first);
							output += utf32_to_utf8(c);
							if (add_charspace)
								m_current_state.m_line_matrix.m_offset_x += char_space;

							//update matrix for cid
							cid_matrix = tmp_matrix.combinedWith(m_current_state.m_line_matrix);

							//get character size
							double cid_width = charWidth(u32_text, *pCurFont, curFontSize, idx);
							docwire_log_var(cid_width);
							++idx;
							double advance = cid_width;

							//calculate bounding box
							double tmp_y = m_current_state.m_rise + pCurFont->GetMetrics().GetDescent() * curFontSize;
							double text_height = pCurFont->GetMetrics().GetLineSpacing() * curFontSize;
							docwire_log_vars(tmp_y, text_height);
							double x0 = cid_matrix.transformX(0, tmp_y);
							double y0 = cid_matrix.transformY(0, tmp_y);
							double x1 = cid_matrix.transformX(advance, tmp_y + text_height);
							double y1 = cid_matrix.transformY(advance, tmp_y + text_height);
							docwire_log_vars(x0, y0, x1, y1);
							if (first)
							{
								x_pos = x0 < x1 ? x0 : x1;
								y_pos = y0 < y1 ? y0 : y1;
								docwire_log_vars(x_pos, y_pos);
								first = false;
							}
//							if (last)
								str_width = x0 > x1 ? x0 - x_pos : x1 - x_pos;
							if (abs(y1 - y0) > str_height)
								str_height = abs(y1 - y0);
							docwire_log_vars(str_width, str_height);
							if (y_pos > y1)
								y_pos = y1;
							if (y_pos > y0)
								y_pos = y0;

							const double SPACE_SIZE_COEFF = 0.1; //from pdfminer
							space_size = SPACE_SIZE_COEFF * std::max(advance, text_height);
							docwire_log_var(space_size);

							m_current_state.m_line_matrix.m_offset_x += advance;
							if (output.length() > 0 && output[output.length() - 1] == ' ')
							{

								m_current_state.m_line_matrix.m_offset_x += word_space;
							}
							add_charspace = true;
						}
					}
				}
				// warning TODO: Workaround for NULL characters but probably should not happen.
				output.erase(std::remove(output.begin(), output.end(), '\0'), output.end());
				TextElement new_element(x_pos, y_pos, str_width, str_height, space_size, output);
				m_text_elements.insert(new_element);
			}


      void executeTJ(std::vector<TJArrayElement>& tj_array)
			{
				docwire_log_func_with_args(tj_array);
				if (!m_font)
					return;
				TransformationMatrix tmp_matrix, cid_matrix;
				cid_matrix = tmp_matrix = m_current_state.m_ctm.combinedWith(m_current_state.m_matrix);
				double scale = m_current_state.m_scaling / 100.0;
				double x_scale = (m_current_state.m_font_size * scale) / 1000.0;
				double char_space = m_current_state.m_char_space * scale;
				double word_space = m_font->m_multibyte ? 0 : m_current_state.m_word_space * scale;

				bool add_charspace = false;
				double str_width = 0.0, str_height = 0.0;
				double x_pos = 0.0, y_pos = 0.0;
				std::string output;
				bool first = true, last = false;
				double space_size = 1.5;	//default space size

				for (size_t i = 0; i < tj_array.size(); ++i)
				{
					if (tj_array[i].m_is_number)
					{
						double distance = (-tj_array[i].m_value * x_scale);
						m_current_state.m_line_matrix.m_offset_x += distance;
						if (distance >= space_size)
						{
							output += ' ';
						}
						add_charspace = true;
					}
					else
					{
						m_font->convertToCidString(tj_array[i].m_text);
						m_font->setCidString(tj_array[i].m_text);
            output += tj_array[i].m_utf_text;
						while (m_font->hasNextCid())
						{
              std::string temp;
							unsigned int cid = m_font->getNextCIDandDecode(temp);
							if (i == tj_array.size() - 1 && !m_font->hasNextCid())
								last = true;
							if (add_charspace)
								m_current_state.m_line_matrix.m_offset_x += char_space;

							//update matrix for cid
							cid_matrix = tmp_matrix.combinedWith(m_current_state.m_line_matrix);

							//get character size
							double cid_width = m_font->getWidth(cid);
							double advance = cid_width * scale * m_current_state.m_font_size;

							//calculate bounding box
							double tmp_y = m_current_state.m_rise + m_current_state.m_font_size * m_font->getDescent();
							double text_height = m_current_state.m_font_size * m_font->getHeight();
							double x0 = cid_matrix.transformX(0, tmp_y);
							double y0 = cid_matrix.transformY(0, tmp_y);
							double x1 = cid_matrix.transformX(advance, tmp_y + text_height);
							double y1 = cid_matrix.transformY(advance, tmp_y + text_height);
							if (first)
							{
								x_pos = x0 < x1 ? x0 : x1;
								y_pos = y0 < y1 ? y0 : y1;
								first = false;
							}
//							if (last)
								str_width = x0 > x1 ? x0 - x_pos : x1 - x_pos;
							if (abs(int(y1 - y0)) > str_height)
								str_height = abs(int(y1 - y0));
							if (y_pos > y1)
								y_pos = y1;
							if (y_pos > y0)
								y_pos = y0;

							const double SPACE_SIZE_COEFF = 0.1; //from pdfminer
							space_size = SPACE_SIZE_COEFF * std::max(advance, text_height);

							m_current_state.m_line_matrix.m_offset_x += advance;
							if (output.length() > 0 && output[output.length() - 1] == ' ')
							{
								m_current_state.m_line_matrix.m_offset_x += word_space;
							}
							add_charspace = true;
						}
					}
				}
				// warning TODO: Workaround for NULL characters but probably should not happen.
				output.erase(std::remove(output.begin(), output.end(), '\0'), output.end());
				TextElement new_element(x_pos, y_pos, str_width, str_height, space_size, output);
				m_text_elements.insert(new_element);
			}


			void executeTj(const std::string& str, const PoDoFo::PdfFont* pCurFont, double curFontSize)
			{
				docwire_log_func_with_args(str, pCurFont);
				std::vector<TJArrayElement> tj_array;
				tj_array.push_back(TJArrayElement());
				tj_array[0].m_is_number = false;
				tj_array[0].m_text = str;
        if (pCurFont) {
          executeTJ(tj_array, pCurFont, curFontSize);
        }
        else
        {
          executeTJ(tj_array);
        }
				if (!m_font)
					return;
			}

			void getText(std::string& output)
			{
				// warning TODO: For now we are sorting strings using their x and y positions. Maybe we should implement better algorithms.
				std::multiset<TextElement>::iterator it = m_text_elements.begin();
				bool first = true;
				double x_end, y, x_begin;
				while (it != m_text_elements.end())
				{
					docwire_log_var(*it);
					//some minimum values for new line and space. Calculated experimentally
					double new_line_size = (*it).m_height * 0.75 < 4.0 ? 4.0 : (*it).m_height * 0.75;

          double horizontal_lines_separator_size = (*it).m_height;
					docwire_log_vars(new_line_size, horizontal_lines_separator_size, first);
					if (!first)
					{
						double dx = (*it).m_x - x_end;
						double dy = y - ((*it).m_y + (*it).m_height / 2);
						docwire_log_vars(dx, dy);

						if (dy >= new_line_size)
						{
							while (dy >= new_line_size)
							{
								docwire_log(debug) << "New line because of y position difference" << docwire_log_streamable_vars(dy, new_line_size);
								output += '\n';
								dy -= new_line_size;
							}
						}
						else if ((*it).m_x < x_begin)	//force new line
						{
							docwire_log(debug) << "New line because of x position difference" << docwire_log_streamable_vars(it->m_x, x_begin);
							output += '\n';
						}
						else if (dx >= (*it).m_space_size)
						{
						  if (dx > horizontal_lines_separator_size)
						  {
						    output += "\t\t\t\t";
						  }
						  else if (dx >= (*it).m_space_size)
						  {
						    output += ' ';
						  }
						}
					}
					output += (*it).m_text;
					first = false;
					x_begin = (*it).m_x;
					x_end = x_begin + (*it).m_width;
					y = (*it).m_y + (*it).m_height / 2;
					++it;
				}
			}

			PageText()
			{
				m_font = NULL;
			}
		};

		typedef std::map<std::string, Font*> FontsByNames;
		std::map<unsigned int, Font*> m_fonts_by_indexes;
		std::vector<Font*> m_fonts;

		~PDFContent()
		{
			for (size_t i = 0; i < m_fonts.size(); ++i)
				delete m_fonts[i];
			m_fonts.clear();
		}
	};

	std::shared_ptr<std::istream> m_data_stream;
	PDFContent m_pdf_content;

  PDFContent::FontsByNames parseFonts(const PoDoFo::PdfPage& page)
  {
    docwire_log_func();
    PDFContent::FontsByNames fonts_for_page;
    const PoDoFo::PdfDictionary* res_dictionary = to_dictionary(&page.GetResources()->GetObject());
    if (!res_dictionary)
      return fonts_for_page;

    const PoDoFo::PdfDictionary* fonts_dictionary = to_dictionary(res_dictionary->GetKey("Font"));
    if (fonts_dictionary)
    {
      for (auto k = fonts_dictionary->begin(); k != fonts_dictionary->end(); k++)
      {
        PoDoFo::PdfName font_code = k->first.GetString();
        docwire_log_var(font_code);
        const PoDoFo::PdfDictionary* font_dictionary = to_dictionary(&k->second);
        if (font_dictionary)
        {
          docwire_log(debug) << "Font dictionary available";
          PDFContent::Font* font = NULL;
          bool is_new_font = false;
          //make sure we wont create the same instance of Font twice.
          try
          {
            const PoDoFo::PdfReference& font_dictionary_ref = fonts_dictionary->GetKey(font_code)->GetReference();
            if (fonts_dictionary->HasKey(font_code))
            {
              docwire_log(debug) << "Font dictionary contains font code";
              auto index = fonts_dictionary->GetKey(font_code)->GetReference().ObjectNumber();
              if (m_pdf_content.m_fonts_by_indexes.find(index) == m_pdf_content.m_fonts_by_indexes.end())
              {
                is_new_font = true;
                font = new PDFContent::Font;
                font->m_font_dictionary = font_dictionary;
                fonts_for_page[font_code.GetString()] = font;
                m_pdf_content.m_fonts_by_indexes[index] = font;
                m_pdf_content.m_fonts.push_back(font);
              }
              else
                fonts_for_page[font_code.GetString()] = m_pdf_content.m_fonts_by_indexes[index];
            }
            else
            {
              docwire_log(debug) << "Font dictionary does not contain font code";
              is_new_font = true;
              font = new PDFContent::Font;
              font->m_font_dictionary = font_dictionary;
              fonts_for_page[font_code.GetString()] = font;
              m_pdf_content.m_fonts.push_back(font);
            }
          }
          catch (std::bad_alloc& ba)
          {
            if (font)
              delete font;
            font = NULL;
            throw;
          }

          if (is_new_font)
          {
            getFontEncoding(*font);
            getFontInfo(*font);
          }
        }
      }
    }
    return fonts_for_page;
  }

	void getFontWidths(PDFContent::Font& font)
	{
		if (font.m_font_metrics.m_first_char > font.m_font_metrics.m_last_char)
			font.m_font_metrics.m_last_char = font.m_font_metrics.m_first_char;	//throw an exception?
		const PoDoFo::PdfArray* widths = to_array(font.m_font_dictionary->GetKey("Widths"));
		if (widths)
		{
			for (size_t i = 0; i < widths->GetSize(); ++i)
				font.m_font_metrics.m_widths.push_back((*widths)[i].GetNumber());
		}
	}

	void loadFontDescriptor(PDFContent::Font& font)
	{
		if (font.m_font_descriptor)
		{
			font.m_font_metrics.m_font_name = to_string(font.m_font_descriptor->GetKey("FontName"), "unknown");
			font.m_font_metrics.m_flags = to_long(font.m_font_descriptor->GetKey("Flags"), 0);
			font.m_font_metrics.m_ascent = to_double(font.m_font_descriptor->GetKey("Ascent"), 0.0);
			font.m_font_metrics.m_descent = to_double(font.m_font_descriptor->GetKey("Descent"), 0.0);
			font.m_font_metrics.m_italic_angle = to_double(font.m_font_descriptor->GetKey("ItalicAngle"), 0.0);
			font.m_font_metrics.m_x_height = to_double(font.m_font_descriptor->GetKey("XHeight"), 0.0);
			font.m_font_metrics.m_missing_width = to_long(font.m_font_descriptor->GetKey("MissingWidth"), 0);
			font.m_font_metrics.m_leading = to_double(font.m_font_descriptor->GetKey("Leading"), 0.0);
			font.m_font_metrics.m_cap_height = to_double(font.m_font_descriptor->GetKey("CapHeight"), 0.0);
			const PoDoFo::PdfArray* pdf_bbox = to_array(font.m_font_descriptor->GetKey("FontBBox"));
			if (!pdf_bbox)
				pdf_bbox = to_array(font.m_font_dictionary->GetKey("FontBBox"));
			if (pdf_bbox && pdf_bbox->GetSize() == 4)
			{
				for (size_t i = 0; i < 4; ++i)
					font.m_font_metrics.m_font_bbox[i] = to_double(&(*pdf_bbox)[i], font.m_font_metrics.m_font_bbox[i]);
			}
		}
	}

	std::string to_string(const PoDoFo::PdfObject* object, const std::string& def_val)
	{
		if (object == nullptr)
			return def_val;
		else if (object->IsName())
			return object->GetName().GetString();
		else if (object->IsString())
			return object->GetString().GetString();
		else
			return def_val;
	}

	const PoDoFo::PdfName* to_name(const PoDoFo::PdfObject* object)
	{
		if (object == nullptr)
			return nullptr;
		else if (object->IsName())
			return &object->GetName();
		else
			return nullptr;
	}

	double to_double(const PoDoFo::PdfObject* object, double def_val)
	{
		return object == nullptr ? def_val : object->GetReal();
	}

	double to_long(const PoDoFo::PdfObject* object, long def_val)
	{
		docwire_log_func_with_args(object, def_val);
		if (object == nullptr)
			return def_val;
		else if (object->IsNumber())
			return object->GetNumber();
		else
			return def_val;
	}

	const PoDoFo::PdfDictionary* to_dictionary(const PoDoFo::PdfObject* object)
	{
		docwire_log_func_with_args(object);
		if (object == nullptr)
			return nullptr;
		else if (object->IsReference())
			return to_dictionary(object->GetDocument()->GetObjects().GetObject(object->GetReference()));
		else if (object->IsDictionary())
			return &object->GetDictionary();
		else
			return nullptr;
	}

	const PoDoFo::PdfArray* to_array(const PoDoFo::PdfObject* object)
	{
		if (object == nullptr)
			return nullptr;
		else if (object->IsReference())
			return to_array(object->GetDocument()->GetObjects().GetObject(object->GetReference()));
		else if (object->IsArray())
			return &object->GetArray();
		else
			return nullptr;
	}

	std::vector<char> to_buffer(const PoDoFo::PdfObject* object)
	{
		throw_if (object == nullptr, errors::program_logic{});
		if (object->IsReference())
			return to_buffer(object->GetDocument()->GetObjects().GetObject(object->GetReference()));
		else
		{
			PoDoFo::charbuff buffer = object->GetStream()->GetCopy();
			return std::vector<char>(buffer.c_str(), buffer.c_str() + buffer.size());
		}
	}

	void getFontInfo(PDFContent::Font& font)
	{
		docwire_log_func();
		font.m_font_type = to_string(font.m_font_dictionary->GetKey("Subtype"), "Type1");

		if (font.m_font_type != "TrueType" && font.m_font_type != "Type0" && font.m_font_type != "Type3" && font.m_font_type != "Type1" && font.m_font_type != "MMType1")
			font.m_font_type = "Type1";

		font.m_font_descriptor = to_dictionary(font.m_font_dictionary->GetKey("FontDescriptor"));
		font.m_base_font = to_string(font.m_font_dictionary->GetKey("BaseFont"), "unknown");

		if (font.m_font_type == "Type0")
		{
			font.m_multibyte = true;
			const PoDoFo::PdfArray* descendant_fonts = to_array(font.m_font_dictionary->GetKey("DescendantFonts"));
			if (descendant_fonts && descendant_fonts->GetSize() > 0)	//according to the documentation, only one value is allowed
			{
				const PoDoFo::PdfDictionary* descendant_font_dictionary = to_dictionary(&(*descendant_fonts)[0]);
				if (descendant_font_dictionary)
				{
					font.m_font_descriptor = to_dictionary(descendant_font_dictionary->GetKey("FontDescriptor"));
					font.m_font_metrics.m_missing_width = to_long(descendant_font_dictionary->GetKey("DW"), 1000);
					const PoDoFo::PdfArray* widths_array = to_array(descendant_font_dictionary->GetKey("W"));
					if (widths_array)
					{
						unsigned int got_values = 0, first_value, to_range;
						for (size_t i = 0; i < widths_array->GetSize(); ++i)
						{
							const PoDoFo::PdfObject* obj = &(*widths_array)[i];
							if (obj->IsArray() && got_values == 1) //INDEX [VAL1 Val2 ... VALN]
							{
								const PoDoFo::PdfArray* subwidth_array = &obj->GetArray();
								if (first_value > font.m_font_metrics.m_widths.size())
									font.m_font_metrics.m_widths.resize(first_value, font.m_font_metrics.m_missing_width);
								for (size_t j = 0; j < subwidth_array->GetSize(); ++j)
								{
									font.m_font_metrics.m_widths.push_back(to_long(&(*subwidth_array)[j], 0));
								}
								got_values = 0;
							}
							else if (obj->IsNumber())
							{
								++got_values;
								if (got_values == 1)
									first_value = obj->GetNumber();
								else if (got_values == 2)
									to_range = obj->GetNumber();
								else if (got_values == 3) // [FROM TO VAL]
								{
									got_values = 0;
									if (to_range < first_value)
										to_range = first_value;	//throw an exception?
									if (to_range >= font.m_font_metrics.m_widths.size())
										font.m_font_metrics.m_widths.resize(to_range + 1, font.m_font_metrics.m_missing_width);
									unsigned int calculated_value = obj->GetNumber();
									for (size_t j = first_value; j <= to_range; ++j)
										font.m_font_metrics.m_widths[j] = calculated_value;
								}
							}
						}
					}
					loadFontDescriptor(font);
					// warning TODO: Those fonts can be vertical. PDF parser should support that feature
				}
			}
		}
		else if (font.m_font_type == "Type3")
		{
			font.m_font_metrics.m_first_char = to_long(font.m_font_dictionary->GetKey("FirstChar"), 0);
			font.m_font_metrics.m_last_char = to_long(font.m_font_dictionary->GetKey("LastChar"), 0);
			if (!font.m_font_descriptor)
			{
				const PoDoFo::PdfArray* pdf_bbox = to_array(font.m_font_dictionary->GetKey("FontBBox"));
				if (pdf_bbox && pdf_bbox->GetSize() == 4)
				{
					for (size_t i = 0; i < 4; ++i)
						font.m_font_metrics.m_font_bbox[i] = to_double(&(*pdf_bbox)[i], font.m_font_metrics.m_font_bbox[i]);
				}
			}
			else
				loadFontDescriptor(font);
			font.m_font_metrics.m_ascent = font.m_font_metrics.m_font_bbox[3];
			font.m_font_metrics.m_descent = font.m_font_metrics.m_font_bbox[1];
			const PoDoFo::PdfArray* pdf_font_matrix = to_array(font.m_font_dictionary->GetKey("FontMatrix"));
			if (pdf_font_matrix && pdf_font_matrix->GetSize() == 6)
			{
				for (size_t i = 0; i < 6; ++i)
					font.m_font_metrics.m_font_matrix[i] = to_double(&(*pdf_font_matrix)[i], font.m_font_metrics.m_font_matrix[i]);
			}
			font.m_font_metrics.m_vscale = font.m_font_metrics.m_font_matrix[1] + font.m_font_metrics.m_font_matrix[3];
			font.m_font_metrics.m_hscale = font.m_font_metrics.m_font_matrix[0] + font.m_font_metrics.m_font_matrix[2];
		}
		else
		{
			if (PDFContent::pdf_font_metrics_map.find(font.m_base_font) != PDFContent::pdf_font_metrics_map.end())
				font.m_font_metrics = PDFContent::pdf_font_metrics_map[font.m_base_font];
			else
			{
				font.m_font_metrics.m_first_char = to_long(font.m_font_dictionary->GetKey("FirstChar"), 0);
				font.m_font_metrics.m_last_char = to_long(font.m_font_dictionary->GetKey("LastChar"), 255);
				getFontWidths(font);
				loadFontDescriptor(font);
			}
		}
	}

	void getFontEncoding(PDFContent::Font& font)
	{
		docwire_log_func();
		if (font.m_font_dictionary->HasKey("ToUnicode"))
		{
			std::vector<char> buf = to_buffer(font.m_font_dictionary->GetKey("ToUnicode"));
			PDFReader::PDFStream::PDFStreamIterator it;
			it.init(buf.data(), buf.size());
			parseCMap(it, font.m_cmap);
		}
		//check if "Encoding" is defined. It can be a name...
		const PoDoFo::PdfName* encoding_name = to_name(font.m_font_dictionary->GetKey("Encoding"));
		if (encoding_name)
		{
			font.m_font_encoding = encoding_name->GetString();
			PredefinedSimpleEncodings::const_iterator it = m_pdf_predefined_simple_encodings.find(font.m_font_encoding);
			if (it != m_pdf_predefined_simple_encodings.end())
			{
				font.m_predefined_simple_encoding = true;
				font.m_simple_encoding_table = (unsigned int*)it->second;
			}
			//In that case, Encoding may be something more "complicated" like 90ms-RKSJ-H
			else
			{
				CIDToUnicode::const_iterator it = m_pdf_cid_to_unicode.find(font.m_font_encoding);
				if (it != m_pdf_cid_to_unicode.end())
				{
					font.m_predefined_cmap = true;
					parsePredefinedCMap(font, it->second);
				}
			}
		}
		//or dictionary
		const PoDoFo::PdfDictionary* encoding_dict = to_dictionary(font.m_font_dictionary->GetKey("Encoding"));
		if (encoding_dict)
		{
			font.m_predefined_simple_encoding = true;
			font.m_own_simple_encoding_table = true;
			font.m_simple_encoding_table = new unsigned int[256];
			const PoDoFo::PdfName* base_encoding_name = to_name(encoding_dict->GetKey("BaseEncoding"));
			const unsigned int* source_table;
			if (base_encoding_name)
			{
				PredefinedSimpleEncodings::const_iterator it = m_pdf_predefined_simple_encodings.find(base_encoding_name->GetString());
				if (it != m_pdf_predefined_simple_encodings.end())
					source_table = it->second;
				else
					source_table = StandardEncodingUtf8;
			}
			else
				source_table = StandardEncodingUtf8;
			for (int j = 0; j < 256; ++j)
				font.m_simple_encoding_table[j] = source_table[j];
			size_t replacements = 0;
			const PoDoFo::PdfArray* differences = to_array(encoding_dict->GetKey("Differences"));
			if (differences)
			{
				for (auto const& difference: *differences)
				{
					if (difference.IsNumber())
					{
						replacements = difference.GetNumber();
						if (replacements > 255)
							replacements = 0;
					}
					else
					{
						if (difference.IsName())
						{
							CharacterNames::const_iterator it = m_pdf_character_names.find(difference.GetName().GetString());
							if (it != m_pdf_character_names.end())
							{
								font.m_simple_encoding_table[replacements] = it->second;
								++replacements;
								if (replacements > 255)
									replacements = 0;
							}
						}
					}
				}
			}
		}
	}

	void parsePredefinedCMap(PDFContent::Font& font, const std::string& cid_to_unicode_cmap)
	{
		try
		{
			#ifdef WIN32
			std::string cmap_to_cid_file_name = "resources\\" + font.m_font_encoding;
			#else
			std::string cmap_to_cid_file_name = "resources/" + font.m_font_encoding;
			#endif
			bool next_cmap_exist = true;
			PDFContent::CMap* current_cmap = &font.m_to_cid_cmap;
			while (next_cmap_exist)
			{
				PDFReader::PDFStream::PDFStreamIterator to_cid_stream_iterator;
				FileStream file_stream(cmap_to_cid_file_name);
				if (!file_stream.open())
				{
					owner().sendTag(make_error_ptr("Cannot open file", cmap_to_cid_file_name));
					return;
				}
				std::vector<char> buffer(file_stream.size() + 2);
				throw_if (!file_stream.read(&buffer[1], 1, buffer.size() - 2), buffer.size() - 2);
				file_stream.close();

				std::string last_name;
				std::string min, max;
				unsigned int codepoint;
				bool is_not_def = false;
				bool in_cid_range = false;
				bool in_cid_char = false;
				bool reading_min = false;
				bool reading_max = false;
				buffer[0] = '[';
				buffer[buffer.size() - 1] = ']';
				to_cid_stream_iterator.init(&buffer[0], buffer.size());
				to_cid_stream_iterator.levelDown();
				next_cmap_exist = false;

				while (to_cid_stream_iterator.hasNext())
				{
					to_cid_stream_iterator.getNextElement();
					switch (to_cid_stream_iterator.getType())
					{
						case PDFReader::name:
						{
							last_name = std::string(to_cid_stream_iterator.getData() + 1, to_cid_stream_iterator.getDataLength() - 1);
							break;
						}
						case PDFReader::string:
						{
							if (reading_min)
							{
								to_cid_stream_iterator.toHexString(min);
								reading_min = false;
								if (in_cid_range)
									reading_max = true;
							}
							else if (reading_max)
							{
								to_cid_stream_iterator.toHexString(max);
								reading_max = false;
							}
							break;
						}
						case PDFReader::int_numeric:
						{
							codepoint = to_cid_stream_iterator.toLong();
							if (in_cid_range)
							{
								reading_min = true;
								current_cmap->addCodeRange(min, max, codepoint, "", is_not_def);
							}
							else if (in_cid_char)
							{
								reading_min = true;
								current_cmap->addCodeRange(min, min, codepoint, "", is_not_def);
							}
							break;
						}
						case PDFReader::operator_obj:
						{
							std::string pdf_operator = std::string(to_cid_stream_iterator.getData(), to_cid_stream_iterator.getDataLength());
							switch (PDFReader::getOperatorCode(pdf_operator))
							{
								case PDFReader::usecmap:
								{
									#ifdef WIN32
									cmap_to_cid_file_name = "resources\\" + last_name;
									#else
									cmap_to_cid_file_name = "resources/" + last_name;
									#endif
									next_cmap_exist = true;
									break;
								}
								case PDFReader::begincidrange:
								{
									reading_min = true;
									in_cid_range = true;
									break;
								}
								case PDFReader::endcidrange:
								{
									reading_min = false;
									in_cid_range = false;
									break;
								}
								case PDFReader::begincidchar:
								{
									reading_min = true;
									in_cid_char = true;
									break;
								}
								case PDFReader::endcidchar:
								{
									reading_min = false;
									in_cid_char = false;
									break;
								}
								case PDFReader::beginnotdefrange:
								{
									reading_min = true;
									is_not_def = true;
									in_cid_range = true;
									break;
								}
								case PDFReader::endnotdefrange:
								{
									reading_min = false;
									is_not_def = false;
									in_cid_range = false;
									break;
								}
								case PDFReader::beginnotdefchar:
								{
									reading_min = true;
									is_not_def = true;
									in_cid_char = true;
									break;
								}
								case PDFReader::endnotdefchar:
								{
									reading_min = false;
									is_not_def = false;
									in_cid_char = false;
									break;
								}
							}
							break;
						}
					}
				}
				if (next_cmap_exist)
				{
					current_cmap->m_ready = true;
					current_cmap->m_parent = new PDFContent::CMap;
					current_cmap = current_cmap->m_parent;
				}
			}

			PDFReader::PDFStream::PDFStreamIterator to_unicode_stream_iterator;
			#ifdef WIN32
			FileStream file_stream("resources\\" + cid_to_unicode_cmap);
			#else
			FileStream file_stream("resources/" + cid_to_unicode_cmap);
			#endif
			if (!file_stream.open())
			{
				owner().sendTag(make_error_ptr("Cannot open file", cid_to_unicode_cmap));
				return;
			}
			std::vector<char> buffer(file_stream.size() + 2);
			throw_if (!file_stream.read(&buffer[1], 1, buffer.size() - 2), cid_to_unicode_cmap, buffer.size() - 2);
			file_stream.close();
			buffer[0] = '[';
			buffer[buffer.size() - 1] = ']';
			to_unicode_stream_iterator.init(&buffer[0], buffer.size());
			parseCMap(to_unicode_stream_iterator, font.m_cmap);
		}
		catch (const std::exception& e)
		{
			std::throw_with_nested(make_error(font.m_font_encoding));
		}
	}

	void parseCMap(PDFReader::PDFStream::PDFStreamIterator& iterator, PDFContent::CMap& cmap)
	{
		iterator.backToRoot();
		iterator.levelDown();

		bool in_bf_range = false;
		std::string min;
		std::string max;
		bool reading_min = true;
		bool reading_max = false;
		bool reading_range = false;

		bool in_bf_char = false;
		bool bf_char_next_first = true;
		std::string bf_char;
		std::string bf_code;

		bool in_not_def = false;

		while (iterator.hasNext())
		{
			iterator.getNextElement();
			switch (iterator.getType())
			{
				case PDFReader::string:
				{
					if (in_bf_range)
					{
						if (reading_min)
						{
							reading_min = false;
							reading_max = true;
							iterator.toHexString(min);
						}
						else if (reading_max)
						{
							reading_max = false;
							reading_range = true;
							iterator.toHexString(max);
						}
						else if (reading_range)
						{
							reading_range = false;
							reading_min = true;
							std::string range;
							iterator.toHexString(range);
							cmap.addCodeRange(min, max, hex_string_to_uint(range.c_str(), range.length()), utf16be_to_utf8(range), in_not_def);
						}
					}
					else if (in_bf_char)
					{
						if (bf_char_next_first)
						{
							bf_char_next_first = false;
							iterator.toHexString(bf_char);
						}
						else
						{
							bf_char_next_first = true;
							iterator.toHexString(bf_code);
							cmap.addCodeRange(bf_char, bf_char, 0, utf16be_to_utf8(bf_code), in_not_def);	//first code point doesnt matter here
						}
					}
					break;
				}
				case PDFReader::operator_obj:
				{
					std::string pdf_operator = iterator.toPlainText();
					switch (PDFReader::getOperatorCode(pdf_operator))
					{
						case PDFReader::beginbfrange:
						{
							in_bf_range = true;
							break;
						}
						case PDFReader::endbfrange:
						{
							in_bf_range = false;
							break;
						}
						case PDFReader::beginbfchar:
						{
							in_bf_char = true;
							break;
						}
						case PDFReader::endbfchar:
						{
							in_bf_char = false;
							break;
						}
						case PDFReader::beginnotdefrange:
						{
							in_bf_range = true;
							in_not_def = true;
							break;
						}
						case PDFReader::endnotdefrange:
						{
							in_bf_range = false;
							in_not_def = false;
							break;
						}
						case PDFReader::beginnotdefchar:
						{
							in_not_def = true;
							in_bf_char = true;
							break;
						}
						case PDFReader::endnotdefchar:
						{
							in_not_def = false;
							in_bf_char = false;
							break;
						}
					}
					break;
				}
				case PDFReader::array:
				{
					if (in_bf_range && reading_range)
					{
						reading_range = false;
						reading_min = true;
						iterator.levelDown();
						while (iterator.hasNext())
						{
							iterator.getNextElement();
							if (iterator.getType() == PDFReader::string)
							{
								std::string range;
								iterator.toHexString(range);
								cmap.addCodeRange(min, min, 0, utf16be_to_utf8(range), in_not_def);	//first code point doesnt matter here
								increment_hex_string(min);
							}
						}
						iterator.levelUp();
					}
					break;
				}
			}
		}
		cmap.m_ready = true;
	}

	std::string string_to_hex(const std::string& input)
	{
		static const char hex_digits[] = "0123456789ABCDEF";
		std::string output;
		output.reserve(input.length() * 2);
		for (unsigned char c : input)
		{
			output.push_back(hex_digits[c >> 4]);
			output.push_back(hex_digits[c & 15]);
		}
		return output;
	}

	std::string pdfstring_to_hex(const PoDoFo::PdfString& pdf_string)
	{
		// warning TODO: Operating on internal buffer is the only way to get proper hex string data. GetStringUtf8() returns incremented values sometimes. Bug in PoDoFo?
		return string_to_hex(pdf_string.GetString());
	}

	static std::string encode_to_utf8(const PoDoFo::PdfString& pdf_string, const PoDoFo::PdfFont& font)
	{
		docwire_log_func_with_args(pdf_string, font);
		std::string decoded;
		std::vector<double> lengths;
		std::vector<unsigned int> positions;
		PoDoFo::PdfTextState state;
		state.Font = &font;
		try
		{
			(void)state.Font->TryScanEncodedString(pdf_string, state, decoded, lengths, positions);
		}
		catch (std::exception& e)
		{
			std::throw_with_nested(make_error("Error in TryScanEncodedString()"));
		}
		return decoded;
	}

	std::vector<double> pdfvariant_stack_to_vector_of_double(PoDoFo::PdfVariantStack& stack, int start, int how_many)
	{
		docwire_log_func_with_args(stack, start, how_many);
		std::vector<double> result;
		for (int i = start; i < how_many; i++)
		{
			docwire_log_vars(i, stack[i]);
			result.insert(result.begin(), stack[i].GetReal());
		}
		return result;
	}

	void parseText()
	{
		docwire_log_func();
		int page_count = m_pdf_document.GetPages().GetCount();
		docwire_log_var(page_count);
		for (size_t page_num = 0; page_num < page_count; page_num++)
		{
			docwire_log_var(page_num);
			auto response = owner().sendTag(tag::Page{});
			if (response.skip)
			{
				continue;
			}
			if (response.cancel)
			{
				break;
			}
			try
			{
				PDFContent::PageText page_text;
				const PoDoFo::PdfFont* pCurFont = nullptr;
				double curFontSize = -1;
				PoDoFo::PdfPage* page = &m_pdf_document.GetPages().GetPageAt(page_num);
				PDFContent::FontsByNames fonts_for_page = parseFonts(*page);
				PoDoFo::PdfContentStreamReader reader(*page);
				bool in_text = false;

				PoDoFo::PdfContent content;

				while (reader.TryReadNext(content))
				{
					docwire_log(debug) << "PdfContentStreamReader::TryReadNext() succeeded";
					docwire_log_var(content);
					if (content.Type == PoDoFo::PdfContentType::Operator)
					{
						switch (content.Operator)
						{
							case PoDoFo::PdfOperator::ET:
							{
								docwire_log(debug) << "content.Operator == PdfOperator::ET";
								in_text = false;
								break;
							}
							case PoDoFo::PdfOperator::Tm:
							{
								docwire_log(debug) << "content.Operator == PdfOperator::Tm";
								if (!in_text)
									break;
								page_text.executeTm(pdfvariant_stack_to_vector_of_double(content.Stack, 0, 6));
								break;
							}
							case PoDoFo::PdfOperator::Td:
							{
								docwire_log(debug) << "content.Operator == PdfOperator::Td";
								if (!in_text)
									break;
								page_text.executeTd(pdfvariant_stack_to_vector_of_double(content.Stack, 0, 2));
								break;
							}
							case PoDoFo::PdfOperator::T_Star:
							{
								docwire_log(debug) << "content.Operator == PdfOperator::T_Star";
								if (!in_text)
									break;
								page_text.executeTstar();
								break;
							}
							case PoDoFo::PdfOperator::TD:
							{
								docwire_log(debug) << "content.Operator == PdfOperator::TD";
								if (!in_text)
									break;
								page_text.executeTD(pdfvariant_stack_to_vector_of_double(content.Stack, 0, 2));
									break;
							}
							case PoDoFo::PdfOperator::TJ:
							{
								docwire_log(debug) << "content.Operator == PdfOperator::TJ";
								if (!in_text)
									break;
								std::vector<PDFContent::TJArrayElement> tj_array;
								tj_array.reserve(100);
								const PoDoFo::PdfArray& a = content.Stack[0].GetArray();
								for (size_t j = 0; j < a.GetSize(); ++j)
								{
									switch (a[j].GetDataType())
									{
										case PoDoFo::PdfDataType::String:
										{
											tj_array.push_back(PDFContent::TJArrayElement());
											tj_array[tj_array.size() - 1].m_is_number = false;
											if (pCurFont)
											{
												try
												{
													tj_array[tj_array.size() - 1].m_utf_text = encode_to_utf8(a[j].GetString(), *pCurFont);
												}
												catch (const std::exception&)
												{}
												if (a[j].GetString().IsHex())
												{
													tj_array[tj_array.size() - 1].m_text = pdfstring_to_hex(a[j].GetString());
												}
												else
												{
													tj_array[tj_array.size() - 1].m_text = pdfstring_to_hex(a[j].GetString());
												}
												tj_array[tj_array.size() - 1].m_pdf_string = a[j].GetString();
											}
											else
											{
												tj_array[tj_array.size() - 1].m_utf_text = a[j].GetString().GetString();
												if (a[j].GetString().IsHex())
												{
													tj_array[tj_array.size() - 1].m_text = pdfstring_to_hex(a[j].GetString());
												}
												else
												{
													tj_array[tj_array.size() - 1].m_text = pdfstring_to_hex(a[j].GetString());
												}
												tj_array[tj_array.size() - 1].m_pdf_string = a[j].GetString();
											}
											break;
										}
										case PoDoFo::PdfDataType::Number:
										case PoDoFo::PdfDataType::Real:
										{
											tj_array.push_back(PDFContent::TJArrayElement());
											tj_array[tj_array.size() - 1].m_is_number = true;
											tj_array[tj_array.size() - 1].m_value = a[j].GetReal();
											break;
										}
									}
									PDFContent::TJArrayElement& new_element = tj_array[tj_array.size() - 1];
									docwire_log_var(new_element);
								}
								if (pCurFont)
								{
									page_text.executeTJ(tj_array, pCurFont, curFontSize);
								}
								else
								{
									page_text.executeTJ(tj_array);
								}
								break;
							}
							case PoDoFo::PdfOperator::Tj:
							{
								docwire_log(debug) << "content.Operator == PdfOperator::Tj";
								if (!in_text)
									break;

								std::vector<PDFContent::TJArrayElement> tj_array;
								tj_array.push_back(PDFContent::TJArrayElement());
								tj_array[tj_array.size() - 1].m_is_number = false;
								if (pCurFont)
								{
									auto text = content.Stack[0].GetString();
									try
									{
										tj_array[tj_array.size() - 1].m_utf_text = encode_to_utf8(text, *pCurFont);
									}
									catch (const std::exception&)
									{}
									tj_array[tj_array.size() - 1].m_pdf_string = text;
									if (text.IsHex())
									{
										tj_array[tj_array.size() - 1].m_text = pdfstring_to_hex(text);
									}
									else
									{
										tj_array[tj_array.size() - 1].m_text = pdfstring_to_hex(text);
									}
								}
								else
								{
									auto text = content.Stack[0].GetString();
									tj_array[tj_array.size() - 1].m_utf_text = text.GetString();
									tj_array[tj_array.size() - 1].m_pdf_string = text;
									if (text.IsHex())
									{
										tj_array[tj_array.size() - 1].m_text = pdfstring_to_hex(text);
									}
									else
									{
										tj_array[tj_array.size() - 1].m_text = pdfstring_to_hex(text);
									}
								}
								if (pCurFont)
								{
									page_text.executeTJ(tj_array, pCurFont, curFontSize);
								}
								else{
									page_text.executeTJ(tj_array);
								}
								break;
							}
							case PoDoFo::PdfOperator::Tw:
							{
								docwire_log(debug) << "content.Operator == PdfOperator::Tw";
								if (!in_text)
									break;
								auto values = pdfvariant_stack_to_vector_of_double(content.Stack, 0, 1);
								page_text.executeTw(values);
								if (pCurFont)
								{
									//state.WordSpacing = values[0];
								}
								break;
							}
							case PoDoFo::PdfOperator::Tc:
							{
								docwire_log(debug) << "content.Operator == PdfOperator::Tc";
								if (!in_text)
									break;
								auto values = pdfvariant_stack_to_vector_of_double(content.Stack, 0, 1);
								page_text.executeTc(values);
								if (pCurFont)
								{
									//state.CharSpacing = values[0];
								}
								break;
							}
							case PoDoFo::PdfOperator::Ts:
							{
								docwire_log(debug) << "content.Operator == PdfOperator::Ts";
								if (!in_text)
									break;
								page_text.executeTs(pdfvariant_stack_to_vector_of_double(content.Stack, 0, 1));
								break;
							}
							case PoDoFo::PdfOperator::Quote:
							{
								docwire_log(debug) << "content.Operator == PdfOperator::Quote";
								if (!in_text)
									break;
								if (pCurFont)
								{
									page_text.executeQuote(content.Stack[0].GetString().GetString(), pCurFont, curFontSize);
								}
								else
								{
									page_text.executeQuote(content.Stack[0].GetString().GetString(), pCurFont, curFontSize);
								}
								break;
							}
							case PoDoFo::PdfOperator::DoubleQuote:
							{
								docwire_log(debug) << "content.Operator == PdfOperator::DoubleQuote";
								if (!in_text)
									break;
								if(pCurFont)
								{
									std::string s = content.Stack[0].GetString().GetString();
									page_text.executeDoubleQuote(s, pdfvariant_stack_to_vector_of_double(content.Stack, 1, 2), pCurFont, curFontSize);
								}
								else
								{
									page_text.executeDoubleQuote(content.Stack[0].GetString().GetString(), pdfvariant_stack_to_vector_of_double(content.Stack, 1, 2), pCurFont, curFontSize);
								}
								break;
							}
							case PoDoFo::PdfOperator::Tf:
							{
								docwire_log(debug) << "content.Operator == PdfOperator::Tf";
								if (!in_text)
									break;
								long font_size = content.Stack[0].GetReal();
								std::string font_name = content.Stack[1].GetName().GetString();
								if (fonts_for_page.find(font_name) != fonts_for_page.end()) {
									page_text.executeTf(font_size, *(fonts_for_page)[font_name]);
								}
								try
								{
									std::lock_guard<std::mutex> podofo_freetype_mutex_lock(podofo_freetype_mutex);
									pCurFont = page->GetResources()->GetFont(font_name);
								}
								catch (PoDoFo::PdfError &error)
								{
									if (error.GetCode() != PoDoFo::PdfErrorCode::InternalLogic)
									{
										throw PoDoFo::PdfError(error);
									}
								}

								if (pCurFont)
								{
									//state.FontSize = font_size;
									curFontSize = font_size;
								}
								else
								{
									owner().sendTag(make_error_ptr("Unknown font"));
								}

								break;
							}
							case PoDoFo::PdfOperator::BT:
							{
								docwire_log(debug) << "content.Operator == PdfOperator::BT";
								in_text = true;
								page_text.executeBT();
								break;
							}
							case PoDoFo::PdfOperator::TL:
							{
								docwire_log(debug) << "content.Operator == PdfOperator::TL";
								page_text.executeTL(pdfvariant_stack_to_vector_of_double(content.Stack, 0, 1));
								break;
							}
							case PoDoFo::PdfOperator::Tz:
							{
								docwire_log(debug) << "content.Operator == PdfOperator::Tz";
								long scale = content.Stack[0].GetReal();
								page_text.executeTZ(scale);
								if (pCurFont) {
									//state.FontScale = scale;
								}
								break;
							}
							case PoDoFo::PdfOperator::cm:
							{
								docwire_log(debug) << "content.Operator == PdfOperator::cm";
								page_text.executeCm(pdfvariant_stack_to_vector_of_double(content.Stack, 0, 6));
								break;
							}
							case PoDoFo::PdfOperator::Q:
							{
								docwire_log(debug) << "content.Operator == PdfOperator::Q";
								page_text.popState();
								break;
							}
							case PoDoFo::PdfOperator::q:
							{
								docwire_log(debug) << "content.Operator == PdfOperator::q";
								page_text.pushState();
								break;
							}
						}
					}
					else
					{
						docwire_log(debug) << "content.Type != PdfContentType::Operator";
						// warning TODO throw
					}
				}
				std::string single_page_text;
				page_text.getText(single_page_text);
				single_page_text += "\n\n";
				auto response = owner().sendTag(tag::Text{single_page_text});
				if (response.cancel)
				{
					break;
				}
        auto response2 = owner().sendTag(tag::ClosePage{});
        if (response2.cancel)
        {
          break;
        }
			}
			catch (const std::exception& e)
			{
				std::throw_with_nested(make_error(page_num));
			}
			docwire_log(debug) << "Page processed" << docwire_log_streamable_var(page_num);
		}
	}

	void parseMetadata(PDFReader& pdf_reader, attributes::Metadata& metadata)
	{
		//according to PDF specification, we can extract: author, creation date and last modification date.
		//LastModifyBy is not possible. Other metadata information available in PDF are not supported in Metadata class.
		bool got_author = false;
		bool got_creation_date = false;
		bool got_modify_date = false;
		PDFReader::PDFDictionary* info = pdf_reader.m_info;
		if (info)
		{
			PDFReader::PDFString* author = info->getObjAsString("Author");
			if (author)
			{
				got_author = true;
				author->ConvertToLiteral();
				metadata.author = author->m_value;
			}
			PDFReader::PDFString* creation_date = info->getObjAsString("CreationDate");
			if (creation_date)
			{
				got_creation_date = true;
				creation_date->ConvertToLiteral();
				tm creation_date_tm;
				int offset = 0;
				std::string creation_date_str = (*creation_date)();
				while (creation_date_str.length() > offset && (creation_date_str[offset] < '0' || creation_date_str[offset] > '9'))
					++offset;
				creation_date_str.erase(0, offset);
				parsePDFDate(creation_date_tm, creation_date_str);
				metadata.creation_date = creation_date_tm;
			}
			PDFReader::PDFString* modify_date = info->getObjAsString("ModDate");
			if (modify_date)
			{
				got_modify_date = true;
				modify_date->ConvertToLiteral();
				tm modify_date_tm;
				int offset = 0;
				std::string mod_date_str = (*modify_date)();
				while (mod_date_str.length() > offset && (mod_date_str[offset] < '0' || mod_date_str[offset] > '9'))
					++offset;
				mod_date_str.erase(0, offset);
				parsePDFDate(modify_date_tm, mod_date_str);
				metadata.last_modification_date = modify_date_tm;
			}
		}
		if (!got_author || !got_creation_date || !got_modify_date)
		{
			PDFReader::PDFStream* metadata_stream = pdf_reader.m_metadata;
			if (metadata_stream)
			{
				size_t pos;
				std::string content = metadata_stream->getIterator().toPlainText();
				if (!got_author)
				{
					//possibilities I have found: Authour="name", Author='name', Author>name<.
					if ((pos = content.find("Author")) != std::string::npos)
					{
						pos += 7;
						std::string author;
						while (pos < content.length() && content[pos] != '\"' && content[pos] != '\'' && content[pos] != '<')
							author += content[pos];
						metadata.author = author;
					}
				}
				if (!got_creation_date)
				{
					size_t entry_len;
					if ((pos = content.find("CreationDate")) == std::string::npos)
					{
						pos = content.find("CreateDate");
						entry_len = 10;
					}
					else
						entry_len = 12;
					if (pos != std::string::npos)
					{
						pos += entry_len;
						std::string creation_date;
						while (pos < content.length() && content[pos] != '\"' && content[pos] != '\'' && content[pos] != '<')
							creation_date += content[pos];
						tm creation_date_tm;
						if (string_to_date(creation_date, creation_date_tm))
							metadata.creation_date = creation_date_tm;
					}
				}
				if (!got_modify_date)
				{
					size_t entry_len;
					if ((pos = content.find("ModifyDate")) == std::string::npos)
					{
						pos = content.find("ModDate");
						entry_len = 7;
					}
					else
						entry_len = 10;
					if (pos != std::string::npos)
					{
						pos += entry_len;
						std::string modify_date;
						while (pos < content.length() && content[pos] != '\"' && content[pos] != '\'' && content[pos] != '<')
							modify_date += content[pos];
						tm modify_date_tm;
						if (string_to_date(modify_date, modify_date_tm))
							metadata.last_modification_date = modify_date_tm;
					}
				}
			}
		}
		metadata.page_count = m_pdf_document.GetPages().GetCount();
	}

	void loadDocument(const data_source& data)
	{
		docwire_log_func();
		std::lock_guard<std::mutex> load_document_mutex_lock(load_document_mutex);
		std::span<const std::byte> span = data.span();
		try
		{
			m_pdf_document.LoadFromDevice(std::make_shared<PoDoFo::SpanStreamDevice>(reinterpret_cast<const char*>(span.data()), span.size()));
		}
		catch (const PoDoFo::PdfError& e)
		{
			if (e.GetCode() == PoDoFo::PdfErrorCode::InvalidPassword)
			{
				std::throw_with_nested(make_error(errors::file_encrypted{}));
			}
			else
			{
				std::throw_with_nested(make_error("LoadFromDevice() failed"));
			}
		}
	}
};

pimpl_impl<PDFParser>::CIDToUnicode pimpl_impl<PDFParser>::m_pdf_cid_to_unicode;
pimpl_impl<PDFParser>::PDFContent::FontMetricsMap pimpl_impl<PDFParser>::PDFContent::pdf_font_metrics_map;
pimpl_impl<PDFParser>::CharacterNames pimpl_impl<PDFParser>::m_pdf_character_names;
pimpl_impl<PDFParser>::PredefinedSimpleEncodings pimpl_impl<PDFParser>::m_pdf_predefined_simple_encodings;
pimpl_impl<PDFParser>::PDFReader::CompressionCodes pimpl_impl<PDFParser>::PDFReader::m_compression_codes;
pimpl_impl<PDFParser>::PDFReader::OperatorCodes pimpl_impl<PDFParser>::PDFReader::m_operator_codes;

std::mutex podofo_mutex;

PDFParser::PDFParser()
	: with_pimpl<PDFParser>(nullptr)
{
	std::lock_guard<std::mutex> podofo_mutex_lock(podofo_mutex);
	renew_impl();
}

PDFParser::~PDFParser()
{
	std::lock_guard<std::mutex> podofo_freetype_mutex_lock(podofo_freetype_mutex);
	destroy_impl();
}

attributes::Metadata PDFParser::metaData(const data_source& data)
{
	attributes::Metadata metadata;
	impl().m_data_stream = data.istream();
	impl().loadDocument(data);
	pimpl_impl<PDFParser>::PDFReader pdf_reader(impl().m_data_stream);
	impl().parseMetadata(pdf_reader, metadata);
	return metadata;
}

void
PDFParser::parse(const data_source& data)
{
	docwire_log(debug) << "Using PDF parser.";
	{
		std::lock_guard<std::mutex> podofo_mutex_lock(podofo_mutex);
		renew_impl();
	}
	sendTag(tag::Document
		{
			.metadata = [this, &data]()
			{
				return metaData(data);
			}
		});
	impl().loadDocument(data);
	{
		std::lock_guard<std::mutex> podofo_mutex_lock(podofo_mutex);
		impl().parseText();
	}
	sendTag(tag::CloseDocument{});
}

} // namespace docwire
