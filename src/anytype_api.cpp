#include "anytype_api.h"

#include "common.h"
#include "secret_store.h"

#include <httplib.h>

#include <chrono>
#include <mutex>
#include <strings.h>

namespace {

// ─────────────────────────────────────
const json* extract_anytype_objects(const json& payload) {
    if (payload.contains("data")) {
        const auto& data = payload["data"];
        if (data.is_object()) {
            if (data.contains("objects") && data["objects"].is_array())
                return &data["objects"];
            if (data.contains("results") && data["results"].is_array())
                return &data["results"];
        }
        if (data.is_array())
            return &data;
    }
    if (payload.contains("objects") && payload["objects"].is_array())
        return &payload["objects"];
    if (payload.contains("results") && payload["results"].is_array())
        return &payload["results"];
    return nullptr;
}

// ─────────────────────────────────────
int extract_anytype_total(const json& payload) {
    const json* candidates[] = {
        payload.contains("data") ? &payload["data"] : nullptr,
        payload.contains("meta") ? &payload["meta"] : nullptr,
        payload.contains("pagination") ? &payload["pagination"] : nullptr,
        &payload
    };

    for (const json* j : candidates) {
        if (!j || !j->is_object()) continue;
        auto it = j->find("total");
        if (it != j->end() && it->is_number_integer())
            return it->get<int>();
    }
    return -1;
}

// ─────────────────────────────────────
const json* find_property_by_key(const json& properties, std::string_view key) {
    if (!properties.is_array()) return nullptr;

    for (const auto& prop : properties) {
        if (!prop.is_object()) continue;
        auto it = prop.find("key");
        if (it == prop.end() || !it->is_string()) continue;

        const auto& prop_key = it->get_ref<const std::string&>();
        if (prop_key.size() == key.size() &&
            strcasecmp(prop_key.c_str(), key.data()) == 0) {
            return &prop;
        }
    }
    return nullptr;
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
json normalize_anytype_task(const json& obj, int fallback_id) {
    auto get_id = [&]() -> std::string {
        static constexpr std::string_view keys[] = {
            "id", "objectId", "object_id", "uid"
        };
        for (auto k : keys) {
            std::string v = get_string(obj, k.data(), "");
            if (!v.empty()) return v;
        }
        return "anytype-" + std::to_string(fallback_id);
    };

    json out = json::object();
    out["id"] = get_id();

    std::string title = get_string(obj, "name", "");
    if (title.empty())
        title = get_string(obj, "title", "(Untitled)");
    out["title"] = title;

    const json& properties = obj.contains("properties") && obj["properties"].is_array()
                                 ? obj["properties"]
                                 : json::array();

    const json* done_prop = find_property_by_key(properties, "done");
    const json* category_prop = find_property_by_key(properties, "category");
    const json* apps_allowed_prop = find_property_by_key(properties, "apps_allowed");
    const json* app_title_prop = find_property_by_key(properties, "app_title");

    bool done = done_prop &&
                done_prop->contains("checkbox") &&
                (*done_prop)["checkbox"].is_boolean() &&
                (*done_prop)["checkbox"].get<bool>();

    std::string category = "Uncategorized";
    if (category_prop &&
        category_prop->contains("select") &&
        (*category_prop)["select"].is_object()) {
        category = get_string((*category_prop)["select"], "name", category);
    }

    out["done"] = done;
    out["category"] = category;
    out["allowed_app_ids"] =
        apps_allowed_prop ? extract_multi_select_names(*apps_allowed_prop)
                          : json::array();
    out["allowed_titles"] =
        app_title_prop ? extract_multi_select_names(*app_title_prop)
                       : json::array();

    return out;
}

// ─────────────────────────────────────
bool fetch_anytype_tasks(json &out, std::string &error) {
    try {
        const std::string api_key = load_secret_string("anytype_api_key");
        const std::string space_id = load_secret_string("anytype_space_id");
        if (api_key.empty() || space_id.empty()) {
            error = "missing anytype api key or space id";
            out = json::array();
            return false;
        }

        constexpr int limit = 50;
        constexpr int kMaxTasks = 2000;

        httplib::Client client("http://localhost:31009");
        client.set_default_headers({
            {"Authorization", "Bearer " + api_key},
            {"Anytype-Version", "2025-11-08"},
            {"Content-Type", "application/json"},
        });
        client.set_connection_timeout(3, 0);
        client.set_read_timeout(30, 0);
        client.set_write_timeout(30, 0);

        json tasks = json::array();
        tasks.get_ref<json::array_t&>().reserve(kMaxTasks);

        int offset = 0;
        while (offset <= 10000) {
            json body{
                {"types", json::array({"task"})},
                {"offset", offset},
                {"limit", limit},
            };

            auto res = client.Post(
                ("/v1/spaces/" + space_id + "/search").c_str(),
                body.dump(),
                "application/json"
            );

            if (!res || res->status < 200 || res->status >= 300) {
                error = "anytype request failed";
                out = json::array();
                return false;
            }

            json payload = parse_json_or_throw(res->body);
            const json* objects = extract_anytype_objects(payload);
            int total = extract_anytype_total(payload);

            if (!objects) break;

            int idx = 0;
            for (const auto& obj : *objects) {
                json task = normalize_anytype_task(obj, offset + idx++);
                if (!task.value("done", false)) {
                    tasks.push_back(std::move(task));
                    if (tasks.size() >= kMaxTasks) {
                        out = std::move(tasks);
                        return true;
                    }
                }
            }

            if (objects->size() < static_cast<size_t>(limit)) break;
            if (total >= 0 && offset + limit >= total) break;

            offset += limit;
        }

        out = std::move(tasks);
        return true;
    } catch (const std::exception &e) {
        error = e.what();
        out = json::array();
        return false;
    }
}

} // namespace

// ─────────────────────────────────────
bool fetch_anytype_tasks_live(json &out, std::string &error) {
    return fetch_anytype_tasks(out, error);
}

// ─────────────────────────────────────
bool create_anytype_auth_challenge(const std::string &app_name, std::string &challenge_id,
                                   std::string &error) {
    try {
        if (app_name.empty()) {
            error = "missing app_name";
            return false;
        }

        httplib::Client client("http://localhost:31009");
        client.set_default_headers({
            {"Anytype-Version", "2025-11-08"},
            {"Content-Type", "application/json"},
        });
        client.set_connection_timeout(3, 0);
        client.set_read_timeout(30, 0);
        client.set_write_timeout(30, 0);

        json body{{"app_name", app_name}};
        auto res = client.Post("/v1/auth/challenges", body.dump(), "application/json");
        if (!res || res->status < 200 || res->status >= 300) {
            error = "anytype auth challenge failed";
            return false;
        }

        json payload = parse_json_or_throw(res->body);
        challenge_id = get_string(payload, "challenge_id", "");
        if (challenge_id.empty()) {
            error = "missing challenge_id";
            return false;
        }
        return true;
    } catch (const std::exception &e) {
        error = e.what();
        return false;
    }
}

// ─────────────────────────────────────
bool create_anytype_api_key(const std::string &challenge_id, const std::string &code,
                            std::string &api_key, std::string &error) {
    try {
        if (challenge_id.empty() || code.empty()) {
            error = "missing challenge_id or code";
            return false;
        }

        httplib::Client client("http://localhost:31009");
        client.set_default_headers({
            {"Anytype-Version", "2025-11-08"},
            {"Content-Type", "application/json"},
        });
        client.set_connection_timeout(3, 0);
        client.set_read_timeout(30, 0);
        client.set_write_timeout(30, 0);

        json body{{"challenge_id", challenge_id}, {"code", code}};
        auto res = client.Post("/v1/auth/api_keys", body.dump(), "application/json");
        if (!res || res->status < 200 || res->status >= 300) {
            error = "anytype api key request failed";
            return false;
        }

        json payload = parse_json_or_throw(res->body);
        api_key = get_string(payload, "api_key", "");
        if (api_key.empty()) {
            error = "missing api_key";
            return false;
        }
        return true;
    } catch (const std::exception &e) {
        error = e.what();
        return false;
    }
}

// ─────────────────────────────────────
bool fetch_anytype_spaces(json &out, std::string &error) {
    try {
        const std::string api_key = load_secret_string("anytype_api_key");
        if (api_key.empty()) {
            error = "missing anytype api key";
            out = json::object();
            return false;
        }

        httplib::Client client("http://localhost:31009");
        client.set_default_headers({
            {"Authorization", "Bearer " + api_key},
            {"Anytype-Version", "2025-11-08"},
        });
        client.set_connection_timeout(3, 0);
        client.set_read_timeout(30, 0);
        client.set_write_timeout(30, 0);

        auto res = client.Get("/v1/spaces");
        if (!res || res->status < 200 || res->status >= 300) {
            error = "anytype spaces request failed";
            out = json::object();
            return false;
        }

        out = parse_json_or_throw(res->body);
        return true;
    } catch (const std::exception &e) {
        error = e.what();
        out = json::object();
        return false;
    }
}
