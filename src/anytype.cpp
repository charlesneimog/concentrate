#include <httplib.h>
#include <nlohmann/json.hpp>

#include "anytype.hpp"

Anytype::Anytype() {}

// ─────────────────────────────────────
nlohmann::json Anytype::GetTasks(const std::string api_key, const std::string space_id) {
    if (api_key.empty() || space_id.empty()) {
        throw std::runtime_error("missing anytype api key or space id");
    }

    const std::string base = "http://localhost:31009";
    const std::string api_version = "2025-11-08";
    const int limit = 50;

    httplib::Client client(base);
    client.set_default_headers({
        {"Authorization", "Bearer " + api_key},
        {"Anytype-Version", api_version},
        {"Content-Type", "application/json"},
    });
    client.set_connection_timeout(3, 0);
    client.set_read_timeout(30, 0);
    client.set_write_timeout(30, 0);

    nlohmann::json tasks = nlohmann::json::array();
    int offset = 0;
    constexpr int kMaxTasks = 2000;
    while (true) {
        nlohmann::json body = nlohmann::json::object();
        body["types"] = nlohmann::json::array({"task"});
        body["offset"] = offset;
        body["limit"] = limit;

        std::string path = "/v1/spaces/" + space_id + "/search";
        auto res = client.Post(path.c_str(), body.dump(), "application/json");
        if (!res) {
            throw std::runtime_error("anytype request failed");
        }
        if (res->status < 200 || res->status >= 300) {
            throw std::runtime_error("anytype request failed: " + std::to_string(res->status));
        }

        nlohmann::json payload = nlohmann::json::parse(res->body);
        nlohmann::json objects = GetAnytypeObjects(payload);
        int total = GetExtractLength(payload);

        if (objects.is_array()) {
            int idx = 0;
            for (const auto &obj : objects) {
                nlohmann::json task = NormalizeTask(obj, offset + idx);
                bool done = task.contains("done") && task["done"].is_boolean()
                                ? task["done"].get<bool>()
                                : false;
                if (!done) {
                    tasks.push_back(task);
                }
                idx += 1;
            }
        }

        if (tasks.size() >= static_cast<size_t>(kMaxTasks)) {
            break;
        }

        if (!objects.is_array() || objects.size() < static_cast<size_t>(limit)) {
            break;
        }
        if (total >= 0 && offset + limit >= total) {
            break;
        }
        offset += limit;
        if (offset > 10000) {
            break;
        }
    }

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
    }

    std::string title = GetString(obj, "name", "");
    if (title.empty()) {
        title = GetString(obj, "title", "(Untitled)");
    }

    nlohmann::json properties =
        obj.contains("properties") ? obj["properties"] : nlohmann::json::array();
    nlohmann::json done_prop = PropertyByKey(properties, "done");
    nlohmann::json category_prop = PropertyByKey(properties, "category");
    nlohmann::json apps_allowed_prop = PropertyByKey(properties, "apps_allowed");
    nlohmann::json app_title_prop = PropertyByKey(properties, "app_title");

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

    nlohmann::json out = nlohmann::json::object();
    out["id"] = id;
    out["title"] = title;
    out["category"] = category;
    out["done"] = done;
    out["allowed_app_ids"] = ExtractArray(apps_allowed_prop);
    out["allowed_titles"] = ExtractArray(app_title_prop);
    return out;
}
