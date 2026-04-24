#include "providers/WeatherApiProvider.h"
#include "config.h"
#include <HTTPClient.h>
#include <cJSON.h>
#include <Arduino.h>

// Maps full English country name (as returned by WeatherAPI) to ISO 3166-1 alpha-2 code.
static const char* countryNameToCode(const char* name) {
    static const struct { const char* name; const char* code; } kTable[] = {
        {"Afghanistan","AF"},{"Albania","AL"},{"Algeria","DZ"},{"Angola","AO"},
        {"Argentina","AR"},{"Armenia","AM"},{"Australia","AU"},{"Austria","AT"},
        {"Azerbaijan","AZ"},{"Bangladesh","BD"},{"Belarus","BY"},{"Belgium","BE"},
        {"Bolivia","BO"},{"Bosnia and Herzegovina","BA"},{"Brazil","BR"},
        {"Bulgaria","BG"},{"Cambodia","KH"},{"Cameroon","CM"},{"Canada","CA"},
        {"Chile","CL"},{"China","CN"},{"Colombia","CO"},{"Croatia","HR"},
        {"Cuba","CU"},{"Czech Republic","CZ"},{"Czechia","CZ"},{"Denmark","DK"},
        {"Ecuador","EC"},{"Egypt","EG"},{"Estonia","EE"},{"Ethiopia","ET"},
        {"Finland","FI"},{"France","FR"},{"Georgia","GE"},{"Germany","DE"},
        {"Ghana","GH"},{"Greece","GR"},{"Guatemala","GT"},{"Honduras","HN"},
        {"Hong Kong","HK"},{"Hungary","HU"},{"Iceland","IS"},{"India","IN"},
        {"Indonesia","ID"},{"Iran","IR"},{"Iraq","IQ"},{"Ireland","IE"},
        {"Israel","IL"},{"Italy","IT"},{"Jamaica","JM"},{"Japan","JP"},
        {"Jordan","JO"},{"Kazakhstan","KZ"},{"Kenya","KE"},{"Kosovo","XK"},
        {"Kuwait","KW"},{"Kyrgyzstan","KG"},{"Latvia","LV"},{"Lebanon","LB"},
        {"Libya","LY"},{"Lithuania","LT"},{"Luxembourg","LU"},{"Malaysia","MY"},
        {"Mexico","MX"},{"Moldova","MD"},{"Mongolia","MN"},{"Montenegro","ME"},
        {"Morocco","MA"},{"Myanmar","MM"},{"Nepal","NP"},{"Netherlands","NL"},
        {"New Zealand","NZ"},{"Nicaragua","NI"},{"Nigeria","NG"},{"Norway","NO"},
        {"Oman","OM"},{"Pakistan","PK"},{"Palestine","PS"},{"Panama","PA"},
        {"Paraguay","PY"},{"Peru","PE"},{"Philippines","PH"},{"Poland","PL"},
        {"Portugal","PT"},{"Qatar","QA"},{"Romania","RO"},{"Russia","RU"},
        {"Saudi Arabia","SA"},{"Senegal","SN"},{"Serbia","RS"},{"Singapore","SG"},
        {"Slovakia","SK"},{"Slovenia","SI"},{"Somalia","SO"},{"South Africa","ZA"},
        {"South Korea","KR"},{"Spain","ES"},{"Sri Lanka","LK"},{"Sudan","SD"},
        {"Sweden","SE"},{"Switzerland","CH"},{"Syria","SY"},{"Taiwan","TW"},
        {"Tajikistan","TJ"},{"Tanzania","TZ"},{"Thailand","TH"},{"Tunisia","TN"},
        {"Turkey","TR"},{"Turkmenistan","TM"},{"Uganda","UG"},{"Ukraine","UA"},
        {"United Arab Emirates","AE"},{"United Kingdom","GB"},
        {"United States","US"},{"United States of America","US"},
        {"Uruguay","UY"},{"Uzbekistan","UZ"},{"Venezuela","VE"},{"Vietnam","VN"},
        {"Yemen","YE"},{"Zambia","ZM"},{"Zimbabwe","ZW"},
    };
    for (const auto& e : kTable) {
        if (strcasecmp(name, e.name) == 0) return e.code;
    }
    return name; // fallback: return as-is if not found
}

// WeatherAPI free endpoint:
//   GET http://api.weatherapi.com/v1/current.json?key={key}&q={city}&aqi=no
// Response fields used:
//   location.name + location.country  → location
//   current.temp_c                    → temperatureCelsius
//   current.feelslike_c               → feelsLikeCelsius
//   current.humidity                  → humidityPercent
//   current.wind_kph                  → windSpeedKmh
//   current.wind_degree               → windDirection
//   current.condition.code            → WeatherCondition

