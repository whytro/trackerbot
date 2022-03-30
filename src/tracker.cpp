#include "trackerbot/tracker.h"

#include "trackerbot/trackercfg.h"
#include "trackerbot/sql.h"
#include "trackerbot/utility.h"

#include <dpp/dpp.h>
#include <rapidjson/filereadstream.h>
#include <redditcpp/api.h>

#include <exception>
#include <future>
#include <memory>

Tracker::Tracker(dpp::cluster* bot, const reddit::AuthInfo& authinfo, std::unique_ptr<TrackerConfig> cfg_handler) 
    : _bot(bot)
    , _cfg_handler(std::move(cfg_handler))
    , _tracker_on_flag(false)
{   
    _tracker_config = _cfg_handler->get_tracker_config();
    _discord_config = _cfg_handler->get_discord_config();
    _sql_config = _cfg_handler->get_sql_config();
    _format_config = _cfg_handler->get_format_config();

    const std::string target_sub = _tracker_config.target_subreddit;
    const std::string admin_creds = _sql_config.admin_credentials;
    const std::string conn_string = _sql_config.conn_string;
    _sql = std::make_shared<sql_handler>(target_sub, admin_creds, conn_string);

    _reddit_api = std::make_shared<reddit::Api>(authinfo, spdlog::level::debug);
    _reddit_api->authenticate(_reddit_api->browser_get_token());

    const std::unordered_map<std::string, Target> devmap = _sql->get_dev_map();
    _cfg_handler->target_map_reserve(devmap.size());
    for(const auto& itr : devmap) {
        _cfg_handler->target_map_emplace(itr.second);
    }
}
Tracker::~Tracker() {
    _tracker_on_flag = false;
}
void Tracker::reload_config() {
    _cfg_handler->load_config();
}

bool Tracker::permissions_check(dpp::snowflake user_id, User::Permission req_perm_level) {
    const User user = _cfg_handler->user_map_find(user_id);
    return user.permission_level >= req_perm_level;   
}

int32_t Tracker::status_color(Target::Status status) {
    switch(status) {
    case Target::Status::ACTIVE:
        return 0x39DF55;
    case Target::Status::AUTOMATIC:
        return 0x3A30FF;
    case Target::Status::PAUSED:
        return 0xFF0000;
    case Target::Status::SUSPENDED:
        return 0xFF8080;
    default:
        return 0x983356;
    }
}
std::string Tracker::status_emote(Target::Status status) {
    switch(status) {
    case Target::Status::ACTIVE:
        return ":green_circle:";
    case Target::Status::AUTOMATIC:
        return ":purple_circle:";
    case Target::Status::PAUSED:
        return ":orange_circle:";
    case Target::Status::SUSPENDED:
        return ":red_circle:";
    default:
        return ":o:";
    }
}
std::string Tracker::status_string(Target::Status status) {
    switch (status) {
    case Target::Status::ACTIVE:
        return "Active";
    case Target::Status::AUTOMATIC:
        return "Automatic";
    case Target::Status::PAUSED:
        return "Paused";
    case Target::Status::SUSPENDED:
        return "Inactive";
    default:
        return "Unknown";
    }
}
[[nodiscard]] std::string Tracker::format_comment_for_discord(const std::string& comment_body) const {
    std::string text = "> " + Utility::smart_substring(comment_body, '.', _format_config.total_char_limit);
    Utility::discord_quote_formatting(text);
    
    return text;
}

