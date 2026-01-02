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

struct Preferences {
    bool save_window_size = false;
    int window_width = 800;
    int window_height = 600;
    int sidebar_width = 250;
};

class ConnectionManager {
public:
    // Initializes system configurations and directory structures.
    // Creates the default organization storage file if it is missing.
    static void init();
    // Executes an external shell command and returns its output.
    // Captures the result as a string for processing within the app.
    static std::string exec_command(const std::string& cmd);
    // Loads organization data from the persistent JSON storage.
    // Returns a vector of OrganizationInfo for the UI tree.
    static std::vector<OrganizationInfo> load_organizations();
    // Appends or updates an organization record in the local config.
    // Ensures organizations are unique before saving to disk.
    static void save_organization(const OrganizationInfo& org);
    // Retrieves projects associated with a specific organization ID.
    // Filters the global project list for the selected organization.
    static std::vector<ProjectInfo> load_projects(const std::string& orgId);
    // Persists a list of projects for an organization to disk.
    // Updates the JSON storage with discovered project metadata.
    static void save_projects(const std::string& orgId, const std::vector<ProjectInfo>& projects);
    // Fetches and decrypts connection info for a given project.
    // Restores credentials to plaintext for session initiation.
    static std::vector<ConnectionInfo> load_connections(const std::string& projectId);
    // Encrypts and saves instance connection details for a project.
    // Protects credentials before writing them to the config file.
    static void save_connections(const std::string& projectId, const std::vector<ConnectionInfo>& connections);

    // Deletes an organization and its associated projects/connections.
    static void delete_organization(const std::string& orgId);
    // Deletes a project and its associated connections.
    static void delete_project(const std::string& projectId);
    // Deletes a specific connection from a project.
    static void delete_connection(const std::string& projectId, const std::string& connectionId);

    // Reorders and saves projects for an organization based on the provided ID order.
    static void save_project_order(const std::string& orgId, const std::vector<std::string>& projectIds);
    // Reorders and saves connections for a project based on the provided ID order.
    static void save_connection_order(const std::string& projectId, const std::vector<std::string>& connectionIds);

    // Checks if the user is authenticated by listing organizations.
    static bool verify_auth();
    // Initiates the gcloud authentication flow via a terminal dialog.
    static void authenticate_user(Gtk::Window& parent, std::function<void()> on_success);
    // Prompts user to select a default project and configures gcloud.
    static void configure_default_project(Gtk::Window& parent, std::function<void()> on_done);

    // UI Related methods
    // UI helper to browse and register new cloud compute instances.
    // Interacts with gcloud to list and save discovered instance connections.
    static void manage_add_connection(Gtk::Window& parent, const std::string& projectId, const std::string& projectName, std::function<void()> on_save);
    // Starts an interactive SSH terminal session via IAP tunnel.
    // Embeds a VTE terminal into the provided session container.
    static void open_ssh_session(Gtk::Box& session_container, const ConnectionInfo& conn, std::function<void()> on_exit);
    // Launches an embedded RDP session using a secure local tunnel.
    // Bridges xfreerdp into the GTK UI through an X11 socket.
    static void open_rdp_session(Gtk::Box& session_container, const ConnectionInfo& conn, std::function<void()> on_exit);

    // Returns the storage path for organization metadata.
    // Defaults to the user's .config/iapRemote directory.
    static std::string get_config_path();
    // Returns the storage path for organization-project mappings.
    // Used to track which projects belong to which identity.
    static std::string get_projects_config_path();
    // Returns the storage path for detailed connection records.
    // Stores individual instance IDs, zones, and credentials.
    static std::string get_connections_config_path();
    // Returns the storage path for user preferences.
    static std::string get_preferences_config_path();

    // Loads user preferences from disk.
    static Preferences load_preferences();
    // Saves user preferences to disk.
    static void save_preferences(const Preferences& prefs);

    // Safe shutdown and resource release for background processes.
    // Terminates all active tunnels and monitored child processes.
    static void cleanup();
    // Toggles the global diagnostic logging level for the manager.
    // Controls the visibility of DEBUG messages in the console.
    static void set_debug(bool debug);
    static bool m_debug;
private:
    // Guarantees that the necessary configuration directories exist.
    // Initializes the local storage folder structure on first run.
    static void ensure_config_dir();
    static std::vector<GPid> m_active_pids;
    // Securely obfuscates sensitive data for local storage.
    // Uses AES-256-CBC encryption to protect stored credentials.
    static std::string encrypt_value(const std::string& value);
    // Restores obfuscated data to its original plaintext form.
    // Automatically detects and handles non-encrypted legacy data.
    static std::string decrypt_value(const std::string& value);
};

