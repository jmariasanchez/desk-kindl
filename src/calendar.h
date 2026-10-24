#pragma once

#include <ctime>
#include <string>

void calendar_zoneinfo(const std::string &directory);
std::tm calendar_localtime(std::time_t now, const std::string &timezone);
std::string calendar_agenda(const std::string &ics, std::time_t now,
                            const std::string &timezone);
