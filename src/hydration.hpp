#pragma once
#include <httplib.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

class HydrationService {
  public:
    struct Location {
        std::string city;
        std::string region;
        std::string country;
        double latitude = 0.0;
        double longitude = 0.0;
    };

    struct HydrationInfo {
        double temperatureC = 0.0;
        double humidity = 0.0;
        double recommendedLiters = 0.0;
        std::string notes;
    };

    HydrationService();
    Location GetLocation();
    void GetHydrationRecommendation(double weightKg = 75.0);
    double GetLiters() {
        return m_Liters;
    }

  private:
    Location m_Location;
    double m_Liters;
};
