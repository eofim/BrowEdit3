#pragma once
#include <string>

namespace sprite
{
	// Client file names are Korean (cp949). Everything in the sprite module works in UTF-8; these convert at the file boundary.
	std::string utf8ToRaw(const std::string& utf8);
	std::string rawToUtf8(const std::string& raw);
	std::string lower(std::string s);
}
