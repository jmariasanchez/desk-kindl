#include "../src/calendar.h"
#include <libical/ical.h>
#include <cassert>
#include <iostream>
#include <stdexcept>

static time_t at(const char *value) {
    return icaltime_as_timet(icaltime_from_string(value));
}

static std::string agenda(const std::string &events, const char *now = "20261024T093000Z") {
    return calendar_agenda("BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:-//Test//EN\r\n" +
        events + "END:VCALENDAR\r\n", at(now), "Europe/Dublin");
}

static bool contains(const std::string &text, const std::string &part) {
    return text.find(part) != std::string::npos;
}

int main(int argc, char **argv) {
    assert(argc == 2);
    calendar_zoneinfo(argv[1]);
    const std::string daily =
        "BEGIN:VEVENT\r\nUID:daily\r\nDTSTART;TZID=Europe/Dublin:20261001T110000\r\n"
        "DTEND;TZID=Europe/Dublin:20261001T120000\r\nRRULE:FREQ=DAILY\r\n"
        "SUMMARY:Standup & planning\r\nEND:VEVENT\r\n";
    auto result = agenda(daily);
    assert(contains(result, "NEXT · 11:00 · in 30 min"));
    assert(contains(result, "Standup &amp; planning"));
    result = agenda(daily, "20261025T103000Z");
    assert(contains(result, "NEXT · 11:00 · in 30 min"));
    result = agenda(daily, "20261024T103000Z");
    assert(contains(result, "NOW · until 12:00"));
    assert(!contains(agenda(daily, "20261024T110000Z"), "Standup"));

    const std::string moved =
        "BEGIN:VEVENT\r\nUID:daily\r\nRECURRENCE-ID;TZID=Europe/Dublin:20261024T110000\r\n"
        "DTSTART;TZID=Europe/Dublin:20261024T140000\r\nDTEND;TZID=Europe/Dublin:20261024T150000\r\n"
        "SUMMARY:Moved meeting\r\nEND:VEVENT\r\n";
    result = agenda(daily + moved);
    assert(!contains(result, "Standup"));
    assert(contains(result, "NEXT · 14:00 · in 210 min"));
    const std::string cancelled =
        "BEGIN:VEVENT\r\nUID:daily\r\nRECURRENCE-ID;TZID=Europe/Dublin:20261024T110000\r\n"
        "DTSTART;TZID=Europe/Dublin:20261024T110000\r\nSTATUS:CANCELLED\r\nEND:VEVENT\r\n";
    assert(!contains(agenda(daily + cancelled), "Standup"));

    const std::string excluded =
        "BEGIN:VEVENT\r\nUID:excluded\r\nDTSTART;TZID=Europe/Dublin:20261001T110000\r\n"
        "DTEND;TZID=Europe/Dublin:20261001T120000\r\nRRULE:FREQ=DAILY\r\n"
        "EXDATE;TZID=Europe/Dublin:20261024T110000\r\nSUMMARY:Excluded\r\nEND:VEVENT\r\n";
    assert(!contains(agenda(excluded), "Excluded"));
    const std::string holiday =
        "BEGIN:VEVENT\r\nUID:holiday\r\nDTSTART;VALUE=DATE:20261023\r\n"
        "DTEND;VALUE=DATE:20261025\r\nSUMMARY:Long\r\n  weekend\r\nEND:VEVENT\r\n";
    assert(contains(agenda(holiday), "ALL DAY</small>  Long weekend"));
    assert(!contains(agenda(holiday, "20261025T003000Z"), "Long weekend"));
    const std::string utc =
        "BEGIN:VEVENT\r\nUID:utc\r\nDTSTART:20261024T100000Z\r\n"
        "DTEND:20261024T110000Z\r\nSUMMARY:<Interview>\r\nEND:VEVENT\r\n";
    assert(contains(agenda(utc), "NEXT · 11:00 · in 30 min"));
    assert(contains(agenda(utc), "&lt;Interview&gt;"));
    const std::string floating =
        "BEGIN:VEVENT\r\nUID:float\r\nDTSTART:20261024T110000\r\n"
        "DTEND:20261024T120000\r\nSUMMARY:Floating\r\nEND:VEVENT\r\n";
    assert(contains(agenda(floating), "NEXT · 11:00 · in 30 min"));
    assert(!contains(agenda(utc, "20261023T100000Z"), "Interview"));
    assert(contains(agenda(""), "No more appointments today"));
    // Day ends at local midnight (Dublin is UTC+1 here), not UTC midnight.
    const std::string after_midnight =
        "BEGIN:VEVENT\r\nUID:late\r\nDTSTART;TZID=Europe/Dublin:20261025T003000\r\n"
        "DTEND;TZID=Europe/Dublin:20261025T010000\r\nSUMMARY:Tomorrow early\r\nEND:VEVENT\r\n";
    assert(!contains(agenda(after_midnight, "20261024T213000Z"), "Tomorrow early"));
    // "This and following": from 20 Oct the standup moved to 15:00 and was renamed.
    const std::string range = daily +
        "BEGIN:VEVENT\r\nUID:daily\r\nRECURRENCE-ID;RANGE=THISANDFUTURE;TZID=Europe/Dublin:20261020T110000\r\n"
        "DTSTART;TZID=Europe/Dublin:20261020T150000\r\nDTEND;TZID=Europe/Dublin:20261020T153000\r\n"
        "SUMMARY:Afternoon sync\r\nEND:VEVENT\r\n";
    result = agenda(range);
    assert(!contains(result, "Standup"));
    assert(contains(result, "NEXT · 15:00 · in 270 min"));
    assert(contains(result, "Afternoon sync"));
    assert(contains(agenda(range, "20261019T093000Z"), "Standup"));
    const std::string range_cancelled = daily +
        "BEGIN:VEVENT\r\nUID:daily\r\nRECURRENCE-ID;RANGE=THISANDFUTURE;TZID=Europe/Dublin:20261020T110000\r\n"
        "DTSTART;TZID=Europe/Dublin:20261020T110000\r\nSTATUS:CANCELLED\r\nEND:VEVENT\r\n";
    assert(contains(agenda(range_cancelled), "No more appointments today"));

    bool rejected = false;
    try { calendar_agenda("<html>Sign in</html>", at("20261024T093000Z"), "Europe/Dublin"); }
    catch (const std::runtime_error &) { rejected = true; }
    assert(rejected);
    rejected = false;
    try { calendar_localtime(at("20261024T093000Z"), "Invalid/Zone"); }
    catch (const std::runtime_error &) { rejected = true; }
    assert(rejected);
    auto before = calendar_localtime(at("20261025T003000Z"), "Europe/Dublin");
    auto after = calendar_localtime(at("20261025T013000Z"), "Europe/Dublin");
    assert(before.tm_hour == 1 && after.tm_hour == 1);
    std::cout << "Calendar checks passed (recurrence, overrides, DST, all-day, escaping, validation)\n";
}