static constexpr const char* kBaseUrl =
    "http://api.weatherapi.com/v1/current.json";

WeatherApiProvider::WeatherApiProvider(IConnectivity* connectivity,
                                       const char*    apiKey,
                                       const String&  location)
    : _connectivity(connectivity), _apiKey(apiKey), _location(location) {}

bool WeatherApiProvider::isAvailable() const {
    return _connectivity && _connectivity->isConnected() && _location.length() > 0;
}

void WeatherApiProvider::setLocation(const String& location) {
    _location = location;
}

bool WeatherApiProvider::fetch(WeatherData& out) {
    if (!_connectivity) {
        LOG("[WeatherAPI] No connectivity object");
        return false;
    }
    if (!_connectivity->isConnected()) {
        LOG("[WeatherAPI] Not connected to network");
        return false;
    }
    if (_location.length() == 0) {
        LOG("[WeatherAPI] Location not set");
        return false;
    }

    char url[256];
    snprintf(url, sizeof(url),
             "%s?key=%s&q=%s&aqi=no",
             kBaseUrl, _apiKey, _location.c_str());
    LOG_F("[WeatherAPI] GET %s\n", url);

    HTTPClient http;
    http.begin(url);
    http.setTimeout(8000);
    const int code = http.GET();
    LOG_F("[WeatherAPI] HTTP status: %d\n", code);

    if (code != 200) {
        if (code > 0)
            LOG_F("[WeatherAPI] Error body: %s\n", http.getString().c_str());
        http.end();
        return false;
    }

    const String body = http.getString();
    http.end();
    LOG_F("[WeatherAPI] Response (%u bytes): %.120s\n",
          body.length(), body.c_str());

    cJSON* root = cJSON_Parse(body.c_str());
    if (!root) {
        LOG_F("[WeatherAPI] JSON parse failed. Raw: %.80s\n", body.c_str());
        return false;
    }

    bool ok = false;

    // location
    cJSON* loc = cJSON_GetObjectItem(root, "location");
    if (cJSON_IsObject(loc)) {
        cJSON* name    = cJSON_GetObjectItem(loc, "name");
        cJSON* country = cJSON_GetObjectItem(loc, "country");
        if (cJSON_IsString(name)) {
            out.location = name->valuestring;
            if (cJSON_IsString(country)) {
                out.location += ", ";
                out.location += countryNameToCode(country->valuestring);
            }
        }
        LOG_F("[WeatherAPI] Location: %s\n", out.location.c_str());
    } else {
        LOG("[WeatherAPI] 'location' key missing in response");
    }

    // current
    cJSON* cur = cJSON_GetObjectItem(root, "current");
    if (cJSON_IsObject(cur)) {
        cJSON* temp    = cJSON_GetObjectItem(cur, "temp_c");
        cJSON* feels   = cJSON_GetObjectItem(cur, "feelslike_c");
        cJSON* hum     = cJSON_GetObjectItem(cur, "humidity");
        cJSON* wkph    = cJSON_GetObjectItem(cur, "wind_kph");
        cJSON* wdeg    = cJSON_GetObjectItem(cur, "wind_degree");
        cJSON* condObj = cJSON_GetObjectItem(cur, "condition");

        if (cJSON_IsNumber(temp))  { out.temperatureCelsius = static_cast<float>(temp->valuedouble);  LOG_F("[WeatherAPI] temp_c=%.1f\n", out.temperatureCelsius); }
        else                         LOG("[WeatherAPI] 'temp_c' missing");
        if (cJSON_IsNumber(feels)) { out.feelsLikeCelsius   = static_cast<float>(feels->valuedouble); LOG_F("[WeatherAPI] feelslike_c=%.1f\n", out.feelsLikeCelsius); }
        if (cJSON_IsNumber(hum))   { out.humidityPercent    = static_cast<float>(hum->valuedouble);   LOG_F("[WeatherAPI] humidity=%.0f\n", out.humidityPercent); }
        if (cJSON_IsNumber(wkph))  { out.windSpeedKmh       = static_cast<float>(wkph->valuedouble);  LOG_F("[WeatherAPI] wind_kph=%.1f\n", out.windSpeedKmh); }
        if (cJSON_IsNumber(wdeg))    out.windDirection      = mapWindDegrees(static_cast<float>(wdeg->valuedouble));

        if (cJSON_IsObject(condObj)) {
            cJSON* condCode = cJSON_GetObjectItem(condObj, "code");
            if (cJSON_IsNumber(condCode)) {
                const int c = static_cast<int>(condCode->valuedouble);
                out.condition = mapConditionCode(c);
                LOG_F("[WeatherAPI] condition code=%d\n", c);
            } else {
                LOG("[WeatherAPI] 'condition.code' missing");
            }
        } else {
            LOG("[WeatherAPI] 'condition' object missing");
        }

        ok = true;
    } else {
        LOG("[WeatherAPI] 'current' key missing in response");
    }

    cJSON_Delete(root);

    if (ok) {
        out.isValid     = true;
        out.fetchedAtMs = millis();
        LOG_F("[WeatherAPI] OK — %.1fC feels %.1fC, hum %.0f%%, wind %.1fkm/h, loc=%s\n",
              out.temperatureCelsius, out.feelsLikeCelsius,
              out.humidityPercent, out.windSpeedKmh, out.location.c_str());
    } else {
        LOG("[WeatherAPI] fetch failed — incomplete response");
    }
    return ok;
}

