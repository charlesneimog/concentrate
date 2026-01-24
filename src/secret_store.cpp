#include "secret_store.h"

#include <iostream>

#include <libsecret/secret.h>

namespace {
const SecretSchema &focusservice_secret_schema() {
    static SecretSchema schema{};
    static bool initialized = false;
    if (!initialized) {
        schema.name = "io.focusservice.Secret";
        schema.flags = SECRET_SCHEMA_NONE;
        schema.attributes[0].name = "key";
        schema.attributes[0].type = SECRET_SCHEMA_ATTRIBUTE_STRING;
        schema.attributes[1].name = nullptr;
        schema.attributes[1].type = static_cast<SecretSchemaAttributeType>(0);
        initialized = true;
    }
    return schema;
}
} // namespace

bool save_secret_string(const std::string &key, const std::string &value) {
    if (key.empty() || value.empty()) return false;
    GError *error = nullptr;
    gboolean ok = secret_password_store_sync(
        &focusservice_secret_schema(),
        SECRET_COLLECTION_DEFAULT,
        "focusservice secret",
        value.c_str(),
        nullptr,
        &error,
        "key",
        key.c_str(),
        nullptr);
    if (error) {
        std::cerr << "failed to store secret: " << error->message << "\n";
        g_clear_error(&error);
        return false;
    }
    return ok;
}

std::string load_secret_string(const std::string &key) {
    if (key.empty()) return "";
    GError *error = nullptr;
    gchar *secret = secret_password_lookup_sync(
        &focusservice_secret_schema(),
        nullptr,
        &error,
        "key",
        key.c_str(),
        nullptr);
    if (error) {
        std::cerr << "failed to lookup secret: " << error->message << "\n";
        g_clear_error(&error);
    }
    if (!secret) return "";
    std::string value(secret);
    secret_password_free(secret);
    return value;
}
