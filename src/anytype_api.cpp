#include "anytype_api.h"

#include "common.h"
#include "secret_store.h"

#include <httplib.h>

#include <chrono>
#include <mutex>
#include <strings.h>

namespace {
std::mutex anytype_mutex;
AnytypeCache anytype_cache;

// ─────────────────────────────────────
json extract_anytype_objects(const json &payload) {
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
    return json::array();
}

// ─────────────────────────────────────
int extract_anytype_total(const json &payload) {
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
json find_property_by_key(const json &properties, const std::string &key) {
    if (!properties.is_array()) {
        return json();
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
    return json();
}

// ─────────────────────────────────────
json extract_multi_select_names(const json &prop) {
    json out = json::array();
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
        std::string name = get_string(tag, "name", "");
        if (!name.empty()) {
            out.push_back(name);
        }
    }
    return out;
}

// ─────────────────────────────────────
json normalize_anytype_task(const json &obj, int fallback_id) {
    std::string id = get_string(obj, "id", "");
    if (id.empty()) {
        id = get_string(obj, "objectId", "");
    }
    if (id.empty()) {
        id = get_string(obj, "object_id", "");
    }
    if (id.empty()) {
        id = get_string(obj, "uid", "");
    }
    if (id.empty()) {
        id = "anytype-" + std::to_string(fallback_id);
    }

    std::string title = get_string(obj, "name", "");
    if (title.empty()) {
        title = get_string(obj, "title", "(Untitled)");
    }

    json properties = obj.contains("properties") ? obj["properties"] : json::array();
    json done_prop = find_property_by_key(properties, "done");
    json category_prop = find_property_by_key(properties, "category");
    json apps_allowed_prop = find_property_by_key(properties, "apps_allowed");
    json app_title_prop = find_property_by_key(properties, "app_title");

    bool done = done_prop.contains("checkbox") && done_prop["checkbox"].is_boolean()
                    ? done_prop["checkbox"].get<bool>()
                    : false;
    std::string category = "Uncategorized";
    if (category_prop.contains("select") && category_prop["select"].is_object()) {
        category = get_string(category_prop["select"], "name", "Uncategorized");
        if (category.empty()) {
            category = "Uncategorized";
        }
    }

    json out = json::object();
    out["id"] = id;
    out["title"] = title;
    out["category"] = category;
    out["done"] = done;
    out["allowed_app_ids"] = extract_multi_select_names(apps_allowed_prop);
    out["allowed_titles"] = extract_multi_select_names(app_title_prop);
    return out;
}

// ─────────────────────────────────────
json fetch_anytype_tasks() {
    const std::string api_key = load_secret_string("anytype_api_key");
    const std::string space_id = load_secret_string("anytype_space_id");
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

    json tasks = json::array();
    int offset = 0;
    constexpr int kMaxTasks = 2000;
    while (true) {
        json body = json::object();
        body["types"] = json::array({"task"});
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

        json payload = parse_json_or_throw(res->body);
        json objects = extract_anytype_objects(payload);
        int total = extract_anytype_total(payload);

        if (objects.is_array()) {
            int idx = 0;
            for (const auto &obj : objects) {
                json task = normalize_anytype_task(obj, offset + idx);
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
} // namespace

// ─────────────────────────────────────
bool refresh_anytype_cache(std::string *error_out) {
    try {
        json tasks = fetch_anytype_tasks();
        std::lock_guard<std::mutex> lock(anytype_mutex);
        json().swap(anytype_cache.tasks);
        anytype_cache.tasks = std::move(tasks);
        anytype_cache.updated_at =
            std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch())
                .count();
        anytype_cache.error.clear();
        anytype_cache.ready = true;
        return true;
    } catch (const std::exception &e) {
        std::lock_guard<std::mutex> lock(anytype_mutex);
        anytype_cache.error = e.what();
        anytype_cache.ready = false;
        json().swap(anytype_cache.tasks);
        if (error_out) {
            *error_out = e.what();
        }
        return false;
    }
}

// ─────────────────────────────────────
AnytypeCache get_anytype_cache() {
    std::lock_guard<std::mutex> lock(anytype_mutex);
    return anytype_cache;
}
