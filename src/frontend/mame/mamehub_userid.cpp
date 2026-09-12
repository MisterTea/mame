// license:BSD-3-Clause

#include "mamehub_userid.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <random>
#include <string>

#if defined(__EMSCRIPTEN__)
// Browser builds have no reliable MAC; use a weak entropy mix below.
#elif defined(_WIN32)
#include <winsock2.h>
#include <iphlpapi.h>
#if defined(_MSC_VER)
#pragma comment(lib, "iphlpapi.lib")
#endif
#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__) || defined(__DragonFly__)
#include <ifaddrs.h>
#include <net/if.h>
#include <net/if_dl.h>
#include <sys/types.h>
#elif defined(__linux__) || defined(__ANDROID__)
#include <ifaddrs.h>
#include <net/if.h>
#include <netpacket/packet.h>
#include <sys/types.h>
#endif

namespace mamehub {
namespace {

bool mac_is_usable(std::uint8_t const *mac, std::size_t len)
{
	if (len < 6)
		return false;

	bool all_zero = true;
	bool all_ff = true;
	for (std::size_t i = 0; i < 6; ++i)
	{
		if (mac[i] != 0x00)
			all_zero = false;
		if (mac[i] != 0xff)
			all_ff = false;
	}
	return !all_zero && !all_ff;
}

bool collect_mac_bytes(std::array<std::uint8_t, 6> &out)
{
	out.fill(0);

#if defined(__EMSCRIPTEN__)
	return false;

#elif defined(_WIN32)
	ULONG buf_len = 0;
	if (GetAdaptersInfo(nullptr, &buf_len) != ERROR_BUFFER_OVERFLOW)
		return false;

	std::string buffer(buf_len, '\0');
	auto *info = reinterpret_cast<IP_ADAPTER_INFO *>(buffer.data());
	if (GetAdaptersInfo(info, &buf_len) != NO_ERROR)
		return false;

	for (auto *adapter = info; adapter != nullptr; adapter = adapter->Next)
	{
		if (adapter->AddressLength >= 6 && mac_is_usable(adapter->Address, adapter->AddressLength))
		{
			std::memcpy(out.data(), adapter->Address, 6);
			return true;
		}
	}
	return false;

#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__) || defined(__DragonFly__) || defined(__linux__) || defined(__ANDROID__)
	ifaddrs *ifap = nullptr;
	if (getifaddrs(&ifap) != 0)
		return false;

	bool found = false;
	for (ifaddrs *ifa = ifap; ifa != nullptr; ifa = ifa->ifa_next)
	{
		if (ifa->ifa_addr == nullptr)
			continue;
		if ((ifa->ifa_flags & IFF_LOOPBACK) != 0)
			continue;

#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__) || defined(__DragonFly__)
		if (ifa->ifa_addr->sa_family != AF_LINK)
			continue;
		auto const *sdl = reinterpret_cast<sockaddr_dl const *>(ifa->ifa_addr);
		if (sdl->sdl_alen < 6)
			continue;
		auto const *mac = reinterpret_cast<std::uint8_t const *>(LLADDR(sdl));
		if (!mac_is_usable(mac, sdl->sdl_alen))
			continue;
		std::memcpy(out.data(), mac, 6);
		found = true;
		break;
#elif defined(__linux__) || defined(__ANDROID__)
		if (ifa->ifa_addr->sa_family != AF_PACKET)
			continue;
		auto const *sll = reinterpret_cast<sockaddr_ll const *>(ifa->ifa_addr);
		if (sll->sll_halen < 6)
			continue;
		if (!mac_is_usable(sll->sll_addr, sll->sll_halen))
			continue;
		std::memcpy(out.data(), sll->sll_addr, 6);
		found = true;
		break;
#endif
	}
	freeifaddrs(ifap);
	return found;

#else
	return false;
#endif
}

std::uint64_t seed_from_mac(std::array<std::uint8_t, 6> const &mac, bool have_mac)
{
	std::uint64_t seed = 0xcbf29ce484222325ull; // FNV-1a offset basis
	if (have_mac)
	{
		for (std::uint8_t byte : mac)
		{
			seed ^= byte;
			seed *= 0x100000001b3ull;
		}
	}
	else
	{
		// Fallback entropy when MAC is unavailable (browser / exotic hosts).
		seed ^= static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(&seed));
		seed *= 0x100000001b3ull;
		seed ^= static_cast<std::uint64_t>(std::random_device{}());
		seed *= 0x100000001b3ull;
	}
	return seed ? seed : 0x9e3779b97f4a7c15ull;
}

std::string generate_username(std::uint64_t seed)
{
	std::mt19937_64 rng(seed);
	// 16 decimal digits, matching the historical MAMEHub user_id shape.
	std::string user_id(16, '0');
	for (char &ch : user_id)
		ch = static_cast<char>('0' + (rng() % 10));
	return user_id;
}

} // namespace

std::string resolve_user_id(std::string_view configured_user_id)
{
	if (!configured_user_id.empty())
		return std::string(configured_user_id);

	std::array<std::uint8_t, 6> mac{};
	bool const have_mac = collect_mac_bytes(mac);
	return generate_username(seed_from_mac(mac, have_mac));
}

} // namespace mamehub
