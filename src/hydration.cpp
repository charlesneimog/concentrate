#include "hydration.hpp"

HydrationService::HydrationService() {
    m_Location = GetLocation();
    GetHydrationRecommendation();
}

// ─────────────────────────────────────
HydrationService::Location HydrationService::GetLocation() {
    httplib::Client client("http://ip-api.com");
    HydrationService::Location loc;

    client.set_connection_timeout(3, 0);
    client.set_read_timeout(10, 0);

    auto res = client.Get("/json");
    if (!res) {
        spdlog::error("Failed to get location info");
        loc.city = "";
        loc.region = "";
        loc.country = "";
        loc.latitude = 0;
        loc.longitude = 0;
        return loc;
    }

    if (res->status != 200) {
        spdlog::error("Location API returned HTTP {}", res->status);
        throw std::runtime_error("Location API error");
    }

    auto j = nlohmann::json::parse(res->body);
    loc.city = j.value("city", "");
    loc.region = j.value("regionName", "");
    loc.country = j.value("country", "");
    loc.latitude = j.value("lat", 0.0);
    loc.longitude = j.value("lon", 0.0);

    spdlog::info("Location detected: {}, {}, {} (lat={}, lon={})", loc.city, loc.region,
                 loc.country, loc.latitude, loc.longitude);
    return loc;
}

// ─────────────────────────────────────
void HydrationService::GetHydrationRecommendation(double weightKg) {
    HydrationInfo info;

    // HTTPS client
    httplib::SSLClient client("api.open-meteo.com", 443);
    client.enable_server_certificate_verification(true);

    client.set_connection_timeout(3, 0);
    client.set_read_timeout(10, 0);

    std::string url = "/v1/forecast?latitude=" + std::to_string(m_Location.latitude) +
                      "&longitude=" + std::to_string(m_Location.longitude) +
                      "&current_weather=true&hourly=relative_humidity_2m&timezone=auto";

    auto res = client.Get(url.c_str());
    if (!res) {
        spdlog::error("Failed to get weather info, error code: {}", static_cast<int>(res.error()));
        info.temperatureC = 25.0;
        info.humidity = 50.0;
        info.recommendedLiters = 2.0;
        m_Liters = info.recommendedLiters;
        return;
    }

    if (res->status != 200) {
        spdlog::error("Weather API returned status {}", res->status);
        info.temperatureC = 25.0;
        info.humidity = 50.0;
        info.recommendedLiters = 2.0;
        m_Liters = info.recommendedLiters;
        return;
    }

    auto j = nlohmann::json::parse(res->body);
    info.temperatureC = j["current_weather"].value("temperature", 25.0);

    // Find nearest humidity value
    std::string currentTime = j["current_weather"].value("time", "");
    info.humidity = 50.0; // default
    auto& times = j["hourly"]["time"];
    auto& humidities = j["hourly"]["relative_humidity_2m"];
    for (size_t i = 0; i < times.size(); ++i) {
        if (times[i] == currentTime) {
            info.humidity = humidities[i].get<double>();
            break;
        }
    }

    // Hydration calculation
    double base = weightKg * 0.035; // 35 mL per kg/day
    if (info.temperatureC > 20.0) {
        base += (info.temperatureC - 20.0) * 0.05;
    }
    if (info.humidity < 20.0) {
        base += 0.2;
    }

    info.recommendedLiters = std::clamp(base, 1.5, 5.0);

    // Notes
    if (info.temperatureC > 30.0) info.notes += "Hot room; ";
    if (info.humidity < 20.0) info.notes += "Dry air; ";

    spdlog::info("Hydration recommendation: {:.1f} L (temp={}°C, humidity={}%)",
                 info.recommendedLiters, info.temperatureC, info.humidity);

    m_Liters = info.recommendedLiters;
}