// ─── WeatherAPI condition codes → WeatherCondition ───────────────────────────
// https://www.weatherapi.com/docs/weather_conditions.json
WeatherCondition WeatherApiProvider::mapConditionCode(int code) {
    switch (code) {
        case 1000: return WeatherCondition::Sunny;
        case 1003: return WeatherCondition::PartlyCloudy;
        case 1006: return WeatherCondition::Cloudy;
        case 1009: return WeatherCondition::Overcast;
        case 1030: return WeatherCondition::Mist;
        case 1063: return WeatherCondition::PatchyRainPossible;
        case 1066: return WeatherCondition::PatchySnowPossible;
        case 1069: return WeatherCondition::PatchySleetPossible;
        case 1072: return WeatherCondition::PatchyFreezingDrizzle;
        case 1087: return WeatherCondition::ThunderyOutbreaks;
        case 1114: return WeatherCondition::BlowingSnow;
        case 1117: return WeatherCondition::Blizzard;
        case 1135: return WeatherCondition::Fog;
        case 1147: return WeatherCondition::FreezingFog;
        case 1150:
        case 1153: return WeatherCondition::LightDrizzle;
        case 1168: return WeatherCondition::FreezingDrizzle;
        case 1171: return WeatherCondition::HeavyFreezingDrizzle;
        case 1180:
        case 1183: return WeatherCondition::LightRain;
        case 1186:
        case 1189: return WeatherCondition::ModerateRain;
        case 1192:
        case 1195: return WeatherCondition::HeavyRain;
        case 1198: return WeatherCondition::LightFreezingRain;
        case 1201: return WeatherCondition::HeavyFreezingRain;
        case 1204: return WeatherCondition::LightSleet;
        case 1207: return WeatherCondition::HeavySleet;
        case 1210:
        case 1213: return WeatherCondition::LightSnow;
        case 1216:
        case 1219: return WeatherCondition::ModerateSnow;
        case 1222:
        case 1225: return WeatherCondition::HeavySnow;
        case 1237: return WeatherCondition::IcePellets;
        case 1240: return WeatherCondition::LightRainShower;
        case 1243:
        case 1246: return WeatherCondition::HeavyRainShower;
        case 1249: return WeatherCondition::LightSleetShowers;
        case 1252: return WeatherCondition::HeavySleetShowers;
        case 1255: return WeatherCondition::LightSnowShowers;
        case 1258: return WeatherCondition::HeavySnowShowers;
        case 1261: return WeatherCondition::LightIcePellets;
        case 1264: return WeatherCondition::HeavyIcePellets;
        case 1273: return WeatherCondition::LightRainThunder;
        case 1276: return WeatherCondition::HeavyRainThunder;
        case 1279: return WeatherCondition::LightSnowThunder;
        case 1282: return WeatherCondition::HeavySnowThunder;
        default:   return WeatherCondition::Unknown;
    }
}

// ─── Wind degrees → WindDirection ─────────────────────────────────────────────
WindDirection WeatherApiProvider::mapWindDegrees(float deg) {
    const int sector = static_cast<int>((deg + 22.5f) / 45.0f) % 8;
    switch (sector) {
        case 0: return WindDirection::N;
        case 1: return WindDirection::NE;
        case 2: return WindDirection::E;
        case 3: return WindDirection::SE;
        case 4: return WindDirection::S;
        case 5: return WindDirection::SW;
        case 6: return WindDirection::W;
        case 7: return WindDirection::NW;
        default: return WindDirection::Unknown;
    }
}
