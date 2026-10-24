#include "calendar.h"
#include <libical/ical.h>

#include <algorithm>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>
#include <vector>

void calendar_zoneinfo(const std::string &directory) {
    set_zone_directory(directory.c_str());
    icaltimezone_set_builtin_tzdata(1);
}

static icaltimezone *zone_for(const std::string &name) {
    icaltimezone *zone = icaltimezone_get_builtin_timezone(name.c_str());
    if (!zone) throw std::runtime_error("Unknown calendar timezone");
    return zone;
}

std::tm calendar_localtime(std::time_t now, const std::string &timezone) {
    icaltimetype t = icaltime_from_timet_with_zone(now, 0, zone_for(timezone));
    std::tm result = {};
    result.tm_year = t.year - 1900;
    result.tm_mon = t.month - 1;
    result.tm_mday = t.day;
    result.tm_hour = t.hour;
    result.tm_min = t.minute;
    result.tm_sec = t.second;
    result.tm_wday = icaltime_day_of_week(t) - 1;
    result.tm_yday = icaltime_day_of_year(t) - 1;
    return result;
}

static std::string escaped(const char *text) {
    std::string result;
    for (const char *p = text ? text : "Untitled event"; *p; ++p) {
        switch (*p) {
            case '&': result += "&amp;"; break;
            case '<': result += "&lt;"; break;
            case '>': result += "&gt;"; break;
            case '\n': case '\r': case '\t': result += ' '; break;
            default: result += *p;
        }
    }
    return result;
}

struct Event {
    time_t start, end;
    bool all_day;
    std::string title;
};

// RECURRENCE-ID;RANGE=THISANDFUTURE: every occurrence from `from` on is
// shifted by `shift` and takes the override's length, title and status.
struct RangeOverride {
    time_t from, shift, length;
    std::string title;
    bool cancelled;
};

struct Agenda {
    icaltimezone *zone;
    time_t now, day_end;
    std::set<std::pair<std::string, time_t>> overrides;
    std::map<std::string, std::vector<RangeOverride>> ranges;
    std::vector<Event> events;
};

static time_t date_in_zone(time_t value, icaltimezone *zone) {
    icaltimetype t = icaltime_from_timet_with_zone(value, 0, icaltimezone_get_utc_timezone());
    return icaltime_as_timet_with_zone(t, zone);
}

static void collect_event(icalcomponent *component, icaltime_span *span, void *data) {
    Agenda &agenda = *static_cast<Agenda *>(data);
    bool all_day = icalcomponent_get_dtstart(component).is_date;
    time_t start = all_day ? date_in_zone(span->start, agenda.zone) : span->start;
    time_t end = all_day ? date_in_zone(span->end, agenda.zone) : span->end;
    const char *uid = icalcomponent_get_uid(component);
    std::string title = escaped(icalcomponent_get_summary(component));
    if (!icalcomponent_get_first_property(component, ICAL_RECURRENCEID_PROPERTY)) {
        if (agenda.overrides.count({uid, start})) return;
        // Latest range override at or before this occurrence wins.
        const RangeOverride *range = nullptr;
        auto found = agenda.ranges.find(uid);
        if (found != agenda.ranges.end())
            for (const auto &candidate : found->second)
                if (candidate.from <= start && (!range || candidate.from > range->from))
                    range = &candidate;
        if (range) {
            if (range->cancelled) return;
            start += range->shift;
            end = start + range->length;
            title = range->title;
        }
    }
    if (end <= agenda.now || start >= agenda.day_end) return;
    agenda.events.push_back({start, end, all_day, title});
}

