#include "Connections.h"
#include <fstream>
#include <filesystem>
#include <iostream>
#include <unordered_map>


namespace fs = std::filesystem;
using json = nlohmann::json;

void ConnectionManager::init() {
    ensure_config_dir();
    std::string path = get_config_path();
    if (!fs::exists(path)) {
        std::ofstream file(path);
        file << "[]";
    }
}

std::string ConnectionManager::get_config_path() {
    std::string home = std::getenv("HOME");
    return home + "/.config/iapRemote/organizations.json";
}

std::string ConnectionManager::get_projects_config_path() {
    std::string home = std::getenv("HOME");
    return home + "/.config/iapRemote/projects.json";
}

std::string ConnectionManager::get_connections_config_path() {
    std::string home = std::getenv("HOME");
    return home + "/.config/iapRemote/connections.json";
}



void ConnectionManager::ensure_config_dir() {
    std::string home = std::getenv("HOME");
    fs::path dir = fs::path(home) / ".config" / "iapRemote";
    if (!fs::exists(dir)) {
        fs::create_directories(dir);
    }
}

std::vector<OrganizationInfo> ConnectionManager::load_organizations() {
    std::vector<OrganizationInfo> orgs;
    std::ifstream file(get_config_path());
    if (!file.is_open()) return orgs;

    try {
        json j;
        file >> j;
        for (auto& item : j) {
            orgs.push_back({item.value("id", ""), item.value("name", "")});
        }
    } catch (const std::exception& e) {
        std::cerr << "Error loading organizations: " << e.what() << std::endl;
    }
    return orgs;
}

void ConnectionManager::save_organization(const OrganizationInfo& org) {
    std::vector<OrganizationInfo> orgs = load_organizations();

    // Check if it already exists
    for (const auto& existing : orgs) {
        if (existing.id == org.id) return;
    }

    orgs.push_back(org);

    json j = json::array();
    for (const auto& item : orgs) {
        j.push_back({{"id", item.id}, {"name", item.name}});
    }

    std::ofstream file(get_config_path());
    file << j.dump(4);
}

std::vector<ProjectInfo> ConnectionManager::load_projects(const std::string& orgId) {
    std::vector<ProjectInfo> projects;
    std::ifstream file(get_projects_config_path());
    if (!file.is_open()) return projects;

    try {
        json j;
        file >> j;
        for (auto& item : j) {
            if (item.value("organizationId", "") == orgId) {
                projects.push_back({item.value("id", ""), item.value("name", ""), orgId});
            }
        }
    } catch (...) {}
    return projects;
}

void ConnectionManager::save_projects(const std::string& orgId, const std::vector<ProjectInfo>& newProjects) {
    std::ifstream infile(get_projects_config_path());
    json j = json::array();
    if (infile.is_open()) {
        try { infile >> j; } catch (...) { j = json::array(); }
    }
    infile.close();

    // Use a map or set to track IDs for the current organization to avoid duplicates
    std::unordered_map<std::string, ProjectInfo> orgProjects;

    json others = json::array();
    for (auto& item : j) {
        if (item.value("organizationId", "") == orgId) {
            std::string pid = item.value("id", "");
            orgProjects[pid] = {pid, item.value("name", ""), orgId};
        } else {
            others.push_back(item);
        }
    }

    // Merge new projects
    for (const auto& p : newProjects) {
        orgProjects[p.id] = p; // This will overwrite name if it changed, but keep the record
    }

    // Combine everything back
    json updated = others;
    for (const auto& pair : orgProjects) {
        const auto& p = pair.second;
        updated.push_back({{"id", p.id}, {"name", p.name}, {"organizationId", orgId}});
    }

    std::ofstream outfile(get_projects_config_path());
    outfile << updated.dump(4);
}

std::vector<ConnectionInfo> ConnectionManager::load_connections(const std::string& projectId) {
    std::vector<ConnectionInfo> connections;
    std::ifstream file(get_connections_config_path());
    if (!file.is_open()) return connections;

    try {
        json j;
        file >> j;
        for (auto& item : j) {
            if (item.value("projectId", "") == projectId) {
                connections.push_back({
                    item.value("id", ""),
                    item.value("name", ""),
                    item.value("zone", ""),
                    item.value("port", 22),
                    item.value("type", "SSH"),
                    projectId
                });
            }
        }
    } catch (...) {}
    return connections;
}

void ConnectionManager::save_connections(const std::string& projectId, const std::vector<ConnectionInfo>& newConns) {
    std::ifstream infile(get_connections_config_path());
    json j = json::array();
    if (infile.is_open()) {
        try { infile >> j; } catch (...) { j = json::array(); }
    }
    infile.close();

    std::unordered_map<std::string, ConnectionInfo> projConns;
    json others = json::array();
    for (auto& item : j) {
        if (item.value("projectId", "") == projectId) {
            std::string id = item.value("id", "");
            projConns[id] = {
                id,
                item.value("name", ""),
                item.value("zone", ""),
                item.value("port", 22),
                item.value("type", "SSH"),
                projectId
            };
        } else {
            others.push_back(item);
        }
    }

    for (const auto& c : newConns) {
        projConns[c.id] = c;
    }

    json updated = others;
    for (const auto& pair : projConns) {
        const auto& c = pair.second;
        updated.push_back({
            {"id", c.id},
            {"name", c.name},
            {"zone", c.zone},
            {"port", c.port},
            {"type", c.type},
            {"projectId", projectId}
        });
    }

    std::ofstream outfile(get_connections_config_path());
    outfile << updated.dump(4);
}



