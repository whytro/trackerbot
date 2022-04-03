#ifndef TRACKERBOT_TRACKERCFG_H
#define TRACKERBOT_TRACKERCFG_H

#include "trackerbot/types.h"

#include <string>
#include <unordered_map>
#include <vector>

class TrackerConfig {
public:
    explicit TrackerConfig(std::string cfg_path);

    struct Tracker_Config {
        std::string target_subreddit;
        int tracker_interval = 0;
        int tracker_iterate_amount = 0;
        int update_day_limit = 0;
        float minimum_epoch = 0;
    };
    struct Reddit_Config {
        std::string client_id;
        std::string client_secret;
        std::string redirect_uri;
        std::string scope;
        std::string user_agent;
        std::string refresh_token;
    };
    struct Discord_Config {
        std::string token;
        int expertise_max = 0;
        int64_t server_id = 0;
        int64_t managing_channel = 0;
        int64_t queue_channel = 0;
        int64_t log_channel = 0;
    };
    struct SQL_Config {
        std::string admin_credentials;
        std::string conn_string;
    };
    struct Format_Config {
        int total_char_limit = 0;
        int context_char_limit = 0;
        std::string entry;
        std::string entry_wexpertise;
        std::string context;
        std::string comment;
        std::string footer;
    };

    void load_config();

    Tracker_Config get_tracker_config();
    Reddit_Config get_reddit_config();
    Discord_Config get_discord_config();
    SQL_Config get_sql_config();
    Format_Config get_format_config();

    Target target_map_find(const std::string& username);
    void target_map_reserve(int reserve_count);
    void target_map_emplace(const Target& target);
    void target_map_remove(const std::string& username);
    User user_map_find(int64_t user_id);
    std::vector<Target::Data> get_targets_vector_data();

private:
    std::string _cfg_path;

    Tracker_Config _tracker_config;
    Reddit_Config _reddit_config;
    Discord_Config _discord_config;
    SQL_Config _sql_config;
    Format_Config _format_config;

    // //Username - Tracked User
    std::unordered_map<std::string, Target> _target_map;
    // //Managing Message ID - Tracked User
    // std::unordered_map<int64_t, std::shared_ptr<Tracking_Target>> _managing_map;

    //Discord User ID - User
    std::unordered_map<int64_t, User> _user_map;
};

#endif // TRACKERBOT_TRACKERCFG_H