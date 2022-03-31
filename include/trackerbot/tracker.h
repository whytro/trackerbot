#ifndef TRACKERBOT_TRACKER_H
#define TRACKERBOT_TRACKER_H

#include "sql.h"
#include "tracker.h"
#include "trackercfg.h"
#include "types.h"

#include <dpp/dpp.h>
#include <redditcpp/api.h>

#include <memory>

class Tracker {
public:
	Tracker(dpp::cluster* bot, const reddit::AuthInfo& authinfo, std::unique_ptr<TrackerConfig> cfg_handler);
	~Tracker();

	Tracker(const Tracker&) = delete;
	Tracker& operator=(const Tracker&) = delete;
	Tracker(Tracker&&) = delete;
	Tracker& operator=(Tracker&&) = delete;

	void reload_config();

	bool permissions_check(const dpp::interaction_create_t& event, User::Permission req_perm_level);

	void approve_post(const std::string& comment_id, const std::string& supervisor_username, int64_t supervisor_id);
	void deny_post(const std::string& comment_id, const std::string& supervisor_username, int64_t supervisor_id);
	void switch_comment_status(const dpp::interaction_create_t& event, const std::string& comment_id);

	void print_target_list();

	void tracker_thread_initiate();
	void tracker_iterate();
	void update_finder_iterate();
	void update_iterate();

	void add_target_menu(const dpp::interaction_create_t& event, const std::string& target);
	void edit_target_menu(const dpp::interaction_create_t& event, const std::string& target_name);
	void close_target_menu(const std::string& target);
	void change_target_status(const dpp::interaction_create_t& event, const std::string& target_name, Target::Status status);
	void change_target_expertise(const dpp::interaction_create_t& event, const std::string& target_name, const std::string& expertise);
	void add_target_to_tracker(const dpp::interaction_create_t& event, const std::string& target_name);
	void suspend_target(const dpp::interaction_create_t& event, const std::string& target_name);

	int force_update(int days, int interval);

private:
	dpp::cluster* _bot;
	std::shared_ptr<reddit::Api> _reddit_api;
	std::shared_ptr<sql_handler> _sql;
	std::unique_ptr<TrackerConfig> _cfg_handler;
	std::atomic_bool _tracker_on_flag;

	TrackerConfig::Tracker_Config _tracker_config;
	TrackerConfig::Discord_Config _discord_config;
	TrackerConfig::SQL_Config _sql_config;
	TrackerConfig::Format_Config _format_config;

	static int32_t status_color(Target::Status status);
	static std::string status_emote(Target::Status status);
	static std::string status_string(Target::Status status);
	[[nodiscard]] std::string format_comment_for_discord(const std::string& comment_body) const;

	reddit::Comment get_comment(const std::string& comment_id);
	void log_post_action(const std::string& user, const reddit::Comment& comment, bool approved, const std::string& sticky_id);
	
	void send_for_approval(const reddit::Comment& comment);

	std::string generate_sticky_comment(const std::string& author, const std::string& url, int64_t epoch_time, const std::string& comment);
	std::string preformat_sticky_comment(const std::string& author, const std::string& url, int64_t epoch_time, std::string context, const std::string& comment);
	std::string preformat_sticky_comment(const std::string& author, const std::string& url, int64_t epoch_time, const std::string& comment);
	std::string construct_comments(const std::string& thread_id);

	static dpp::embed pre_user_embed(const reddit::UserAbout& user, const reddit::CommentListings& comments, int comment_cap);
	
	int update_thread(const std::string& thread_id, const std::map<std::string, int64_t>& timestamps, bool ignore_edit_checks);
	int update_thread_id(const std::string& thread_id, bool ignore_edit_checks);
	int update_thread_id_by_days(const std::string& thread_id, int days, bool ignore_edit_checks);
};

#endif // TRACKERBOT_TRACKER_H