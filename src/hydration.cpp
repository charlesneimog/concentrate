#include "hydration.hpp"

HydrationService::HydrationService() {
    m_Location = GetLocation();
    GetHydrationRecommendation();
}

// ─────────────────────────────────────
HydrationService::Location HydrationService::GetLocation() {
    httplib::Client client("http://ip-api.com");
    client.set_connection_timeout(3, 0);
    client.set_read_timeout(10, 0);

    auto res = client.Get("/json");
    if (!res) {
        spdlog::error("Failed to get location info");
        throw std::runtime_error("Location request failed");
    }

    if (res->status != 200) {
        spdlog::error("Location API returned HTTP {}", res->status);
        throw std::runtime_error("Location API error");
    }

    auto j = nlohmann::json::parse(res->body);
    HydrationService::Location loc;
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
    httplib::Client client("https://api.open-meteo.com");
    client.set_connection_timeout(3, 0);
    client.set_read_timeout(10, 0);

    std::string url = "/v1/forecast?latitude=" + std::to_string(m_Location.latitude) +
                      "&longitude=" + std::to_string(m_Location.longitude) +
                      "&current_weather=true&timezone=auto";

    auto res = client.Get(url.c_str());
    if (!res) {
        throw std::runtime_error("Weather request failed");
    }
    if (res->status != 200) {
        throw std::runtime_error("Weather API error");
    }

    auto j = nlohmann::json::parse(res->body);
    double temp = j["current_weather"].value("temperature", 20.0);
    double humidity = j["current_weather"].value("humidity", 50.0);

    HydrationInfo info;
    info.temperatureC = temp;
    info.humidity = humidity;

    // Baseline for sedentary computer work
    double base = weightKg * 0.035; // 35 mL per kg/day

    if (temp > 20) {
        base += (temp - 20) * 0.05; // slightly more in warm rooms
    }
    if (humidity < 20) {
        base += 0.2; // small adjustment for very dry air
    }

    info.recommendedLiters = std::clamp(base, 1.5, 5.0);

    // Notes
    std::string notes;
    if (temp > 30) {
        notes += "Hot room; ";
    }
    if (humidity < 20) {
        notes += "Dry air; ";
    }
    info.notes = notes;

    spdlog::info("Hydration recommendation: {:.1f} L (temp={}°C, humidity={}%)",
                 info.recommendedLiters, info.temperatureC, info.humidity);

    m_Liters = info.recommendedLiters;
}

