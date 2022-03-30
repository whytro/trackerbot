#include "trackerbot/trackercfg.h"

#include "trackerbot/sql.h"
#include "trackerbot/utility.h"

#include <rapidjson/document.h>
#include <rapidjson/filereadstream.h>

#include <unordered_map>
#include <vector>

TrackerConfig::TrackerConfig(std::string cfg_path)
    : _cfg_path(std::move(cfg_path)) 
{
    load_config();
}

void TrackerConfig::load_config() {
    FILE* fp = fopen(_cfg_path.c_str(), "r");
    char read_buffer[65536];
    rapidjson::FileReadStream is(fp, read_buffer, sizeof(read_buffer));
    
    rapidjson::Document doc;
    doc.ParseStream(is);
    fclose(fp);
    
    if(doc.HasParseError()) {
        throw std::runtime_error("Failed to parse config file.");
    }

    if(!(doc.HasMember("Tracker_Config") && doc.HasMember("Discord_Config") 
        && doc.HasMember("SQL_Config") && doc.HasMember("Format_Config") 
        && doc.HasMember("Users"))) 
    {
        throw std::runtime_error("Config file is invalid.");
    }

    rapidjson::Value& tracker_cfg = doc["Tracker_Config"];
    _tracker_config.target_subreddit = tracker_cfg["Target_Subreddit"].GetString();
    _tracker_config.tracker_interval = tracker_cfg["Tracker_Interval"].GetInt();
    _tracker_config.tracker_iterate_amount = tracker_cfg["Tracker_Iterate_Amount"].GetInt();
    _tracker_config.update_day_limit = tracker_cfg["Update_Day_Limit"].GetInt();
    _tracker_config.minimum_epoch =tracker_cfg["Minimum_Epoch"].GetFloat();

    rapidjson::Value& reddit_cfg = doc["Reddit_Config"];
    _reddit_config.client_id = reddit_cfg["Client_Id"].GetString();
    _reddit_config.client_secret = reddit_cfg["Client_Secret"].GetString();
    _reddit_config.redirect_uri = reddit_cfg["Redirect_URI"].GetString();
    _reddit_config.scope = reddit_cfg["Scope"].GetString();
    _reddit_config.user_agent = reddit_cfg["User_Agent"].GetString();

    rapidjson::Value& discord_cfg = doc["Discord_Config"];
    _discord_config.token = discord_cfg["Token"].GetString();
    _discord_config.expertise_max = discord_cfg["Expertise_Max"].GetInt();
    _discord_config.server_id = discord_cfg["Server_Id"].GetUint64();
    _discord_config.managing_channel = discord_cfg["Managing_Channel"].GetUint64();
    _discord_config.queue_channel = discord_cfg["Queue_Channel"].GetUint64();
    _discord_config.log_channel = discord_cfg["Log_Channel"].GetUint64();

    rapidjson::Value& sql_cfg = doc["SQL_Config"];
    _sql_config.admin_credentials = sql_cfg["Admin_Credentials"].GetString();
    _sql_config.conn_string = sql_cfg["Connection_String"].GetString();

    rapidjson::Value& format_cfg = doc["Format_Config"];
    _format_config.total_char_limit = format_cfg["Main_Char_Limit"].GetInt();
    _format_config.context_char_limit = format_cfg["Context_Char_Limit"].GetInt();
    _format_config.entry = format_cfg["Entry"].GetString();
    _format_config.entry_wexpertise = format_cfg["Entry_wExpertise"].GetString();
    _format_config.context = format_cfg["Context"].GetString();
    _format_config.comment = format_cfg["Comment"].GetString();
    _format_config.footer = format_cfg["Footer"].GetString();

    if(doc.HasMember("Users")) {
        for(const auto& itr : doc["Users"].GetObject()) {
            User user;
            user.username = itr.name.GetString();
            user.snowflake = itr.value["User_Id"].GetUint64();
            user.permission_level = static_cast<User::Permission>(itr.value["Permissions_Level"].GetInt());

            _user_map.emplace(user.snowflake, user);
        }
    }
}

TrackerConfig::Tracker_Config TrackerConfig::get_tracker_config() {
    return _tracker_config;
}
TrackerConfig::Reddit_Config TrackerConfig::get_reddit_config() {
    return _reddit_config;
}
TrackerConfig::Discord_Config TrackerConfig::get_discord_config() {
    return _discord_config;
}
TrackerConfig::SQL_Config TrackerConfig::get_sql_config() {
    return _sql_config;
}
TrackerConfig::Format_Config TrackerConfig::get_format_config() {
    return _format_config;
}

Target TrackerConfig::target_map_find(const std::string& username) {
    const auto itr = _target_map.find(Utility::get_lowercase(username));
    if(itr == _target_map.end()) {
        return Target();
    }

    return itr->second;
}
void TrackerConfig::target_map_reserve(int reserve_count) {
    _target_map.reserve(reserve_count);
}
void TrackerConfig::target_map_emplace(const Target& target) {
    const std::string username = Utility::get_lowercase(target.data->username);
    _target_map.emplace(username, target);
}
void TrackerConfig::target_map_remove(const std::string& username) {
    const auto itr = _target_map.find(Utility::get_lowercase(username));
    _target_map.erase(itr);
}
User TrackerConfig::user_map_find(int64_t user_id) {
    const auto itr = _user_map.find(user_id);
    if(itr == _user_map.end()) {
        return User();
    }

    return itr->second;
}
std::vector<Target::Data> TrackerConfig::get_targets_vector_data() {
    std::vector<Target::Data> res;
    res.reserve(_target_map.size());
    for(const auto& itr : _target_map) {
        res.emplace_back(*(itr.second.data));
    }

    return res;
}