std::string Tracker::generate_sticky_comment(const std::string& author, const std::string& url, int64_t epoch_time, const std::string& comment) {
    Target target = _cfg_handler->target_map_find(author);

    time_t t_epoch = epoch_time;
    std::stringstream ss;
    ss << std::put_time(std::gmtime(&t_epoch), "%F %X");
    const std::string date_time = ss.str();

    if(target.is_empty() || target.data->expertise.empty()) {
        return fmt::format(_format_config.entry, author, url, date_time, comment);
    }
    return fmt::format(_format_config.entry_wexpertise, author, url, target.data->expertise, date_time, comment);
}
std::string Tracker::preformat_sticky_comment(const std::string& author, const std::string& url, int64_t epoch_time, std::string context, const std::string& comment) {
    Utility::strip_markdown_formatting(context);
    Utility::smart_substring(context, ' ', _format_config.context_char_limit);
    const std::string formatted_context = fmt::format(_format_config.context, context);
    
    Utility::smart_substring(comment, '.',  _format_config.total_char_limit);
    const std::string formatted_comment = fmt::format(_format_config.comment, comment);
    const std::string combined_text = formatted_context + formatted_comment;

    return generate_sticky_comment(author, url, epoch_time, combined_text);
}
std::string Tracker::preformat_sticky_comment(const std::string& author, const std::string& url, int64_t epoch_time, const std::string& comment) {
    const std::string formatted_comment = fmt::format(_format_config.comment, comment);
    const std::string trimmed_comment = Utility::smart_substring(formatted_comment, '.',  _format_config.total_char_limit);

    return generate_sticky_comment(author, url, epoch_time, trimmed_comment);
}
std::string Tracker::construct_comments(const std::string& thread_id) {
    const std::string target_subreddit = _tracker_config.target_subreddit;
    const std::vector<sql_handler::Comment_Response> other_comments = _sql->get_comments_in_thread(thread_id);
    const std::unordered_map<std::string, std::string> contexts = _sql->get_contexts_for_thread(thread_id);
    
    std::string cumulative_text;
    const uint64_t comment_text_size = other_comments.size() * _format_config.total_char_limit;
    const uint64_t context_text_size = contexts.size() * _format_config.context_char_limit;
    cumulative_text.reserve(comment_text_size + context_text_size);

    for(const auto& itr : other_comments) {
        const auto context_itr = contexts.find(itr.comment_id);
        const std::string comment_url = fmt::format("/r/{}/comments/{}/-/{}/", target_subreddit, itr.thread_id, itr.comment_id);
        
        if(context_itr != contexts.end()) {
            cumulative_text += preformat_sticky_comment(itr.author, comment_url, itr.epoch_time, context_itr->second, itr.comment_text);
        }
        else {
            cumulative_text += preformat_sticky_comment(itr.author, comment_url, itr.epoch_time, itr.comment_text);
        }
        cumulative_text += "\n\n";
    }
    
    return cumulative_text;
}

