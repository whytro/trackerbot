#ifndef TRACKERBOT_COMMANDS_H
#define TRACKERBOT_COMMANDS_H

#include <dpp/dpp.h>
#include <functional>
#include <initializer_list>
#include <unordered_map>

#include "trackerbot/tracker.h"

class Commands_Handler {
public:
    Commands_Handler(dpp::cluster* bot, Tracker* tracker);
    ~Commands_Handler();

    void interact(const std::string& cmd, const dpp::interaction_create_t& event);
    void btnclick(const std::string& cmd, const dpp::interaction_create_t& event, const std::string& arg);

private:
    dpp::cluster* _bot;
    Tracker* _tracker;

    std::unordered_map<std::string, std::function<void(const dpp::interaction_create_t&)>> _on_interact_map;
    std::unordered_map<std::string, std::function<void(const dpp::interaction_create_t&, const std::string& arg)>> _on_btnclick_map;
};

#endif // TRACKERBOT_COMMANDS_H