#include "FormatUtils.h"

std::string FormatDuration(std::chrono::seconds duration) {
    using namespace std::chrono;
    auto hrs = duration_cast<hours>(duration);
    auto mins = duration_cast<minutes>(duration % hours(1));

    std::string timeStr;

    if (hrs.count() > 0) {
        timeStr += std::to_string(hrs.count()) + "h ";
    }
    if (mins.count() > 0 || hrs.count() > 0) {
        timeStr += std::to_string(mins.count()) + "m ";
    }
    if (hrs.count() == 0 && mins.count() == 0) {
        timeStr = "0m";
    }

    if (!timeStr.empty() && timeStr.back() == ' ') {
        timeStr.pop_back();
    }

    return timeStr;
}