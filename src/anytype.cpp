#include "anytype.hpp"

// ─────────────────────────────────────
Anytype::Anytype() {
    m_Secrets = Secrets();

    const char *BASE_URL = "http://localhost:31009";
    const char *API_VERSION = "2025-11-08";
    httplib::Client client(BASE_URL);
    client.set_default_headers({
        {"Anytype-Version", API_VERSION},
        {"Content-Type", "application/json"},
    });

    client.set_connection_timeout(3, 0);
    client.set_read_timeout(30, 0);
    client.set_write_timeout(30, 0);

    int Attemps = 0;
    while (true) {
        Attemps++;
        if (auto res = client.Get("/")) {
            spdlog::info("Serving on: http://localhost:31009");
            break;
        } else {
            spdlog::warn("Waiting for Anytype Server...");
        }
        std::this_thread::sleep_for(std::chrono::seconds(2));
        if (Attemps > 15) {
            spdlog::error("Waited too long for the Anytype Server. Exiting...");
            exit(1);
        }
    }
}

// ─────────────────────────────────────
Anytype::~Anytype() {}

// ─────────────────────────────────────
std::string Anytype::LoginChallengeId() {
    const char *BASE_URL = "http://localhost:31009";
    const char *API_VERSION = "2025-11-08";
    const char *APP_NAME = "FocusService";

    httplib::Client client(BASE_URL);
    client.set_default_headers({
        {"Anytype-Version", API_VERSION},
        {"Content-Type", "application/json"},
    });
    client.set_connection_timeout(3, 0);
    client.set_read_timeout(30, 0);
    client.set_write_timeout(30, 0);

    nlohmann::json body = {{"app_name", APP_NAME}};
    spdlog::info("Anytype: Requesting login challenge for app '{}'", APP_NAME);
    auto res = client.Post("/v1/auth/challenges", body.dump(), "application/json");

    if (!res) {
        spdlog::error("Anytype: Failed to connect to server for login challenge");
        throw std::runtime_error("Anytype: connection failed");
    }

    if (res->status < 200 || res->status >= 300) {
        spdlog::error("Anytype: Login challenge failed with HTTP {}: {}", res->status, res->body);
        throw std::runtime_error("Anytype: HTTP " + std::to_string(res->status) + " — " +
                                 res->body);
    }

    auto j = nlohmann::json::parse(res->body);
    std::string challenge_id = j.at("challenge_id").get<std::string>();
    spdlog::info("Anytype: Received challenge ID: {}", challenge_id);
    return challenge_id;
}

// ─────────────────────────────────────
std::string Anytype::CreateApiKey(const std::string &challenge_id, const std::string &code) {
    const char *BASE_URL = "http://localhost:31009";
    const char *API_VERSION = "2025-11-08";

    httplib::Client client(BASE_URL);
    client.set_default_headers({
        {"Anytype-Version", API_VERSION},
        {"Content-Type", "application/json"},
    });
    client.set_connection_timeout(3, 0);
    client.set_read_timeout(30, 0);
    client.set_write_timeout(30, 0);

    nlohmann::json body = {{"challenge_id", challenge_id}, {"code", code}};
    spdlog::info("Anytype: Creating API key for challenge ID: {}", challenge_id);
    auto res = client.Post("/v1/auth/api_keys", body.dump(), "application/json");

    if (!res) {
        spdlog::error("Anytype: Failed to connect to server for API key creation");
        throw std::runtime_error("Anytype: connection failed");
    }

    if (res->status < 200 || res->status >= 300) {
        spdlog::error("Anytype: API key creation failed with HTTP {}: {}", res->status, res->body);
        throw std::runtime_error("Anytype: HTTP " + std::to_string(res->status) + " — " +
                                 res->body);
    }

    auto j = nlohmann::json::parse(res->body);
    std::string api_key = j.at("api_key").get<std::string>();
    m_Secrets.SaveSecret("api_key", api_key);
    spdlog::info("Anytype: API key created and saved successfully");
    return api_key;
}