void Tracker::approve_post(const std::string& comment_id, const std::string& supervisor_username, int64_t supervisor_id) {
    const reddit::CommentListings commentlistings = _reddit_api->get_comment("t1_" + comment_id);

    if(commentlistings.data.dist == 0) {
        throw std::runtime_error("Invalid Comment ID was provided, resulting in 0 results.");
    }

    const reddit::Comment comment = commentlistings.children[0];
    if(comment.author == "[deleted]") {
        log_post_action("Invalid Post - Deleted", comment, false, "");
        return;
    }

    _sql->change_comment_status(comment.id, 1, supervisor_username, supervisor_id);
    const std::string trimmed_link_id = comment.link_id.substr(3,9);
    const std::string sticky_comment = construct_comments(trimmed_link_id) + _format_config.footer;
    const std::string sticky_id = _sql->get_sticky_id(trimmed_link_id);

    if(sticky_id.empty()) {
        const reddit::Comment posted_comment = _reddit_api->post_comment(comment.link_id, sticky_comment);
        _reddit_api->distinguish(posted_comment.name, true);
        _reddit_api->lock(posted_comment.name);
        _sql->insert_thread(trimmed_link_id, posted_comment.id);
        log_post_action(supervisor_username, comment, true, posted_comment.id);
    }
    else {
        _reddit_api->edit_comment("t1_" + sticky_id, sticky_comment);
        log_post_action(supervisor_username, comment, true, sticky_id);
    }
}
void Tracker::deny_post(const std::string& comment_id, const std::string& supervisor_username, int64_t supervisor_id) {
    const reddit::CommentListings commentlistings = _reddit_api->get_comment("t1_" + comment_id);

    if(commentlistings.data.dist == 0) {
        throw std::runtime_error("Invalid Comment ID was provided, resulting in 0 results.");
    }

    const reddit::Comment comment = commentlistings.children[0];
    _sql->change_comment_status(comment.id, 0, supervisor_username, supervisor_id);

    log_post_action(supervisor_username, comment, false, "");
}
void Tracker::log_post_action(const std::string& user, const reddit::Comment& comment, bool approved, const std::string& sticky_id) {
    const int32_t color = approved ? 0x00FF00 : 0xFF0000;
    const std::string action_string = approved ? "Approved by: " : "Denied by: ";

    dpp::embed embed = dpp::embed()
        .set_title(comment.subreddit_name_prefixed)
        .set_color(color)
        .set_description(comment.author)
        .add_field(
            "Post Link",
            "https://www.reddit.com" + comment.permalink
        )
        .add_field(
            "Post Text",
            format_comment_for_discord(comment.body)
        )
        .set_footer(action_string + user, "");

    if(approved) {
        const std::string trimmed_link_id = comment.link_id.substr(3,9);
        embed.add_field(
            "Sticky Link",
            fmt::format("https://www.reddit.com/r/{}/comments/{}/-/{}/", 
                comment.subreddit, trimmed_link_id, sticky_id)
        );
    }  

    const dpp::component reverse_button = dpp::component()
        .set_label("Reverse Action")
        .set_type(dpp::cot_button)
        .set_style(dpp::cos_primary)
        .set_id("switchcomment " + comment.id);
    const dpp::component action_row = dpp::component()
        .set_type(dpp::cot_action_row)
        .add_component(reverse_button);
        
    const dpp::message log_msg = dpp::message(_discord_config.log_channel, embed)
        .add_component(action_row);

    _bot->message_create(log_msg, [](const dpp::confirmation_callback_t& msg_callback) {
        if(msg_callback.is_error()) {
            throw std::runtime_error("Failed to create Log Message:\n" + msg_callback.get_error().message);
        }
    });
}
void Tracker::switch_comment_status(const dpp::interaction_create_t& event, const std::string& comment_id) {
    if(!_sql->check_comment_existence(comment_id)) {
        throw std::runtime_error("Provided Comment ID does not exist in SQL Table.");
    }

    const bool changed_status = !_sql->get_comment_status(comment_id);

    const dpp::user invoker = event.command.usr;
    const std::string thread_id = _sql->change_comment_status(comment_id, static_cast<int>(changed_status), invoker.username, invoker.id);
    update_thread_id(thread_id, true);

    const std::string status_string = changed_status ? "Changed to Approved by: " : "Changed to Denied by: ";

    dpp::embed embed = dpp::embed()
        .set_title("Comment Status Changed")
        .set_color(0xD133FF)
        .set_footer(status_string + invoker.username, "");

    event.reply(dpp::message(event.command.channel_id, embed));
}

