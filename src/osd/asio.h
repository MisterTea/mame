// license:BSD-3-Clause
// copyright-holders:Miodrag Milanovic
/***************************************************************************

    asio.hpp

    ASIO library implementation loader

***************************************************************************/

#ifdef __clang__
#pragma clang diagnostic ignored "-Wunknown-pragmas"
#pragma clang diagnostic ignored "-Winconsistent-missing-override"
#pragma clang diagnostic ignored "-Wunused-local-typedef"
#elif defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wsuggest-override"
#pragma GCC diagnostic ignored "-Wunused-variable"
#endif

#ifndef _WIN32_WINNT
#ifndef _WIN32_WINNT_WIN7
#define _WIN32_WINNT_WIN7 0x0601  // Windows 7
#endif
#define _WIN32_WINNT _WIN32_WINNT_WIN7
#endif
#define ASIO_HEADER_ONLY (1)
#ifndef ASIO_STANDALONE
#define ASIO_STANDALONE (1)
#endif
#define ASIO_SEPARATE_COMPILATION
#define ASIO_NOEXCEPT noexcept(true)
#define ASIO_NOEXCEPT_OR_NOTHROW noexcept(true)
#define ASIO_ERROR_CATEGORY_NOEXCEPT noexcept(true)

#include <asio.hpp>
#undef interface
