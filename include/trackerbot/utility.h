#ifndef TRACKERBOT_UTILITY_H
#define TRACKERBOT_UTILITY_H

#include <string>

class Utility {
public:
	static void strip_markdown_formatting(std::string& target);
	static std::string smart_substring(const std::string& longstring, char character, int length);
	static std::string get_lowercase(std::string string);
	
	static void discord_quote_formatting(std::string& string);
	static std::string discord_timestamp_formatting(int64_t epoch_time);

private:
	static void replace_string(std::string& target, const std::string& from, const std::string& to);
};

#endif // TRACKERBOT_UTILITY_H