void Tracker::send_for_approval(const reddit::Comment& comment) {
    const dpp::embed embed = dpp::embed()
        .set_title(comment.subreddit_name_prefixed)
        .set_color(0xFF8300)
        .set_description(comment.author)
        .add_field(
            "Post Link",
            fmt::format("https://www.reddit.com{0}?context=1", comment.permalink)
        )
        .add_field(
            "Context",
            "> Response to Thread"
        )
        .add_field(
            "Post Text",
            format_comment_for_discord(comment.body)
        )
        .set_timestamp(comment.created_utc);

    const dpp::component approve_button = dpp::component()
        .set_label("Approve")
        .set_type(dpp::cot_button)
        .set_style(dpp::cos_success)
        .set_id(fmt::format("approvecomment {}", comment.id));
    const dpp::component reject_button = dpp::component()
        .set_label("Reject")
        .set_type(dpp::cot_button)
        .set_style(dpp::cos_danger)
        .set_id(fmt::format("rejectcomment {}", comment.id));
    const dpp::component action_row = dpp::component()
        .set_type(dpp::cot_action_row)
        .add_component(approve_button)
        .add_component(reject_button);

    const dpp::message approval_msg = dpp::message(_discord_config.managing_channel, embed)
        .add_component(action_row);

    _bot->message_create(approval_msg, [](const dpp::confirmation_callback_t& msg_callback) {
        if(msg_callback.is_error()) {
            throw std::runtime_error("Failed to create Approval Message:\n" + msg_callback.get_error().message);
        }
    });
}

void Tracker::print_target_list() {
    const std::vector<Target::Data> targets = _cfg_handler->get_targets_vector_data();
    const int64_t channel_id = _discord_config.managing_channel;
    const int discord_field_size = 25;
    
    std::vector<dpp::message> message_batches;
    message_batches.reserve(targets.size() / discord_field_size + 1);

    const dpp::embed first_embed = dpp::embed()
        .set_title(fmt::format("{} Users Tracked", targets.size()))
        .set_color(0xFF8300);

    message_batches.emplace_back(dpp::message(channel_id, first_embed));

    for(int i = 0; i < targets.size();) {
        std::vector<dpp::embed_field> user_fields;
        user_fields.reserve(discord_field_size);

        const int64_t modulus_count = targets.size() % discord_field_size;
        const int64_t count = (modulus_count == 0) ? discord_field_size : modulus_count;

        for(int j = 0; j < count; ++j) {
            Target::Data current_target = targets[i];
            const std::string status_symbol = status_emote(current_target.status);
            const std::string status_name = status_string(current_target.status);
            const std::string expertise = current_target.expertise;
            const std::string value = fmt::format("{0}\n{1} {2}", 
                expertise, status_symbol, status_name);

            dpp::embed_field current_field;
            current_field.is_inline = true;
            current_field.name = current_target.username;
            current_field.value = value;

            user_fields.emplace_back(current_field);
        }
        i += count;

        dpp::embed tracker_batch_msg = dpp::embed();
        tracker_batch_msg.fields = user_fields;
        message_batches.emplace_back(dpp::message(channel_id, tracker_batch_msg));
    }

    for(const auto& message_itr : message_batches) {
        _bot->message_create(message_itr, [](const dpp::confirmation_callback_t& msg_callback) {
            if(msg_callback.is_error()) {
                throw std::runtime_error("Failed to create Target List:\n" + msg_callback.get_error().message);
            }
        });
    }
}

