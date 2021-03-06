#include <dpp/dpp.h>
#include <dpp/fmt/format.h>

#include "trackerbot/tracker.h"

#include <memory>
#include <sstream>

int main() {
    std::unique_ptr<TrackerConfig> cfg = std::make_unique<TrackerConfig>("tracker_config.json");
    const TrackerConfig::Discord_Config discord_cfg = cfg->get_discord_config();
    
    const dpp::snowflake target_server_id = discord_cfg.server_id;
    const int expertise_max = discord_cfg.expertise_max;

    dpp::cluster bot(discord_cfg.token, dpp::i_default_intents);

    Tracker tracker(&bot, std::move(cfg));

    bot.on_ready([&bot, &tracker, target_server_id](const dpp::ready_t& event) {
        std::cout << "Logged in as " << bot.me.username << "!\n";

        if(dpp::run_once<struct register_bot_commands>()) {
            std::vector<dpp::slashcommand> commands;

            dpp::slashcommand tracker_cmd = dpp::slashcommand("addtracker", "Add Reddit User to the Tracker", bot.me.id)
                .add_option(
                    dpp::command_option(dpp::co_string, "username", "Their Reddit Username", true)
                );
            commands.emplace_back(tracker_cmd);

            dpp::slashcommand edit_cmd = dpp::slashcommand("edit", "Edit Target Parameters", bot.me.id)
                .add_option(
                    dpp::command_option(dpp::co_string, "username", "Their Reddit Username", true)
                );
            commands.emplace_back(edit_cmd);

            commands.emplace_back(dpp::slashcommand("debug", "Debug Menu", bot.me.id));

            bot.guild_bulk_command_create(commands, target_server_id);
            std::cout << "Registered Commands\n";

            tracker.tracker_thread_initiate();
            std::cout << "Tracker Started\n";
        }
    });

    bot.on_log([&bot](const dpp::log_t& event) {
        if (event.severity >= dpp::ll_debug) {
            std::cout << dpp::utility::current_date_time() << " [" << dpp::utility::loglevel(event.severity) << "] " << event.message << "\n";
        }
    });

    bot.on_interaction_create([&bot, &tracker](const dpp::interaction_create_t& event) {
        const std::string command = event.command.get_command_name();

        if(command == "addtracker") {
            if(!tracker.permissions_check(event, User::Permission::MANAGEMENT)) {
                return;
            }

            const std::string user = std::get<std::string>(event.get_parameter("username"));
            tracker.add_target_menu(event, user);
        }
        else if(command == "edit") {
            if(!tracker.permissions_check(event, User::Permission::MANAGEMENT)) {
                return;
            }

            const std::string user = std::get<std::string>(event.get_parameter("username"));
            tracker.edit_target_menu(event, user);
        }
        else if(command == "ping") {
            if(!tracker.permissions_check(event, User::Permission::BASIC)) {
                return;
            }

            dpp::message cmd_reply = dpp::message(dpp::ir_channel_message_with_source, "Pong")
                .set_flags(dpp::m_ephemeral);
            event.reply(cmd_reply);
        }
        else if(command == "debug") {
            const dpp::component ping_button = dpp::component()
                .set_label("Ping")
                .set_type(dpp::cot_button)
                .set_style(dpp::cos_primary)
                .set_id("ping");
            const dpp::component list_button = dpp::component()
                .set_label("List Tracked Users")
                .set_type(dpp::cot_button)
                .set_style(dpp::cos_secondary)
                .set_id("print_targetlist");
            const dpp::component force_update_button = dpp::component()
                .set_label("Force Update Posts <30 Days")
                .set_type(dpp::cot_button)
                .set_style(dpp::cos_secondary)
                .set_id("force_update 30");
            const dpp::component action_row = dpp::component()
                .set_type(dpp::cot_action_row)
                .add_component(ping_button)
                .add_component(list_button)
                .add_component(force_update_button);

            const dpp::message res =  dpp::message(event.command.channel_id, "")
                .add_component(action_row)
                .set_flags(dpp::m_ephemeral);

            event.reply(res);
        }
    });

    bot.on_button_click([&bot, &tracker, expertise_max](const dpp::button_click_t& event) {
        std::stringstream ss(event.custom_id);
        std::string command;
        ss >> command;

        if(!tracker.permissions_check(event, User::Permission::MANAGEMENT)) {
            return;
        }
        
        if(command == "approvetracker") {
            std::string target_name;
            ss >> target_name;

            tracker.add_target_to_tracker(event, target_name);
            bot.message_delete(event.command.msg.id, event.command.msg.channel_id);
        }
        else if(command == "rejecttracker") {
            bot.message_delete(event.command.msg.id, event.command.msg.channel_id);
        }
        else if(command == "approvecomment") {
            std::string comment_id;
            ss >> comment_id;
            
            dpp::user user = event.command.usr;
            tracker.approve_post(comment_id, user.username, user.id);
            bot.message_delete(event.command.msg.id, event.command.msg.channel_id);
        }
        else if(command == "rejectcomment") {
            std::string comment_id;
            ss >> comment_id;

            dpp::user user = event.command.usr;
            tracker.deny_post(comment_id, user.username, user.id);
            bot.message_delete(event.command.msg.id, event.command.msg.channel_id);
        }
        else if(command == "switchcomment") {
            std::string comment_id;
            ss >> comment_id;

            tracker.switch_comment_status(event, comment_id);
        }
        else if(command == "edit_expertise") {
            std::string target_name;
            ss >> target_name;

            dpp::component expertise_input = dpp::component()
                .set_label("Expertise")
                .set_type(dpp::cot_text)
                .set_min_length(0)
                .set_max_length(expertise_max)
                .set_text_style(dpp::text_short)
                .set_id("expertise_input " + target_name);
            dpp::interaction_modal_response expertise_modal("expertise_modal "  + target_name, "Edit Expertise");
            expertise_modal.add_component(expertise_input);
            event.dialog(expertise_modal);
        }
        else if(command == "close_menu") {
            std::string target_name;
            ss >> target_name;

            tracker.close_target_menu(target_name);
            bot.message_delete(event.command.msg.id, event.command.msg.channel_id);
        }
        else if(command == "suspend_user") {
            std::string target_name;
            ss >> target_name;

            tracker.suspend_target(event, target_name);
        }
        else if(command == "ping") {
            event.reply("Pong!");
        }
        else if(command == "print_targetlist") {
            tracker.print_target_list(event.command.channel_id);
        }
        else if(command == "force_update") {
            std::string days;
            ss >> days;

            event.reply("Force Updating");
            tracker.force_update(std::stoi(days));
        }
    });

    bot.on_select_click([&bot, &tracker](const dpp::select_click_t& event) {
        std::stringstream ss(event.custom_id);
        std::string command;
        ss >> command;

        if(!tracker.permissions_check(event, User::Permission::MANAGEMENT)) {
            return;
        }

        if(command == "change_status") {
            const std::string status_value = event.values[0];
            Target::Status new_status = Target::Status::UNKNOWN;

            if(status_value == "enable") {
                new_status = Target::Status::ACTIVE;
            }
            else if(status_value == "disable") {
                new_status = Target::Status::PAUSED;
            }
            else if(status_value == "auto") {
                new_status = Target::Status::AUTOMATIC;
            }
            else {
                event.reply("Invalid Status Applied.");
                return;
            }

            std::string target_name;
            ss >> target_name;
            tracker.change_target_status(event, target_name, new_status);

            dpp::message cmd_reply = dpp::message(dpp::ir_channel_message_with_source, "Status Changed.")
                .set_flags(dpp::m_ephemeral);
            event.reply(cmd_reply);
        }
    });

    bot.on_form_submit([&bot, &tracker](const dpp::form_submit_t& event) {
        std::stringstream ss(event.custom_id);
        std::string command;
        ss >> command;

        if(!tracker.permissions_check(event, User::Permission::BASIC)) {
            return;
        }

        if(command == "expertise_modal") {
            std::string target_name;
            ss >> target_name;
            
            const std::string& expertise = std::get<std::string>(event.components[0].components[0].value);
            tracker.change_target_expertise(event, target_name, expertise);

            dpp::message cmd_reply = dpp::message(dpp::ir_channel_message_with_source, "Expertise Changed.")
                .set_flags(dpp::m_ephemeral);
            event.reply(cmd_reply);
        }
    });

    bot.start(false);
}