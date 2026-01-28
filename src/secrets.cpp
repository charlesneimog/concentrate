#include "secrets.hpp"
#include <iostream>
#include <spdlog/spdlog.h>

// ─────────────────────────────────────
Secrets::Secrets()
    : m_Schema{"io.focusservice.Secret",
               SECRET_SCHEMA_NONE,
               {{"key", SECRET_SCHEMA_ATTRIBUTE_STRING},
                {nullptr, static_cast<SecretSchemaAttributeType>(0)}}} {}

// ─────────────────────────────────────
bool Secrets::SaveSecret(const std::string &key, const std::string &value) {
    if (key.empty() || value.empty()) {
        return false;
    }

    GError *error = nullptr;

    gboolean ok =
        secret_password_store_sync(&m_Schema, SECRET_COLLECTION_DEFAULT, "focusservice secret",
                                   value.c_str(), nullptr, &error, "key", key.c_str(), nullptr);

    if (error) {
        spdlog::error("failed to store secret: {}", error->message);
        g_clear_error(&error);
        return false;
    }

    return ok;
}

// ─────────────────────────────────────
std::string Secrets::LoadSecret(const std::string &key) {
    if (key.empty()) {
        return "";
    }

    GError *error = nullptr;

    gchar *secret =
        secret_password_lookup_sync(&m_Schema, nullptr, &error, "key", key.c_str(), nullptr);

    if (error) {
        spdlog::error("failed to lookup secret: {}", error->message);
        g_clear_error(&error);
        return "";
    }

    if (!secret) {
        return "";
    }

    std::string value(secret);
    secret_password_free(secret);
    return value;
}