void Tracker::tracker_thread_initiate() {
    _tracker_on_flag = true;

    std::thread([&]() {
        while(_tracker_on_flag) {
            tracker_iterate();
            update_finder_iterate();
            update_iterate();
            std::this_thread::sleep_for(std::chrono::seconds(_tracker_config.tracker_interval));
            std::cout << "Tracker Iterated" << std::endl;
        }
    }).detach();
}
void Tracker::tracker_iterate() {
    const reddit::Listing input = reddit::Listing().limit(_tracker_config.tracker_iterate_amount);
    const reddit::CommentListings comments = _reddit_api->subreddit("friends").get_comments(input);

    const std::string target_subreddit = _tracker_config.target_subreddit;
    const float minimum_epoch = _tracker_config.minimum_epoch;

    //Process Contexts
    std::vector<std::string> context_ids;
    context_ids.reserve(comments.children.size());

    for(const auto& itr : comments.children) {
        if(itr.subreddit == target_subreddit && itr.parent_id.substr(0,2) == "t1") {
            context_ids.emplace_back(itr.parent_id);
        }        
    }
    reddit::CommentListings contexts = _reddit_api->get_comments(context_ids);

    auto process_comment = [&](const reddit::Comment& comment) {
        const bool subcheck = Utility::get_lowercase(comment.subreddit) == Utility::get_lowercase(target_subreddit);

        if(comment.created_utc < minimum_epoch || !subcheck) {
            return;
        }

        Target target = _cfg_handler->target_map_find(comment.author);
        if(target.is_empty() || _sql->check_comment_existence(comment.id)) {
            return;
        }

        const std::string trimmed_link_id = comment.link_id.substr(3,9);
        const float timestamp = comment.edited ? comment.edited : comment.created_utc;
        _sql->insert_comment(comment.id, trimmed_link_id, comment.author,
            0, "", -1, timestamp, comment.body);

        if(comment.parent_id.substr(0,2) == "t1") {
            for(const auto& itr : contexts.children) {
                //check if parent comment is the thread itself
                if(itr.name != comment.parent_id) {
                    _sql->insert_context(itr.id, trimmed_link_id, comment.id, true, itr.body);
                }
            }
        }

        if(target.data->status == Target::Status::ACTIVE) {
            send_for_approval(comment);
            std::this_thread::sleep_for(std::chrono::seconds(2));
        }	
        else if(target.data->status == Target::Status::AUTOMATIC) {
            approve_post(comment.id, "Automatic", 0);
        }
    };

    for(const auto& itr : comments.children) {
        process_comment(itr);
    }
}
void Tracker::update_finder_iterate() {
    std::vector<std::string> entry_ids = _sql->get_comment_ids_by_date(_tracker_config.update_day_limit);

    if(entry_ids.empty()) {
        return;
    }

    for(auto& itr : entry_ids) {
        itr.reserve(3);
        itr.insert(0, "t1_");
    }
    const reddit::CommentListings comments = _reddit_api->get_comments(entry_ids);
    const std::map<std::string, int64_t> entry_timestamps = _sql->get_comment_id_epoch_pair_by_date(_tracker_config.update_day_limit);

    for(const auto& itr : comments.children) {
        if(itr.author == "[deleted]") {
            _sql->delete_comment(itr.id);
        }
        else {
            if(itr.edited == 0.0F || itr.edited == entry_timestamps.at(itr.id)) {
                continue;
            }

            _sql->update_comment(itr.id, itr.body, itr.edited);
        }
        const std::string trimmed_link_id = itr.link_id.substr(3,9);
        _sql->enqueue_update(trimmed_link_id);
    }
}
void Tracker::update_iterate() {
    const std::unordered_set<std::string> update_queue = _sql->get_update_queue();

    for (const auto& thread_id_itr : update_queue) {
        const std::vector<sql_handler::Comment_Response> stored_comments = _sql->get_comments_in_thread(thread_id_itr);
        std::string cumulative_text = construct_comments(thread_id_itr);
        const std::string sticky_id = _sql->get_sticky_id(thread_id_itr);

        if(!cumulative_text.empty()) {
            cumulative_text += _format_config.footer;
            if(!sticky_id.empty()) {
                _reddit_api->edit_comment("t1_" + sticky_id, cumulative_text);
            }
            else {
                const std::string link_id = stored_comments.front().thread_id;
                reddit::Comment posted_comment = _reddit_api->post_comment("t3_" + link_id, cumulative_text);
                _reddit_api->distinguish(posted_comment.name, true);
                _reddit_api->lock(posted_comment.name);
                _sql->insert_thread(thread_id_itr, posted_comment.id);
            }
        }
        else {
            _sql->begin_transaction();
            _sql->delete_thread(thread_id_itr);
            _reddit_api->edit().del("t1_" + sticky_id);
            _sql->commit_transaction();
        }
        
        _sql->dequeue_update(thread_id_itr);
        std::this_thread::sleep_for(std::chrono::seconds(3));
    }
}

