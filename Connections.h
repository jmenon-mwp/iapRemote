#ifndef CONNECTIONS_H
#define CONNECTIONS_H

#include <string>
#include <vector>
#include <nlohmann/json.hpp>

struct OrganizationInfo {
    std::string id;
    std::string name;
};

struct ProjectInfo {
    std::string id;
    std::string name;
    std::string organizationId;
};

struct ConnectionInfo {
    std::string id; // Usually instance name
    std::string name;
    std::string zone;
    int port;
    std::string type; // "SSH" or "RDP"
    std::string projectId;
};



class ConnectionManager {
public:
    static void init();
    static std::vector<OrganizationInfo> load_organizations();
    static void save_organization(const OrganizationInfo& org);
    static std::vector<ProjectInfo> load_projects(const std::string& orgId);
    static void save_projects(const std::string& orgId, const std::vector<ProjectInfo>& projects);
    static std::vector<ConnectionInfo> load_connections(const std::string& projectId);
    static void save_connections(const std::string& projectId, const std::vector<ConnectionInfo>& connections);
    static std::string get_config_path();
    static std::string get_projects_config_path();
    static std::string get_connections_config_path();



private:
    static void ensure_config_dir();
};

#endif