// ─────────────────────────────────────
nlohmann::json Anytype::GetSpaces() {
    const char *BASE_URL = "http://localhost:31009";
    const char *API_VERSION = "2025-11-08";
    std::string api_key = m_Secrets.LoadSecret("api_key");
    std::string space_id = m_Secrets.LoadSecret("space_id");

    httplib::Client client(BASE_URL);
    client.set_default_headers({
        {"Anytype-Version", API_VERSION},
        {"Authorization", "Bearer " + api_key},
        {"Content-Type", "application/json"},
    });
    client.set_connection_timeout(3, 0);
    client.set_read_timeout(30, 0);
    client.set_write_timeout(30, 0);

    spdlog::info("Anytype: Fetching available spaces");
    auto res = client.Get("/v1/spaces");

    if (!res) {
        spdlog::error("Anytype: Failed to connect to server for spaces");
        throw std::runtime_error("Anytype: connection failed");
    }

    if (res->status < 200 || res->status >= 300) {
        spdlog::error("Anytype: Spaces fetch failed with HTTP {}: {}", res->status, res->body);
        throw std::runtime_error("Anytype: HTTP " + std::to_string(res->status) + " — " +
                                 res->body);
    }

    spdlog::info("Anytype: Successfully fetched spaces");
    return nlohmann::json::parse(res->body);
}
// ─────────────────────────────────────
void Anytype::SetDefaultSpace(std::string space_id) {
    m_Secrets.SaveSecret("default_space_id", space_id);
    spdlog::info("Anytype: Set default space to: {}", space_id);
}

// ─────────────────────────────────────
nlohmann::json Anytype::GetPage(const std::string &id) {
    const std::string base = "http://localhost:31009";
    const std::string api_version = "2025-11-08";

    // Load secrets
    std::string api_key = m_Secrets.LoadSecret("api_key");
    std::string space_id = m_Secrets.LoadSecret("default_space_id");

    if (api_key.empty() || space_id.empty()) {
        spdlog::error("Anytype: API key or default space ID is missing");
        return {};
    }

    // Build URL
    std::string url = "/v1/spaces/" + space_id + "/objects/" + id;

    httplib::Client client(base);
    client.set_default_headers({
        {"Authorization", "Bearer " + api_key},
        {"Anytype-Version", api_version},
        {"Content-Type", "application/json"},
    });
    client.set_connection_timeout(3, 0);
    client.set_read_timeout(30, 0);
    client.set_write_timeout(30, 0);

    spdlog::debug("Anytype: Fetching page with ID: {}", id);
    if (auto res = client.Get(url.c_str())) {
        if (res->status == 200) {
            try {
                nlohmann::json page_json = nlohmann::json::parse(res->body);
                spdlog::debug("Anytype: Successfully fetched page: {}", id);
                return page_json;
            } catch (const nlohmann::json::parse_error &e) {
                spdlog::error("Anytype: Failed to parse JSON response for page {}: {}", id,
                              e.what());
                return {};
            }
        } else {
            spdlog::error("Anytype: Failed to fetch page {}, HTTP status: {}", id, res->status);
            return {};
        }
    } else {
        spdlog::error("Anytype: HTTP request failed for page {}: {}", id,
                      httplib::to_string(res.error()));
        return {};
    }
}

// ─────────────────────────────────────
nlohmann::json Anytype::GetTasks() {
    const std::string base = "http://localhost:31009";
    const std::string api_version = "2025-11-08";
    const int limit = 50;

    std::string api_key = m_Secrets.LoadSecret("api_key");
    std::string space_id = m_Secrets.LoadSecret("default_space_id");

    httplib::Client client(base);
    client.set_default_headers({
        {"Authorization", "Bearer " + api_key},
        {"Anytype-Version", api_version},
        {"Content-Type", "application/json"},
    });
    client.set_connection_timeout(3, 0);
    client.set_read_timeout(30, 0);
    client.set_write_timeout(30, 0);

    spdlog::info("Anytype: Starting task retrieval from space: {}", space_id);
    nlohmann::json tasks = nlohmann::json::array();
    int offset = 0;
    constexpr int kMaxTasks = 2000;
    while (true) {
        nlohmann::json body = nlohmann::json::object();
        body["types"] = nlohmann::json::array({"task"});
        body["offset"] = offset;
        body["limit"] = limit;

        std::string path = "/v1/spaces/" + space_id + "/search";
        spdlog::debug("Anytype: Fetching tasks batch with offset: {}, limit: {}", offset, limit);
        auto res = client.Post(path.c_str(), body.dump(), "application/json");
        if (!res) {
            spdlog::error("Anytype: Connection failed during task retrieval");
            return tasks;
        }
        if (res->status < 200 || res->status >= 300) {
            spdlog::error("Anytype: Task retrieval failed with HTTP {}: {}", res->status,
                          res->body);
            return tasks;
        }

        nlohmann::json payload = nlohmann::json::parse(res->body);
        nlohmann::json objects = GetAnytypeObjects(payload);
        int total = GetExtractLength(payload);
        if (objects.is_array()) {
            int idx = 0;
            for (const auto &obj : objects) {
                nlohmann::json task = NormalizeTask(obj, offset + idx);
                spdlog::debug("Anytype: Processing task ID: {}", task["id"].get<std::string>());

                bool done = task.contains("done") && task["done"].is_boolean()
                                ? task["done"].get<bool>()
                                : false;
                if (!done) {
                    // get markdown
                    nlohmann::json page = GetPage(task["id"].get<std::string>());
                    task["markdown"] = page["object"]["markdown"].get<std::string>();
                    tasks.push_back(task);
                }
                idx += 1;
            }
        }

        if (tasks.size() >= static_cast<size_t>(kMaxTasks)) {
            spdlog::warn("Anytype: Reached maximum task limit of {}", kMaxTasks);
            break;
        }

        if (!objects.is_array() || objects.size() < static_cast<size_t>(limit)) {
            break;
        }
        if (total >= 0 && offset + limit >= total) {
            break;
        }
        offset += limit;
    }

    spdlog::info("Anytype: Completed task retrieval, found {} active tasks", tasks.size());
    return tasks;
}

