#include "Common.hh"
#include "core/Logging.hh"

#include <algorithm>
#include <cctype>

#ifdef _WIN32
#include <intrin.h>
#define os_break __debugbreak
#else
#define os_break __builtin_trap
#endif

namespace sp
{
	void Assert(bool condition, const string &message)
	{
		if (!condition)
		{
			Errorf("assertion failed: %s", message);
			os_break();
			throw std::runtime_error(message);
		}
	}

	void DebugBreak()
	{
		os_break();
	}

	uint32 CeilToPowerOfTwo(uint32 v)
	{
		v--;
		v |= v >> 1;
		v |= v >> 2;
		v |= v >> 4;
		v |= v >> 8;
		v |= v >> 16;
		v++;
		return v;
	}

	uint32 Uint32Log2(uint32 v)
	{
		uint32 r = 0;
		while (v >>= 1) r++;
		return r;
	}

	// Boost replacement functions
	bool starts_with(const string &str, const string &prefix)
	{
		return str.rfind(prefix, 0) == 0;
	}

	string to_lower_copy(const string &str)
	{
		string out(str);
		std::transform(str.begin(), str.end(), out.begin(), ::tolower);
		return out;
	}

	void trim(string &str)
	{
		trim_left(str);
		trim_right(str);
	}

	void trim_left(string &str)
	{
		auto left = std::find_if(str.begin(), str.end(), [](char ch) {
			return !std::isspace(ch);
		});
		str.erase(str.begin(), left);
	}

	void trim_right(string &str)
	{
		auto right = std::find_if(str.rbegin(), str.rend(), [](char ch) {
			return !std::isspace(ch);
		}).base();
		str.erase(right, str.end());
	}
}
