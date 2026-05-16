#pragma once
#include "interfaces/IEventProvider.h"
#include "interfaces/IConnectivity.h"

class CalendarEventProvider : public IEventProvider {
public:
    CalendarEventProvider(IConnectivity* connectivity,
                          const char* host,
                          uint16_t port,
                          const char* path = "/events");

    bool fetchEvents(std::vector<EventData>& out) override;
    bool isAvailable() const                      override;
    void acknowledgeEvent(const String& eventId)  override;

private:
    IConnectivity* _connectivity;
    const char*    _host;
    uint16_t       _port;
    const char*    _path;

    static bool parseDate(const char* str, uint16_t& year, uint8_t& month, uint8_t& day);
    static bool parseTime(const char* str, uint8_t& startH, uint8_t& startM,
                          uint8_t& endH, uint8_t& endM);
    static DayOfWeek computeWeekday(int year, int month, int day);
};
