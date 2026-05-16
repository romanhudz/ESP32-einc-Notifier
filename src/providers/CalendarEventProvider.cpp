#include "providers/CalendarEventProvider.h"
#include "config.h"
#include <HTTPClient.h>
#include <cJSON.h>

CalendarEventProvider::CalendarEventProvider(IConnectivity* connectivity,
                                             const char* host,
                                             uint16_t port,
                                             const char* path)
    : _connectivity(connectivity), _host(host), _port(port), _path(path) {}

bool CalendarEventProvider::isAvailable() const {
    return _connectivity && _connectivity->isConnected();
}

void CalendarEventProvider::acknowledgeEvent(const String&) {}

bool CalendarEventProvider::fetchEvents(std::vector<EventData>& out) {
    if (!isAvailable()) {
        LOG("[CalendarEvents] Not connected");
        return false;
    }

    char url[128];
    snprintf(url, sizeof(url), "http://%s:%u%s", _host, _port, _path);
    LOG_F("[CalendarEvents] GET %s\n", url);

    HTTPClient http;
    http.begin(url);
    http.setTimeout(5000);
    const int code = http.GET();
    LOG_F("[CalendarEvents] HTTP status: %d\n", code);

    if (code != 200) {
        http.end();
        return false;
    }

    const String body = http.getString();
    http.end();

    cJSON* root = cJSON_Parse(body.c_str());
    if (!root || !cJSON_IsArray(root)) {
        LOG("[CalendarEvents] JSON parse failed or not an array");
        if (root) cJSON_Delete(root);
        return false;
    }

    out.clear();
    int index = 0;
    cJSON* item = nullptr;
    cJSON_ArrayForEach(item, root) {
        cJSON* jDate  = cJSON_GetObjectItem(item, "date");
        cJSON* jTime  = cJSON_GetObjectItem(item, "time");
        cJSON* jTitle = cJSON_GetObjectItem(item, "title");

        if (!cJSON_IsString(jDate) || !cJSON_IsString(jTime) || !cJSON_IsString(jTitle))
            continue;

        uint16_t year;
        uint8_t month, day;
        if (!parseDate(jDate->valuestring, year, month, day))
            continue;

        uint8_t startH, startM, endH, endM;
        if (!parseTime(jTime->valuestring, startH, startM, endH, endM))
            continue;

        uint32_t durationSeconds = ((endH * 60 + endM) - (startH * 60 + startM)) * 60;

        EventData ev;
        ev.id              = "CAL" + String(index++);
        ev.title           = jTitle->valuestring;
        ev.durationSeconds = durationSeconds;
        ev.priority        = EventPriority::Normal;
        ev.type            = EventType::CalendarEvent;

        ev.dateTime.year    = year;
        ev.dateTime.month   = month;
        ev.dateTime.day     = day;
        ev.dateTime.hour    = startH;
        ev.dateTime.minute  = startM;
        ev.dateTime.second  = 0;
        ev.dateTime.weekday = computeWeekday(year, month, day);
        ev.dateTime.isSynced = true;

        out.push_back(ev);
    }

    cJSON_Delete(root);
    LOG_F("[CalendarEvents] Parsed %d events\n", (int)out.size());
    return true;
}

// "dd.MM.yyyy"
bool CalendarEventProvider::parseDate(const char* str,
                                      uint16_t& year, uint8_t& month, uint8_t& day) {
    int d, m, y;
    if (sscanf(str, "%d.%d.%d", &d, &m, &y) != 3) return false;
    year  = (uint16_t)y;
    month = (uint8_t)m;
    day   = (uint8_t)d;
    return true;
}

// "HH:MM - HH:MM"
bool CalendarEventProvider::parseTime(const char* str,
                                      uint8_t& startH, uint8_t& startM,
                                      uint8_t& endH, uint8_t& endM) {
    int sh, sm, eh, em;
    if (sscanf(str, "%d:%d - %d:%d", &sh, &sm, &eh, &em) != 4) return false;
    startH = (uint8_t)sh;
    startM = (uint8_t)sm;
    endH   = (uint8_t)eh;
    endM   = (uint8_t)em;
    return true;
}

// Sakamoto's algorithm — 0=Sun, 1=Mon … 6=Sat → DayOfWeek (Monday=1…Sunday=7)
DayOfWeek CalendarEventProvider::computeWeekday(int y, int m, int d) {
    static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    if (m < 3) y--;
    int dow = (y + y/4 - y/100 + y/400 + t[m-1] + d) % 7;
    return static_cast<DayOfWeek>(dow == 0 ? 7 : dow);
}
