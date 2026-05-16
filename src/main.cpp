#include <Arduino.h>
#include "config.h"

// ─── Managers ─────────────────────────────────────────────────────────────────
#include "managers/DisplayManager.h"
#include "managers/WeatherManager.h"
#include "managers/TimeManager.h"
#include "managers/EventManager.h"
#include "managers/ConnectivityManager.h"

// ─── Application Coordinator ──────────────────────────────────────────────────
#include "app/AppCoordinator.h"

// ─── Real implementations ─────────────────────────────────────────────────────
#include "display/Lyligo_4_7_e_paper.h"
#include "providers/Lyligo_4_7_e_paper_TimeProvider.h"
#include "providers/WiFiConnectivityProvider.h"
#include "providers/WeatherApiProvider.h"
#include "credentials.h"

// ─── Providers ───────────────────────────────────────────────────────────────
#include "providers/CalendarEventProvider.h"

// ─── Fake stubs ──────────────────────────────────────────────────────────────
#include "stubs/NullSyncService.h"

// ─── Infrastructure Instances ─────────────────────────────────────────────────
static Lyligo_4_7_e_paper              display;
static Lyligo_4_7_e_paper_TimeProvider timeProvider;
static WiFiConnectivityProvider        connectivity(WIFI_SSID, WIFI_PASSWORD);
static WeatherApiProvider              weatherProvider(&connectivity, WEATHERAPI_KEY, "Chernivtsi");
static CalendarEventProvider           eventProvider(&connectivity, CALENDAR_SERVICE_HOST, CALENDAR_SERVICE_PORT, CALENDAR_SERVICE_PATH);
static NullSyncService                 syncService;

// ─── Manager Instances ────────────────────────────────────────────────────────
static DisplayManager      displayManager(&display);
static WeatherManager      weatherManager(&weatherProvider, WEATHER_UPDATE_INTERVAL_MS);
static EventManager        eventManager(&eventProvider, EVENT_SYNC_INTERVAL_MS);
static ConnectivityManager connectivityManager(&connectivity,
                                               CONNECTIVITY_RETRY_DELAY_MS,
                                               CONNECTIVITY_MAX_RETRIES);

// ─── Application Coordinator ──────────────────────────────────────────────────
static AppCoordinator app(
    &displayManager,
    &weatherManager,
    &eventManager,
    &connectivityManager,
    &syncService
);

// ─── Arduino Entry Points ──────────────────────────────────────────────────────
void setup() {
#ifdef DEBUG_SERIAL
    Serial.begin(SERIAL_BAUD_RATE);
    delay(1000);
#endif

    if (!timeProvider.begin()) {
        LOG("[WARN] RTC chip not found — time unavailable");
    }

    TimeManager::instance().setProvider(&timeProvider);

    app.setup();
}

void loop() {
    app.loop();
}
