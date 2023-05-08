#include "trackerbot/commands.h"

#include "trackerbot/tracker.h"

#include <functional>
#include <unordered_map>

Commands_Handler::Commands_Handler(dpp::cluster* bot, Tracker* tracker)
    : _bot(bot)
    , _tracker(tracker)
{
    _on_interact_map = {
        { "addtracker", [this](const dpp::interaction_create_t& event) {
                if(!_tracker->permissions_check(event, User::Permission::MANAGEMENT)) {
                    return;
                }

                const std::string user = std::get<std::string>(event.get_parameter("username"));
                _tracker->add_target_menu(event, user);
            }
        },
        { "edit", [this](const dpp::interaction_create_t& event) {
                if(!_tracker->permissions_check(event, User::Permission::MANAGEMENT)) {
                    return;
                }

                const std::string user = std::get<std::string>(event.get_parameter("username"));
                _tracker->edit_target_menu(event, user);
            }
        },
        { "mass_suspend_users", [this](const dpp::interaction_create_t& event) {
                dpp::component mass_remove_input = dpp::component()
                    .set_label("Mass Remove Users")
                    .set_type(dpp::cot_text)
                    .set_min_length(1)
                    .set_max_length(1000)
                    .set_text_style(dpp::text_paragraph)
                    .set_id("mass_remove_users_input");
                dpp::interaction_modal_response mass_removal_modal("mass_removal_modal", "Mass Remove Users (Separate with newline)");
                mass_removal_modal.add_component(mass_remove_input);
                event.dialog(mass_removal_modal);
            }
        },
        { "ping", [this](const dpp::interaction_create_t& event) {
                if(!_tracker->permissions_check(event, User::Permission::BASIC)) {
                    return;
                }

                dpp::message cmd_reply = dpp::message(dpp::ir_channel_message_with_source, "Pong")
                    .set_flags(dpp::m_ephemeral);
                event.reply(cmd_reply);
            }
        },
        { "debug", [this](const dpp::interaction_create_t& event) {
                if(!_tracker->permissions_check(event, User::Permission::FULL)) {
                    return;
                }

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
                const dpp::component register_commands = dpp::component()
                    .set_label("Register Commands")
                    .set_type(dpp::cot_button)
                    .set_style(dpp::cos_secondary)
                    .set_id("register_commands");
                const dpp::component action_row = dpp::component()
                    .set_type(dpp::cot_action_row)
                    .add_component(ping_button)
                    .add_component(list_button)
                    .add_component(force_update_button)
                    .add_component(register_commands);

                const dpp::message res =  dpp::message(event.command.channel_id, "")
                    .add_component(action_row)
                    .set_flags(dpp::m_ephemeral);

                event.reply(res);
            }
        }
    };

    _on_btnclick_map = {
        { "approvetracker", [this](const dpp::interaction_create_t& event, const std::string& name) {
                _tracker->add_target_to_tracker(event, name);
                _bot->message_delete(event.command.msg.id, event.command.msg.channel_id);
            }
        },
        { "rejecttracker", [this](const dpp::interaction_create_t& event, const std::string& /*unused*/) {
                _bot->message_delete(event.command.msg.id, event.command.msg.channel_id);
            }
        },
        { "approvecomment", [this](const dpp::interaction_create_t& event, const std::string& comment_id) {
                dpp::user user = event.command.usr;
                _tracker->approve_post(comment_id, user.username, user.id);
                _bot->message_delete(event.command.msg.id, event.command.msg.channel_id);
            }
        },
        { "rejectcomment", [this](const dpp::interaction_create_t& event, const std::string& comment_id) {
                dpp::user user = event.command.usr;
                _tracker->deny_post(comment_id, user.username, user.id);
                _bot->message_delete(event.command.msg.id, event.command.msg.channel_id);
            }
        },
        { "switchcomment", [this](const dpp::interaction_create_t& event, const std::string& comment_id) {
                _tracker->switch_comment_status(event, comment_id);
            }
        },
        { "switchcomment", [this](const dpp::interaction_create_t& event, const std::string& comment_id) {
                _tracker->switch_comment_status(event, comment_id);
            }
        },
        { "edit_expertise", [this](const dpp::interaction_create_t& event, const std::string& target_name) {
                dpp::component expertise_input = dpp::component()
                    .set_label("Expertise")
                    .set_type(dpp::cot_text)
                    .set_min_length(0)
                    .set_max_length(_tracker->get_discord_config().expertise_max)
                    .set_text_style(dpp::text_short)
                    .set_id("expertise_input " + target_name);
                dpp::interaction_modal_response expertise_modal("expertise_modal "  + target_name, "Edit Expertise");
                expertise_modal.add_component(expertise_input);
                event.dialog(expertise_modal);
            }
        },
        { "close_menu", [this](const dpp::interaction_create_t& event, const std::string& target_name) {
                _tracker->close_target_menu(target_name);
                _bot->message_delete(event.command.msg.id, event.command.msg.channel_id);
            }
        },
        { "suspend_user", [this](const dpp::interaction_create_t& event, const std::string& target_name) {
                _tracker->suspend_target(event, target_name);
            }
        },
        { "ping", [](const dpp::interaction_create_t& event, const std::string& /*unused*/) {
                event.reply("Pong!");
            }
        },
        { "print_targetlist", [this](const dpp::interaction_create_t& event, const std::string& /*unused*/) {
                _tracker->print_target_list(event.command.channel_id);
            }
        },
        { "force_update", [this](const dpp::interaction_create_t& event, const std::string& days) {
                event.reply("Force Updating");
                _tracker->force_update(std::stoi(days));
            }
        },
        { "register_commands", [this](const dpp::interaction_create_t& event, const std::string& /*unused*/) {
                if(!_tracker->permissions_check(event, User::Permission::FULL)) {
                    return;
                }
                
                std::vector<dpp::slashcommand> commands;

                dpp::slashcommand tracker_cmd = dpp::slashcommand("addtracker", "Add Reddit User to the Tracker", _bot->me.id)
                    .add_option(
                        dpp::command_option(dpp::co_string, "username", "Their Reddit Username", true)
                    );
                dpp::slashcommand edit_cmd = dpp::slashcommand("edit", "Edit Target Parameters", _bot->me.id)
                    .add_option(
                        dpp::command_option(dpp::co_string, "username", "Their Reddit Username", true)
                    );
                dpp::slashcommand mass_removal_prompt_cmd = dpp::slashcommand("mass_suspend_users", "Mass Remove Users from the Tracker", _bot->me.id);
                dpp::slashcommand ping_cmd = dpp::slashcommand("ping", "Check if Bot is still Alive", _bot->me.id);

                commands.emplace_back(tracker_cmd);
                commands.emplace_back(edit_cmd);
                commands.emplace_back(mass_removal_prompt_cmd);
                commands.emplace_back(ping_cmd);

                _bot->guild_bulk_command_create(commands, event.command.guild_id);
                
                event.reply("Registered Commands");
            }
        }
    };
}
Commands_Handler::~Commands_Handler() {
    _bot = nullptr;
    _tracker = nullptr;
}

void Commands_Handler::interact(const std::string& cmd, const dpp::interaction_create_t& event) {
    _on_interact_map.at(cmd)(event);
}
void Commands_Handler::btnclick(const std::string& cmd, const dpp::interaction_create_t& event, const std::string& arg) {
    _on_btnclick_map.at(cmd)(event, arg);
}