std::string calendar_agenda(const std::string &ics, time_t now, const std::string &timezone) {
    if (ics.size() > 5 * 1024 * 1024 || ics.find("BEGIN:VCALENDAR") != 0 ||
        ics.find("END:VCALENDAR") == std::string::npos)
        throw std::runtime_error("Invalid calendar download");
    std::unique_ptr<icalcomponent, decltype(&icalcomponent_free)> calendar(
        icalparser_parse_string(ics.c_str()), icalcomponent_free);
    if (!calendar || icalcomponent_isa(calendar.get()) != ICAL_VCALENDAR_COMPONENT ||
        icalcomponent_count_errors(calendar.get()))
        throw std::runtime_error("Invalid calendar data");

    Agenda agenda = {};
    agenda.zone = zone_for(timezone);
    agenda.now = now;
    icaltimetype day = icaltime_from_timet_with_zone(now, 0, agenda.zone);
    day.hour = day.minute = day.second = 0;
    // Local midnight must be converted in the display zone, not as UTC,
    // or events just after midnight leak into "today" (and early ones drop out).
    time_t day_start = icaltime_as_timet_with_zone(day, agenda.zone);
    icaltime_adjust(&day, 1, 0, 0, 0);
    agenda.day_end = icaltime_as_timet_with_zone(day, agenda.zone);

    std::vector<icalcomponent *> components;
    for (icalcomponent *event = icalcomponent_get_first_component(calendar.get(), ICAL_VEVENT_COMPONENT);
         event; event = icalcomponent_get_next_component(calendar.get(), ICAL_VEVENT_COMPONENT)) {
        if (!icalcomponent_get_uid(event) || icaltime_is_null_time(icalcomponent_get_dtstart(event)))
            throw std::runtime_error("Calendar event is missing its date or ID");
        // Floating times belong to the display timezone; named zones must resolve.
        for (icalproperty *p = icalcomponent_get_first_property(event, ICAL_ANY_PROPERTY);
             p; p = icalcomponent_get_next_property(event, ICAL_ANY_PROPERTY)) {
            icalvalue_kind kind = icalvalue_isa(icalproperty_get_value(p));
            if (kind != ICAL_DATETIME_VALUE) continue;
            icaltimetype t = icalproperty_get_datetime_with_component(p, event);
            if (!t.zone && !t.is_date) {
                if (icalproperty_get_first_parameter(p, ICAL_TZID_PARAMETER))
                    throw std::runtime_error("Calendar event has an unknown timezone");
                icalproperty_add_parameter(p, icalparameter_new_tzid(timezone.c_str()));
            }
        }
        icalproperty *rid = icalcomponent_get_first_property(event, ICAL_RECURRENCEID_PROPERTY);
        if (rid) {
            icaltimetype t = icalproperty_get_datetime_with_component(rid, event);
            time_t from = icaltime_as_timet_with_zone(t, t.zone ? t.zone : agenda.zone);
            if (icalproperty_get_first_parameter(rid, ICAL_RANGE_PARAMETER)) {
                // The override is applied to the master's occurrences instead.
                icaltimetype s = icalcomponent_get_dtstart(event);
                icaltimetype e = icalcomponent_get_dtend(event);
                time_t start = icaltime_as_timet_with_zone(s, s.zone ? s.zone : agenda.zone);
                time_t end = icaltime_is_null_time(e) ? start
                    : icaltime_as_timet_with_zone(e, e.zone ? e.zone : agenda.zone);
                agenda.ranges[icalcomponent_get_uid(event)].push_back(
                    {from, start - from, end - start, escaped(icalcomponent_get_summary(event)),
                     icalcomponent_get_status(event) == ICAL_STATUS_CANCELLED});
                continue;
            }
            agenda.overrides.insert({icalcomponent_get_uid(event), from});
        }
        components.push_back(event);
    }
    for (auto *event : components) {
        if (icalcomponent_get_status(event) == ICAL_STATUS_CANCELLED) continue;
        // Date-only spans use UTC inside libical; widen the query before local filtering.
        icalcomponent_foreach_recurrence(event,
            icaltime_from_timet_with_zone(day_start - 86400, 0, icaltimezone_get_utc_timezone()),
            icaltime_from_timet_with_zone(agenda.day_end + 86400, 0, icaltimezone_get_utc_timezone()),
            collect_event, &agenda);
    }
    std::sort(agenda.events.begin(), agenda.events.end(), [](const Event &a, const Event &b) {
        return a.start < b.start || (a.start == b.start && a.title < b.title);
    });
    std::ostringstream out;
    out << "#markup\n";
    size_t timed = 0, all_day = 0;
    for (const auto &event : agenda.events) {
        if (event.all_day || ++timed > 3) continue;
        std::tm start = calendar_localtime(event.start, timezone);
        std::tm end = calendar_localtime(event.end, timezone);
        char start_text[16], end_text[16];
        strftime(start_text, sizeof start_text, "%H:%M", &start);
        strftime(end_text, sizeof end_text, "%H:%M", &end);
        if (timed == 1) {
            if (event.start <= now) out << "<small>NOW · until " << end_text << "</small>\n";
            else out << "<small>NEXT · " << start_text << " · in "
                     << (event.start - now + 59) / 60 << " min</small>\n";
            out << "<big><b>" << event.title << "</b></big>\n";
        } else out << "\n<b>" << start_text << "</b>  " << event.title << "\n";
    }
    if (!timed) out << "No more appointments today\n";
    if (timed > 3) out << "<small>+" << timed - 3 << " more today</small>\n";
    for (const auto &event : agenda.events) {
        if (!event.all_day || ++all_day > 2) continue;
        out << "\n<small>ALL DAY</small>  " << event.title << "\n";
    }
    if (all_day > 2) out << "<small>+" << all_day - 2 << " more all-day events</small>\n";
    return out.str();
}
