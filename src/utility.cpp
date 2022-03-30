#include "trackerbot/utility.h"

#include <algorithm>
#include <cctype>
#include <string>

void Utility::replace_string(std::string& target, const std::string& from, const std::string& to) {
    int start_pos = 0;
    while((start_pos = target.find(from, start_pos)) != std::string::npos) {
        target.replace(start_pos, from.length(), to);
        start_pos += to.length();
    }
}
void Utility::strip_markdown_formatting(std::string& target) {
    replace_string(target, "*", std::string());
    replace_string(target, "~~", std::string());
    replace_string(target, "^", std::string());
    replace_string(target, ">", std::string());
    replace_string(target, "`", std::string());
    replace_string(target, "\n", " ");
    replace_string(target, "#", "\\#");
}
std::string Utility::smart_substring(const std::string& longstring, char character, int length) {
    if(longstring.length() < length) {
        return longstring;
    }

    const std::string newstring = longstring.substr(0, length);
    int lastSeparatorIndex = newstring.find_last_of(character);
    if(lastSeparatorIndex == std::string::npos) {
        return newstring + " [...]";
    }

    return newstring.substr(0, lastSeparatorIndex) + " [...]";
}
std::string Utility::get_lowercase(std::string string) {
    std::transform(string.begin(), string.end(), string.begin(), ::tolower);
    return string;
}

void Utility::discord_quote_formatting(std::string& string) {
    replace_string(string, "\n\n", "\n > \n");
}
std::string Utility::discord_timestamp_formatting(int64_t epoch_time) {
    return "<t:" + std::to_string(epoch_time) + ">";
}