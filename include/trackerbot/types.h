#ifndef TRACKERBOT_TYPES_H
#define TRACKERBOT_TYPES_H

#include <memory>
#include <string>

class Target {
public:
    enum Status { UNKNOWN = -1, SUSPENDED = 0, ACTIVE = 1, AUTOMATIC = 2, PAUSED = 3 };

    struct Data {
        std::string username;
        std::string expertise;
        Status status = Status::UNKNOWN;
        int64_t managing_msg_id = 0;
        int64_t msg_channel = 0;
    };

    std::shared_ptr<Target::Data> data;
    bool is_empty() {
        return data.use_count() == 0 && data == nullptr;
    }
};

struct User {
    enum Permission { BASIC = 0, MANAGEMENT = 1, FULL = 2 };

    int64_t snowflake = 0;
    Permission permission_level = Permission::BASIC;
    std::string username;
};

#endif // TRACKERBOT_TYPES_H