dpp::embed Tracker::pre_user_embed(const reddit::UserAbout& user, const reddit::CommentListings& comments, int comment_cap) {
    std::string rc_contents = comments.data.dist == 0 ? ">>> No Recent Comments" : ">>> ";

    for(const auto& itr : comments.children) {
        const std::string header = fmt::format("<t:{0}>\n", itr.created_utc);
        const std::string comment_preview = Utility::smart_substring(itr.body, '.', comment_cap);
        rc_contents += header + comment_preview + "\n\n";
    }

    const std::string creation_date = fmt::format("Account Created: <t:{0}>", user.created_utc);
    const std::string reddit_url = fmt::format("https://www.reddit.com/user/{0}/", user.name);

    dpp::embed_author author;
    author.name = user.name;
    author.url = reddit_url;
    author.icon_url = user.icon_img;

    const dpp::embed embed = dpp::embed()
        .set_author(author)
        .set_color(0xFF0000)
        .set_description(creation_date+"\n"+reddit_url)
        .add_field(
            "Recent Comments",
            rc_contents
        );
    
    return embed;
}
void Tracker::add_target_menu(const dpp::interaction_create_t& event, const std::string& target) {
    Target searched_target = _cfg_handler->target_map_find(target);
    if(!searched_target.is_empty() && searched_target.data->status != Target::SUSPENDED) {
        const dpp::message ereply = dpp::message("Target already exists.")
            .set_flags(dpp::m_ephemeral);
        event.reply(ereply);
        return;
    }

    _bot->interaction_response_create(event.command.id, event.command.token, 
        dpp::interaction_response(dpp::ir_deferred_channel_message_with_source, dpp::message("*")),
        [this, event, target](const dpp::confirmation_callback_t& msg_callback){
            if(msg_callback.is_error()) {
                event.edit_response("Error occurred - Please try again.");
                return;
            }

            const dpp::component confirm_button = dpp::component()
                .set_label("Confirm")
                .set_type(dpp::cot_button)
                .set_style(dpp::cos_success)
                .set_id("approvetracker " + target);
            const dpp::component cancel_button = dpp::component()
                .set_label("Cancel")
                .set_type(dpp::cot_button)
                .set_style(dpp::cos_danger)
                .set_id("rejecttracker");
            const dpp::component action_row = dpp::component()
                .set_type(dpp::cot_action_row)
                .add_component(confirm_button)
                .add_component(cancel_button);

            reddit::User targeted_user = _reddit_api->user(target);
            reddit::UserAbout userinfo = targeted_user.get_about();
            reddit::Listing usercomment_input = reddit::Listing().limit(3);
            reddit::CommentListings usercomments = targeted_user.get_comments(usercomment_input);  

            const dpp::embed user_card_embed = pre_user_embed(userinfo, usercomments, 150);
            const dpp::message res =  dpp::message(event.command.channel_id, user_card_embed)
                .add_component(action_row);

            event.edit_response(res);
        }
    );
}
void Tracker::edit_target_menu(const dpp::interaction_create_t& event, const std::string& target_name) {    
    Target target = _cfg_handler->target_map_find(target_name);
    if(target.is_empty()) {
        const dpp::message ereply = dpp::message("User doesn't exist in the Tracker.")
            .set_flags(dpp::m_ephemeral);
        event.reply(ereply);
        return;
    }
    if(target.data->managing_msg_id != 0) {
        std::promise<bool> message_existence;

        _bot->message_get(target.data->managing_msg_id, target.data->msg_channel, 
            [event, &message_existence](const dpp::confirmation_callback_t& msg_callback) 
        {
            message_existence.set_value(!msg_callback.is_error());
        });

        if(message_existence.get_future().get()) {
            event.reply( "User already has an edit instance open.");
            return;
        }
    }
    
    _bot->interaction_response_create(event.command.id, event.command.token, 
        dpp::interaction_response(dpp::ir_deferred_channel_message_with_source, dpp::message("*")),
        [this, event, target](const dpp::confirmation_callback_t& msg_callback){
            if(msg_callback.is_error()) {
                event.edit_response("Error occurred - Please try again.");
                return;
            }

            const std::string desc_string = target.data->expertise.empty() ? "Unassigned" : target.data->expertise;

            const dpp::embed embed = dpp::embed()
                .set_title(target.data->username)
                .set_color(status_color(target.data->status))
                .set_description("Expertise: " + desc_string)
                .add_field(
                    "Status",
                    status_emote(target.data->status) + " " + status_string(target.data->status)
                )
                .set_footer("\nChanges are applied immediately", "");

            const dpp::component dropdown_selections = dpp::component()
                .set_label("Status")
                .set_type(dpp::cot_selectmenu)
                .set_placeholder("Change Status")
                .add_select_option(dpp::select_option("Enable", "enable", "Enable Tracking")
                    .set_emoji(u8"🟢"))
                .add_select_option(dpp::select_option("Disable", "disable", "Disable Tracking")
                    .set_emoji(u8"🟠"))
                .add_select_option(dpp::select_option("Automatic", "auto", "Automatically Approve All Posts")
                    .set_emoji(u8"🟣"))
                .set_id("change_status " + target.data->username);
            const dpp::component selection_component = dpp::component()
                .add_component(dropdown_selections);

            const dpp::component adjust_expertise_button = dpp::component()
                .set_label("Edit Expertise")
                .set_type(dpp::cot_button)
                .set_emoji(u8"📝")
                .set_id("edit_expertise " + target.data->username);
            const dpp::component delete_button = dpp::component()
                .set_label("Suspend User")
                .set_type(dpp::cot_button)
                .set_style(dpp::cos_danger)
                .set_id("suspend_user " + target.data->username);
            const dpp::component close_button = dpp::component()
                .set_label("Close Menu")
                .set_type(dpp::cot_button)
                .set_style(dpp::cos_secondary)
                .set_id("close_menu " + target.data->username);
            const dpp::component action_row = dpp::component()
                .set_type(dpp::cot_action_row)
                .add_component(adjust_expertise_button)
                .add_component(delete_button)
                .add_component(close_button);
            const dpp::message menu_msg = dpp::message(event.command.channel_id, embed)
                .add_component(selection_component)
                .add_component(action_row);

            event.edit_response(menu_msg);
            event.get_original_response([this, target](const dpp::confirmation_callback_t& msg_callback) {
                const dpp::message& finalized_msg = std::get<dpp::message>(msg_callback.value);
                
                target.data->managing_msg_id = finalized_msg.id;
                target.data->msg_channel = finalized_msg.channel_id;
                _sql->insert_devedit_session(target.data->username, finalized_msg.id, finalized_msg.channel_id);
            });
        }
    );
}
void Tracker::close_target_menu(const std::string& target) {
    _sql->delete_devedit_session(target);
}
void Tracker::change_target_status(const dpp::interaction_create_t& event, const std::string& target_name, Target::Status status) {
    Target target = _cfg_handler->target_map_find(target_name);

    const Target::Status old_status = target.data->status;
    target.data->status = status;

    const dpp::embed embed = dpp::embed()
        .set_title("Status Changed")
        .set_color(status_color(status))
        .add_field(
            target.data->username,
            fmt::format("{} ⟶ {}", status_string(old_status), status_string(status))
        )
        .set_footer(event.command.usr.username, "");

    const dpp::message log_msg(_discord_config.log_channel, embed);
    _bot->message_create(log_msg, [=](const dpp::confirmation_callback_t& msg_callback) {
        if(msg_callback.is_error()) {
            event.reply("Error occurred - please try again.");
        }
        else {
            _sql->update_dev_status(target.data->username, status, event.command.usr.username, event.command.usr.id);
        }
    });
}
void Tracker::change_target_expertise(const dpp::interaction_create_t& event, const std::string& target_name, const std::string& expertise) {
    Target target = _cfg_handler->target_map_find(target_name);
    target.data->expertise = expertise;

    const dpp::embed embed = dpp::embed()
        .set_title("Expertise Changed")
        .add_field(
            target.data->username,
            "Assigned: " + expertise
        )
        .set_footer(event.command.usr.username, "");

    const dpp::message log_msg(_discord_config.log_channel, embed);
    _bot->message_create(log_msg, [=](const dpp::confirmation_callback_t& msg_callback) {
        if(msg_callback.is_error()) {
            event.reply("Error occurred - please try again.");
        }
        else {
            _sql->update_dev_expertise(target.data->username, expertise, event.command.usr.username, event.command.usr.id);
        }
    });
}
void Tracker::add_target_to_tracker(const dpp::interaction_create_t& event, const std::string& target_name) {
    const reddit::FriendResponse resp = _reddit_api->user(target_name).add_friend();

    Target target;
    target.data = std::make_shared<Target::Data>();
    target.data->username = resp.name;
    target.data->status = Target::Status::ACTIVE;

    _sql->upsert_dev(target.data->username, target.data->expertise, target.data->status,
        event.command.usr.username, event.command.usr.id);

    _cfg_handler->target_map_emplace(target);
}
void Tracker::suspend_target(const dpp::interaction_create_t& event, const std::string& target_name) {
    _reddit_api->user(target_name).remove_friend();

    change_target_status(event, target_name, Target::Status::SUSPENDED);
    _cfg_handler->target_map_remove(target_name);
}

