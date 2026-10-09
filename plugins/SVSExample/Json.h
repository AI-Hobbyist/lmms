// Small bounded JSON reader for the standalone example's SDK messages.
#ifndef SVS_EXAMPLE_JSON_H
#define SVS_EXAMPLE_JSON_H
#include <cstdlib>
#include <cmath>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace example {
struct Json
{
	enum Type
	{
		Null,
		Number,
		Boolean,
		String,
		Array,
		Object
	} type = Null;
	double number = 0;
	bool boolean = false;
	std::string string;
	std::vector<Json> array;
	std::map<std::string, Json> object;
	const Json& operator[](const std::string& key) const
	{
		auto i = object.find(key);
		static const Json empty;
		return i == object.end() ? empty : i->second;
	}
	double numeric(double fallback) const { return type == Number ? number : fallback; }
	std::string text(const std::string& fallback = {}) const { return type == String ? string : fallback; }
};

inline std::string quote(const std::string& text)
{
	std::string result = "\"";
	constexpr char digits[] = "0123456789abcdef";
	for (unsigned char c : text)
	{
		if (c == '"' || c == '\\')
		{
			result += '\\';
			result += c;
		}
		else if (c < 32)
		{
			result += "\\u00";
			result += digits[c >> 4];
			result += digits[c & 15];
		}
		else
			result += c;
	}
	return result + '"';
}

class Reader
{
	const std::string input;
	size_t offset = 0;
	[[noreturn]] void fail() const { throw std::runtime_error("Invalid JSON at " + std::to_string(offset)); }
	void whitespace()
	{
		while (offset < input.size()
			&& (input[offset] == ' ' || input[offset] == '\r' || input[offset] == '\n' || input[offset] == '\t'))
			++offset;
	}
	bool take(char c)
	{
		whitespace();
		if (offset < input.size() && input[offset] == c)
		{
			++offset;
			return true;
		}
		return false;
	}
	unsigned hex()
	{
		unsigned value = 0;
		for (int i = 0; i < 4; ++i)
		{
			if (offset >= input.size())
				fail();
			char c = input[offset++];
			value *= 16;
			if (c >= '0' && c <= '9')
				value += c - '0';
			else if (c >= 'a' && c <= 'f')
				value += c - 'a' + 10;
			else if (c >= 'A' && c <= 'F')
				value += c - 'A' + 10;
			else
				fail();
		}
		return value;
	}
	static void utf8(std::string& text, unsigned c)
	{
		if (c < 128)
			text += char(c);
		else if (c < 2048)
		{
			text += char(0xc0 | (c >> 6));
			text += char(0x80 | (c & 63));
		}
		else if (c < 65536)
		{
			text += char(0xe0 | (c >> 12));
			text += char(0x80 | ((c >> 6) & 63));
			text += char(0x80 | (c & 63));
		}
		else
		{
			text += char(0xf0 | (c >> 18));
			text += char(0x80 | ((c >> 12) & 63));
			text += char(0x80 | ((c >> 6) & 63));
			text += char(0x80 | (c & 63));
		}
	}
	std::string string()
	{
		if (!take('"'))
			fail();
		std::string result;
		while (offset < input.size())
		{
			unsigned char c = input[offset++];
			if (c == '"')
				return result;
			if (c < 32)
				fail();
			if (c != '\\')
			{
				result += c;
				continue;
			}
			if (offset >= input.size())
				fail();
			c = input[offset++];
			if (c == '"' || c == '\\' || c == '/')
				result += c;
			else if (c == 'b')
				result += '\b';
			else if (c == 'f')
				result += '\f';
			else if (c == 'n')
				result += '\n';
			else if (c == 'r')
				result += '\r';
			else if (c == 't')
				result += '\t';
			else if (c == 'u')
			{
				unsigned code = hex();
				if (code >= 0xd800 && code <= 0xdbff)
				{
					if (offset + 2 > input.size() || input[offset++] != '\\' || input[offset++] != 'u')
						fail();
					unsigned low = hex();
					if (low < 0xdc00 || low > 0xdfff)
						fail();
					code = 0x10000 + ((code - 0xd800) << 10) + (low - 0xdc00);
				}
				else if (code >= 0xdc00 && code <= 0xdfff)
					fail();
				utf8(result, code);
			}
			else
				fail();
		}
		fail();
	}
	Json value(unsigned depth)
	{
		if (depth > 64)
			fail();
		whitespace();
		if (offset >= input.size())
			fail();
		Json result;
		if (input[offset] == '"')
		{
			result.type = Json::String;
			result.string = string();
			return result;
		}
		if (take('{'))
		{
			result.type = Json::Object;
			if (take('}'))
				return result;
			do
			{
				auto key = string();
				if (!take(':') || result.object.count(key))
					fail();
				result.object.emplace(key, value(depth + 1));
			} while (take(','));
			if (!take('}'))
				fail();
			return result;
		}
		if (take('['))
		{
			result.type = Json::Array;
			if (take(']'))
				return result;
			do
			{
				result.array.push_back(value(depth + 1));
			} while (take(','));
			if (!take(']'))
				fail();
			return result;
		}
		for (const auto& literal : std::vector<std::string>{"true", "false", "null"})
			if (input.compare(offset, literal.size(), literal) == 0)
			{
				offset += literal.size();
				if (literal != "null")
				{
					result.type = Json::Boolean;
					result.boolean = literal == "true";
				}
				return result;
			}
		const auto first = offset;
		if (input[offset] == '-')
			++offset;
		if (offset >= input.size())
			fail();
		if (input[offset] == '0')
			++offset;
		else
		{
			if (input[offset] < '1' || input[offset] > '9')
				fail();
			while (offset < input.size() && input[offset] >= '0' && input[offset] <= '9')
				++offset;
		}
		auto digits = [&] {
			const auto start = offset;
			while (offset < input.size() && input[offset] >= '0' && input[offset] <= '9')
				++offset;
			if (start == offset)
				fail();
		};
		if (offset < input.size() && input[offset] == '.')
		{
			++offset;
			digits();
		}
		if (offset < input.size() && (input[offset] == 'e' || input[offset] == 'E'))
		{
			++offset;
			if (offset < input.size() && (input[offset] == '+' || input[offset] == '-'))
				++offset;
			digits();
		}
		result.type = Json::Number;
		result.number = std::strtod(input.substr(first, offset - first).c_str(), nullptr);
		if (!std::isfinite(result.number))
			fail();
		return result;
	}

public:
	explicit Reader(const char* text)
		: input(text ? text : "{}")
	{
		if (input.size() > 16 * 1024 * 1024)
			fail();
	}
	Json read()
	{
		auto result = value(0);
		whitespace();
		if (offset != input.size())
			fail();
		return result;
	}
};
}
#endif
