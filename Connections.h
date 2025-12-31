#ifndef CONNECTIONS_H
#define CONNECTIONS_H

#include <string>
#include <vector>
#include <nlohmann/json.hpp>

struct OrganizationInfo {
    std::string id;
    std::string name;
};

class ConnectionManager {
public:
    static void init();
    static std::vector<OrganizationInfo> load_organizations();
    static void save_organization(const OrganizationInfo& org);
    static std::string get_config_path();

private:
    static void ensure_config_dir();
};

#endif
