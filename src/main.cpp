#include <dpp/dpp.h>
#include <dpp/fmt/format.h>

#include "trackerbot/commands.h"
#include "trackerbot/tracker.h"

#include <memory>
#include <sstream>

int main() {
    std::unique_ptr<TrackerConfig> cfg = std::make_unique<TrackerConfig>("tracker_config.json");
    const TrackerConfig::Discord_Config discord_cfg = cfg->get_discord_config();

    const int64_t target_guild = discord_cfg.server_id;
    const int expertise_max = discord_cfg.expertise_max;

    dpp::cluster bot(discord_cfg.token, dpp::i_default_intents);
    Tracker tracker(&bot, std::move(cfg));
    Commands_Handler cmd_handler(&bot, &tracker);

    bot.on_ready([&bot, &tracker, target_guild](const dpp::ready_t& event) {
        std::cout << "Logged in as " << bot.me.username << "!\n";

        if(dpp::run_once<struct register_bot_commands>()) {
            dpp::slashcommand debug_cmd("debug", "Debug Menu", bot.me.id);
            bot.guild_command_create_sync(debug_cmd, target_guild);
            
            tracker.tracker_thread_initiate();
            std::cout << "Tracker Started\n";
        }
    });

    bot.on_log([&bot](const dpp::log_t& event) {
        if (event.severity >= dpp::ll_debug) {
            std::cout << dpp::utility::current_date_time() << " [" << dpp::utility::loglevel(event.severity) << "] " << event.message << "\n";
        }
    });

    bot.on_interaction_create([&cmd_handler](const dpp::interaction_create_t& event) {
        const std::string command = event.command.get_command_name();

        cmd_handler.interact(command, event);
    });

    bot.on_button_click([&cmd_handler, &tracker](const dpp::button_click_t& event) {
        if(!tracker.permissions_check(event, User::Permission::MANAGEMENT)) {
            return;
        }
        
        std::stringstream ss(event.custom_id);
        std::string command;
        ss >> command;
        std::string arg;
        ss >> arg;        
        
        cmd_handler.btnclick(command, event, arg);
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