#include "Connections.h"
#include <fstream>
#include <filesystem>
#include <iostream>

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
