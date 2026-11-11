#include "Text.h"
#include <algorithm>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#else
#include <iconv.h>
#endif

namespace sprite
{
#ifdef _WIN32
	static std::string conv(const std::string& s, UINT from, UINT to)
	{
		if (s.empty()) return s;
		int n = MultiByteToWideChar(from, 0, s.data(), (int)s.size(), nullptr, 0);
		if (n <= 0) return s;
		std::wstring w(n, L'\0');
		MultiByteToWideChar(from, 0, s.data(), (int)s.size(), &w[0], n);
		int m = WideCharToMultiByte(to, 0, w.data(), n, nullptr, 0, nullptr, nullptr);
		if (m <= 0) return s;
		std::string r(m, '\0');
		WideCharToMultiByte(to, 0, w.data(), n, &r[0], m, nullptr, nullptr);
		return r;
	}
	std::string utf8ToRaw(const std::string& s) { return conv(s, CP_UTF8, 949); }
	std::string rawToUtf8(const std::string& s) { return conv(s, 949, CP_UTF8); }
#else
	static std::string conv(const std::string& s, const char* from, const char* to)
	{
		iconv_t cd = iconv_open(to, from);
		if (cd == (iconv_t)-1) return s;
		std::string out(s.size() * 4 + 4, '\0');
		char* in = const_cast<char*>(s.data()); size_t inl = s.size();
		char* o = &out[0]; size_t ol = out.size();
		size_t r = iconv(cd, &in, &inl, &o, &ol);
		iconv_close(cd);
		if (r == (size_t)-1) return s;
		out.resize(out.size() - ol);
		return out;
	}
	std::string utf8ToRaw(const std::string& s) { return conv(s, "UTF-8", "CP949"); }
	std::string rawToUtf8(const std::string& s) { return conv(s, "CP949", "UTF-8"); }
#endif
	std::string lower(std::string s)
	{
		for (auto& c : s) if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');	// ascii only: utf-8 multibyte stays intact
		return s;
	}
}