// ─────────────────────────────────────
nlohmann::json Anytype::GetAnytypeObjects(const nlohmann::json &payload) {
    if (payload.contains("data") && payload["data"].is_object()) {
        const auto &data = payload["data"];
        if (data.contains("objects") && data["objects"].is_array()) {
            return data["objects"];
        }
        if (data.contains("results") && data["results"].is_array()) {
            return data["results"];
        }
    }
    if (payload.contains("objects") && payload["objects"].is_array()) {
        return payload["objects"];
    }
    if (payload.contains("results") && payload["results"].is_array()) {
        return payload["results"];
    }
    if (payload.contains("data") && payload["data"].is_array()) {
        return payload["data"];
    }
    return nlohmann::json::array();
}

// ─────────────────────────────────────
int Anytype::GetExtractLength(const nlohmann::json &payload) {
    if (payload.contains("data") && payload["data"].is_object()) {
        const auto &data = payload["data"];
        if (data.contains("total") && data["total"].is_number()) {
            return data["total"].get<int>();
        }
    }
    if (payload.contains("total") && payload["total"].is_number()) {
        return payload["total"].get<int>();
    }
    if (payload.contains("meta") && payload["meta"].is_object()) {
        const auto &meta = payload["meta"];
        if (meta.contains("total") && meta["total"].is_number()) {
            return meta["total"].get<int>();
        }
    }
    if (payload.contains("pagination") && payload["pagination"].is_object()) {
        const auto &pagination = payload["pagination"];
        if (pagination.contains("total") && pagination["total"].is_number()) {
            return pagination["total"].get<int>();
        }
    }
    return -1;
}

// ─────────────────────────────────────
nlohmann::json Anytype::GetCategoriesOfTasks() {
    const std::string base = "http://localhost:31009";
    const std::string api_version = "2025-11-08";

    // Load secrets
    std::string api_key = m_Secrets.LoadSecret("api_key");
    std::string space_id = m_Secrets.LoadSecret("default_space_id");
    std::string prop_id = m_Secrets.LoadSecret("task_categories_id");

    if (api_key.empty() || space_id.empty()) {
        spdlog::error("Anytype: API key or default space ID is missing");
        return {};
    }

    // Build URL
    std::string url = "/v1/spaces/" + space_id + "/properties/" + prop_id + "/tags";

    httplib::Client client(base);
    client.set_default_headers({
        {"Authorization", "Bearer " + api_key},
        {"Anytype-Version", api_version},
        {"Content-Type", "application/json"},
    });

    client.set_connection_timeout(3, 0);
    client.set_read_timeout(30, 0);
    client.set_write_timeout(30, 0);

    if (auto res = client.Get(url.c_str())) {
        if (res->status == 200) {
            try {
                nlohmann::json page_json = nlohmann::json::parse(res->body);
                return page_json;
            } catch (const nlohmann::json::parse_error &e) {
                spdlog::error("Anytype: Failed to parse JSON response for page {}: {}", prop_id,
                              e.what());
                return {};
            }
        } else {
            spdlog::error("Anytype: Failed to fetch page {}, HTTP status: {}", prop_id,
                          res->status);
            return {};
        }
    } else {
        spdlog::error("Anytype: HTTP request failed for page {}: {}", prop_id,
                      httplib::to_string(res.error()));
        return {};
    }
}

