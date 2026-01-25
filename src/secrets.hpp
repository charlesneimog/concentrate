#pragma once

#include <string>
#include <libsecret/secret.h>

class Secrets {
  public:
    Secrets();

    bool SaveSecret(const std::string &key, const std::string &value);
    std::string LoadSecret(const std::string &key);

  private:
    SecretSchema m_Schema;
};