int Tracker::update_thread(const std::string& thread_id, const std::map<std::string, int64_t>& timestamps, bool ignore_edit_checks) {
    std::vector<std::string> entry_ids = _sql->get_comment_ids_by_thread_id(thread_id);
    
    for(auto& itr : entry_ids) {
        itr.reserve(3);
        itr.insert(0, "t1_");
    }
    const reddit::CommentListings info = _reddit_api->get_comments(entry_ids);

    int update_count = 0;

    if(info.data.dist != 0) {
        for(const auto& comment : info.children) {
            if(comment.author == "[deleted]") {
                _sql->delete_comment(comment.id);
            }
            else {
                if(ignore_edit_checks) {
                    const float timestamp = (comment.edited != 0.0F) ? comment.edited : comment.created_utc;
                    _sql->update_comment(comment.id, comment.body, timestamp);
                }
                else {
                    if(comment.edited == 0.0F || comment.edited == timestamps.at(comment.id)){
                        continue;
                    }

                    _sql->update_comment(comment.id, comment.body, comment.edited);
                }
            }
            ++update_count;
        }
    }
    _sql->enqueue_update(thread_id);

    return update_count;
}
int Tracker::update_thread_id(const std::string& thread_id, bool ignore_edit_checks) {
    const std::map<std::string, int64_t> entry_timestamps = _sql->get_comment_id_epoch_pair_by_thread_id(thread_id);
    return update_thread(thread_id, entry_timestamps, ignore_edit_checks);
}
int Tracker::update_thread_id_by_days(const std::string& thread_id, int days, bool ignore_edit_checks) {
    const std::map<std::string, int64_t> entry_timestamps = _sql->get_comment_id_epoch_pair_by_date(days);
    return update_thread(thread_id, entry_timestamps, ignore_edit_checks);
}

int Tracker::force_update(int days, int interval) {
    int total_updated = 0;

    const std::vector<std::string> thread_ids = _sql->get_thread_ids_by_date(days);
    for(const auto& thread_id : thread_ids) {
        total_updated += update_thread_id_by_days(thread_id, days, true);
        std::this_thread::sleep_for(std::chrono::seconds(interval));
    }

    return total_updated;
}