// ─────────────────────────────────────
nlohmann::json Anytype::PropertyByKey(const nlohmann::json &properties, const std::string &key) {
    if (!properties.is_array()) {
        return nlohmann::json();
    }
    for (const auto &prop : properties) {
        if (!prop.is_object()) {
            continue;
        }
        auto it = prop.find("key");
        if (it == prop.end() || !it->is_string()) {
            continue;
        }
        std::string prop_key = it->get<std::string>();
        if (strcasecmp(prop_key.c_str(), key.c_str()) == 0) {
            return prop;
        }
    }
    return nlohmann::json();
}

// ─────────────────────────────────────
std::string Anytype::GetString(const nlohmann::json &j, const std::string &key,
                               const std::string &fallback) {
    if (!j.contains(key)) {
        return fallback;
    }

    if (j.at(key).is_string()) {
        return j.at(key).get<std::string>();
    }

    return fallback;
}

// ─────────────────────────────────────
nlohmann::json Anytype::ExtractArray(const nlohmann::json &prop) {
    nlohmann::json out = nlohmann::json::array();

    if (!prop.is_object()) {
        return out;
    }

    auto it = prop.find("multi_select");
    if (it == prop.end() || !it->is_array()) {
        return out;
    }

    for (const auto &tag : *it) {
        if (!tag.is_object()) {
            continue;
        }

        std::string name = GetString(tag, "name", "");
        if (!name.empty()) {
            out.push_back(name);
        }
    }

    return out;
}

// ─────────────────────────────────────
nlohmann::json Anytype::NormalizeTask(const nlohmann::json &obj, int fallback_id) {
    std::string id = GetString(obj, "id", "");
    if (id.empty()) {
        id = GetString(obj, "objectId", "");
    }
    if (id.empty()) {
        id = GetString(obj, "object_id", "");
    }
    if (id.empty()) {
        id = GetString(obj, "uid", "");
    }
    if (id.empty()) {
        id = "anytype-" + std::to_string(fallback_id);
        spdlog::warn("Anytype: Task at index {} has no valid ID, generated: {}", fallback_id, id);
    }

    std::string title = GetString(obj, "name", "");
    if (title.empty()) {
        title = GetString(obj, "title", "(Untitled)");
    }
    if (title == "(Untitled)") {
        spdlog::debug("Anytype: Task {} has no title, using default", id);
    }

    nlohmann::json properties =
        obj.contains("properties") ? obj["properties"] : nlohmann::json::array();
    nlohmann::json done_prop = PropertyByKey(properties, "done");
    nlohmann::json category_prop = PropertyByKey(properties, "category");
    nlohmann::json apps_allowed_prop = PropertyByKey(properties, "apps_allowed");
    nlohmann::json app_title_prop = PropertyByKey(properties, "app_title");
    nlohmann::json priority_key = PropertyByKey(properties, "priority");

    if (m_Secrets.LoadSecret("task_categories_id").empty()) {
        for (auto prop : properties) {
            if (prop["key"] == "category") {
                bool ok = m_Secrets.SaveSecret("task_categories_id", prop["id"]);
                if (!ok) {
                    spdlog::error("Impossible to save task_categories_id on Secrets");
                }
                break;
            }
        }
    }

    bool done = done_prop.contains("checkbox") && done_prop["checkbox"].is_boolean()
                    ? done_prop["checkbox"].get<bool>()
                    : false;
    std::string category = "Uncategorized";
    if (category_prop.contains("select") && category_prop["select"].is_object()) {
        category = GetString(category_prop["select"], "name", "Uncategorized");
        if (category.empty()) {
            category = "Uncategorized";
        }
    }
    if (category == "Uncategorized") {
        spdlog::debug("Anytype: Task {} has no category, using default", id);
    }

    nlohmann::json out = nlohmann::json::object();
    out["id"] = id;
    out["title"] = title;
    out["category"] = category;
    out["done"] = done;
    out["priority"] = priority_key["select"]; //["name"];
    out["allowed_app_ids"] = ExtractArray(apps_allowed_prop);
    out["allowed_titles"] = ExtractArray(app_title_prop);

    spdlog::debug("Anytype: Normalized task {}: title='{}', category='{}', done={}", id, title,
                  category, done);
    return out;
}
