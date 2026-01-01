#pragma once

#include <gtkmm.h>
#include <gtkmm/socket.h>
#include <vte/vte.h>

#include <string>
#include <vector>
#include <functional>
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
    std::string username;
    std::string password;
};

class ConnectionManager {
public:
    static void init();
    static std::string exec_command(const std::string& cmd);
    static std::vector<OrganizationInfo> load_organizations();
    static void save_organization(const OrganizationInfo& org);
    static std::vector<ProjectInfo> load_projects(const std::string& orgId);
    static void save_projects(const std::string& orgId, const std::vector<ProjectInfo>& projects);
    static std::vector<ConnectionInfo> load_connections(const std::string& projectId);
    static void save_connections(const std::string& projectId, const std::vector<ConnectionInfo>& connections);

    // UI Related methods
    static void manage_add_connection(Gtk::Window& parent, const std::string& projectId, const std::string& projectName, std::function<void()> on_save);
    static void open_ssh_session(Gtk::Box& session_container, const ConnectionInfo& conn, std::function<void()> on_exit);
    static void open_rdp_session(Gtk::Box& session_container, const ConnectionInfo& conn, std::function<void()> on_exit);

    static std::string get_config_path();
    static std::string get_projects_config_path();
    static std::string get_connections_config_path();

    static void cleanup();
    static void set_debug(bool debug);
    static bool m_debug;
private:
    static void ensure_config_dir();
    static std::vector<GPid> m_active_pids;
};
