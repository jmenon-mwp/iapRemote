#include "Connections.h"
#include <fstream>
#include <filesystem>
#include <iostream>
#include <unordered_map>
#include <thread>
#include <mutex>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/buffer.h>
#include <regex>

namespace fs = std::filesystem;
using json = nlohmann::json;

std::vector<GPid> ConnectionManager::m_active_pids;
bool ConnectionManager::m_debug = false;

// Sets the global debug flag for the application.
// Controls whether detailed logs are printed to the console.
void ConnectionManager::set_debug(bool debug) {
    m_debug = debug;
}

// Initializes the ConnectionManager by ensuring config directories exist.
// Creates the default organization storage file if it is missing.
void ConnectionManager::init() {
    ensure_config_dir();
    std::string path = get_config_path();
    if (!fs::exists(path)) {
        std::ofstream file(path);
        file << "[]";
    }
}

// Executes a shell command and captures its standard output.
// Uses popen to run the command and returns the result as a string.
std::string ConnectionManager::exec_command(const std::string& cmd) {
    std::array<char, 128> buffer;
    std::string result;
    std::unique_ptr<FILE, std::function<int(FILE*)>> pipe(popen(cmd.c_str(), "r"), [](FILE* f) { return pclose(f); });
    if (!pipe) return "";
    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
        result += buffer.data();
    }
    return result;
}

// Returns the absolute path to the organizations configuration file.
// Typically located in the user's .config directory for persistence.
std::string ConnectionManager::get_config_path() {
    std::string home = std::getenv("HOME");
    return home + "/.config/iapRemote/organizations.json";
}

// Returns the absolute path to the projects configuration file.
// Used to store the relationship between organizations and projects.
std::string ConnectionManager::get_projects_config_path() {
    std::string home = std::getenv("HOME");
    return home + "/.config/iapRemote/projects.json";
}

// Returns the absolute path to the connections configuration file.
// This file stores instance details like IDs, zones, and credentials.
std::string ConnectionManager::get_connections_config_path() {
    std::string home = std::getenv("HOME");
    return home + "/.config/iapRemote/connections.json";
}

// Creates the application's configuration directory if it doesn't exist.
// Uses standard filesystem paths to ensure a consistent local storage.
void ConnectionManager::ensure_config_dir() {
    std::string home = std::getenv("HOME");
    fs::path dir = fs::path(home) / ".config" / "iapRemote";
    if (!fs::exists(dir)) {
        fs::create_directories(dir);
    }
}

// Reads the list of organizations from local storage.
// Parses the JSON configuration file and returns a vector of OrganizationInfo.
std::vector<OrganizationInfo> ConnectionManager::load_organizations() {
    std::vector<OrganizationInfo> orgs;
    std::ifstream file(get_config_path());
    if (!file.is_open()) return orgs;

    try {
        json j;
        file >> j;
        for (auto& item : j) {
            orgs.push_back({
                item.value("id", ""),
                item.value("name", ""),
                item.value("is_expanded", false)
            });
        }
    } catch (const std::exception& e) {
        std::cerr << "Error loading organizations: " << e.what() << std::endl;
    }
    return orgs;
}

// Adds a new organization to the local configuration file.
// Appends the organization to the existing list while avoiding duplicates.
void ConnectionManager::save_organization(const OrganizationInfo& org) {
    std::vector<OrganizationInfo> orgs = load_organizations();

    // Check if it already exists
    for (const auto& existing : orgs) {
        if (existing.id == org.id) return;
    }

    orgs.push_back(org);

    json j = json::array();

    for (const auto& item : orgs) {
        j.push_back({
            {"id", item.id},
            {"name", item.name},
            {"is_expanded", item.is_expanded}
        });
    }

    std::ofstream file(get_config_path());
    file << j.dump(4);
}

// Fetches the list of projects associated with a specific organization.
// Filters the global project list by the provided organization ID.
std::vector<ProjectInfo> ConnectionManager::load_projects(const std::string& parentId) {
    std::vector<ProjectInfo> projects;
    std::ifstream file(get_projects_config_path());
    if (!file.is_open()) return projects;

    try {
        json j;
        file >> j;
        for (auto& item : j) {
            // Check both current parentId and legacy organizationId
            std::string pid = item.value("parentId", "");
            if (pid.empty()) pid = item.value("organizationId", "");

            if (pid == parentId) {
                bool is_exp = item.value("is_expanded", false);
                projects.push_back({
                    item.value("id", ""),
                    item.value("name", ""),
                    parentId,
                    is_exp
                });
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Error loading projects: " << e.what() << std::endl;
    }
    return projects;
}

void ConnectionManager::save_projects(const std::string& parentId, const std::vector<ProjectInfo>& newProjects) {
    std::ifstream infile(get_projects_config_path());
    json j = json::array();
    if (infile.is_open()) {
        try { infile >> j; } catch (...) { j = json::array(); }
    }
    infile.close();

    std::set<std::string> allIds;
    for (const auto& item : j) {
        allIds.insert(item.value("id", ""));
    }

    bool changed = false;
    for (const auto& p : newProjects) {
        if (allIds.find(p.id) == allIds.end()) {
            j.push_back({
                {"id", p.id},
                {"name", p.name},
                {"parentId", parentId},
                {"is_expanded", p.is_expanded}
            });
            allIds.insert(p.id);
            changed = true;
        }
    }

    if (changed) {
        std::ofstream outfile(get_projects_config_path());
        outfile << j.dump(4);
    }
}

// Retrieves connection details for a specific project from disk.
// Decrypts stored passwords and returns instance metadata for the UI.
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
                    projectId,
                    item.value("username", ""),
                    decrypt_value(item.value("password", ""))
                });
            }
        }
    } catch (...) {}
    return connections;
}

// Persists connection settings and credentials for a project.
// Encrypts passwords before writing them to the JSON configuration file.
// Preserves original order of connections in the file.
void ConnectionManager::save_connections(const std::string& projectId, const std::vector<ConnectionInfo>& newConns) {
    std::ifstream infile(get_connections_config_path());
    json j = json::array();
    if (infile.is_open()) {
        try { infile >> j; } catch (...) { j = json::array(); }
    }
    infile.close();

    json updated = json::array();
    std::vector<ConnectionInfo> processedNewConns = newConns;

    // 1. Update existing connections in-place or keep them if they don't match
    for (auto& item : j) {
        bool match_found = false;
        if (item.value("projectId", "") == projectId) {
            std::string id = item.value("id", "");
            for (auto it = processedNewConns.begin(); it != processedNewConns.end(); ++it) {
                if (it->id == id) {
                    // Update this item
                    item = {
                        {"id", it->id},
                        {"name", it->name},
                        {"zone", it->zone},
                        {"port", it->port},
                        {"type", it->type},
                        {"projectId", projectId},
                        {"username", it->username},
                        {"password", encrypt_value(it->password)}
                    };
                    processedNewConns.erase(it);
                    match_found = true;
                    break;
                }
            }
        }
        updated.push_back(item);
    }

    // 2. Add brand new connections (that weren't in the original file) to the end
    for (const auto& c : processedNewConns) {
        updated.push_back({
            {"id", c.id},
            {"name", c.name},
            {"zone", c.zone},
            {"port", c.port},
            {"type", c.type},
            {"projectId", projectId},
            {"username", c.username},
            {"password", encrypt_value(c.password)}
        });
    }

    std::ofstream outfile(get_connections_config_path());
    outfile << updated.dump(4);
}

void ConnectionManager::save_project_order(const std::string& orgId, const std::vector<std::string>& projectIds) {
    std::ifstream file(get_projects_config_path());
    if (!file.is_open()) return;

    json j;
    try { file >> j; } catch (...) { return; }
    file.close();

    std::map<std::string, json> org_items;
    json others = json::array();

    for (const auto& item : j) {
        std::string pid = item.value("parentId", "");
        if (pid.empty()) pid = item.value("organizationId", "");

        if (pid == orgId) {
            org_items[item.value("id", "")] = item;
        } else {
            others.push_back(item);
        }
    }

    json updated = others;
    for (const auto& pid : projectIds) {
        if (org_items.count(pid)) {
            updated.push_back(org_items[pid]);
        }
    }

    // Add back any that might have been missed (defensive)
    for (auto const& [key, val] : org_items) {
        bool found = false;
        for(const auto& pid : projectIds) if(pid == key) found = true;
        if(!found) updated.push_back(val);
    }

    std::ofstream outfile(get_projects_config_path());
    outfile << updated.dump(4);
}

void ConnectionManager::save_connection_order(const std::string& projectId, const std::vector<std::string>& connectionIds) {
    std::ifstream file(get_connections_config_path());
    if (!file.is_open()) return;

    json j;
    try { file >> j; } catch (...) { return; }
    file.close();

    std::map<std::string, json> proj_items;
    json others = json::array();

    for (const auto& item : j) {
        if (item.value("projectId", "") == projectId) {
            proj_items[item.value("id", "")] = item;
        } else {
            others.push_back(item);
        }
    }

    json updated = others;
    for (const auto& cid : connectionIds) {
        if (proj_items.count(cid)) {
            updated.push_back(proj_items[cid]);
        }
    }

    // Add back missed
    for (auto const& [key, val] : proj_items) {
        bool found = false;
        for(const auto& cid : connectionIds) if(cid == key) found = true;
        if(!found) updated.push_back(val);
    }

    std::ofstream outfile(get_connections_config_path());
    outfile << updated.dump(4);
}

std::string ConnectionManager::get_preferences_config_path() {
    std::string home = std::getenv("HOME");
    return home + "/.config/iapRemote/preferences.json";
}

Preferences ConnectionManager::load_preferences() {
    Preferences prefs;
    std::ifstream file(get_preferences_config_path());
    if (file.is_open()) {
        try {
            json j;
            file >> j;
            prefs.save_window_size = j.value("save_window_size", false);
            prefs.window_width = j.value("window_width", 800);
            prefs.window_height = j.value("window_height", 600);
            prefs.sidebar_width = j.value("sidebar_width", 250);
            if (j.contains("expanded_nodes")) {
                prefs.expanded_nodes = j["expanded_nodes"].get<std::vector<std::string>>();
            }
        } catch (...) {}
    }
    return prefs;
}

void ConnectionManager::save_preferences(const Preferences& prefs) {
    json j;
    j["save_window_size"] = prefs.save_window_size;
    j["window_width"] = prefs.window_width;
    j["window_height"] = prefs.window_height;
    j["sidebar_width"] = prefs.sidebar_width;
    j["expanded_nodes"] = prefs.expanded_nodes;

    std::ofstream file(get_preferences_config_path());
    file << j.dump(4);
}

void ConnectionManager::delete_connection(const std::string& projectId, const std::string& connectionId) {
    std::ifstream file(get_connections_config_path());
    if (!file.is_open()) return;

    json j;
    try { file >> j; } catch(...) { return; }
    file.close();

    json updated = json::array();
    for (const auto& item : j) {
        if (item.value("projectId", "") == projectId && item.value("id", "") == connectionId) {
            continue; // Skip this one
        }
        updated.push_back(item);
    }

    std::ofstream outfile(get_connections_config_path());
    outfile << updated.dump(4);
}

void ConnectionManager::delete_project(const std::string& projectId) {
    // 1. Delete associated connections
    std::ifstream cfile(get_connections_config_path());
    if (cfile.is_open()) {
        json cj;
        try { cfile >> cj; } catch(...) { cj = json::array(); }
        cfile.close();

        json c_updated = json::array();
        for (const auto& item : cj) {
            if (item.value("projectId", "") == projectId) continue;
            c_updated.push_back(item);
        }
        std::ofstream coutfile(get_connections_config_path());
        coutfile << c_updated.dump(4);
    }

    // 2. Delete the project itself
    std::ifstream pfile(get_projects_config_path());
    if (!pfile.is_open()) return;

    json pj;
    try { pfile >> pj; } catch(...) { return; }
    pfile.close();

    json p_updated = json::array();
    for (const auto& item : pj) {
        if (item.value("id", "") == projectId) continue;
        p_updated.push_back(item);
    }

    std::ofstream poutfile(get_projects_config_path());
    poutfile << p_updated.dump(4);
}

void ConnectionManager::move_project(const std::string& projectId, const std::string& newParentId) {
    std::ifstream file(get_projects_config_path());
    if (!file.is_open()) return;
    json j;
    try { file >> j; } catch (...) { return; }
    file.close();

    bool found = false;
    for (auto& item : j) {
        if (item.value("id", "") == projectId) {
            item["parentId"] = newParentId;
            if (item.contains("organizationId")) item.erase("organizationId");
            found = true;
            break;
        }
    }

    if (found) {
        std::ofstream outfile(get_projects_config_path());
        outfile << j.dump(4);
    }
}

void ConnectionManager::delete_organization(const std::string& orgId) {
    // 1. Identify projects to delete
    std::vector<std::string> projectsToDelete;
    std::ifstream pfile(get_projects_config_path());
    if (pfile.is_open()) {
        json pj;
        try { pfile >> pj; } catch(...) { pj = json::array(); }
        pfile.close();

        json p_updated = json::array();
    for (const auto& item : pj) {
        std::string ppid = item.value("parentId", "");
        if (ppid.empty()) ppid = item.value("organizationId", "");

        if (ppid == orgId) {
            projectsToDelete.push_back(item.value("id", ""));
        } else {
            p_updated.push_back(item);
        }
    }

        // Save updated projects (removed)
        std::ofstream poutfile(get_projects_config_path());
        poutfile << p_updated.dump(4);
    }

    // 2. Delete connections for those projects
    if (!projectsToDelete.empty()) {
        std::ifstream cfile(get_connections_config_path());
        if (cfile.is_open()) {
            json cj;
            try { cfile >> cj; } catch(...) { cj = json::array(); }
            cfile.close();

            json c_updated = json::array();
            for (const auto& item : cj) {
                std::string pid = item.value("projectId", "");
                bool del = false;
                for (const auto& dpid : projectsToDelete) {
                    if (pid == dpid) { del = true; break; }
                }
                if (!del) c_updated.push_back(item);
            }
            std::ofstream coutfile(get_connections_config_path());
            coutfile << c_updated.dump(4);
        }
    }

    // 3. Delete the organization itself
    std::ifstream ofile(get_config_path());
    if (!ofile.is_open()) return;

    json oj;
    try { ofile >> oj; } catch(...) { return; }
    ofile.close();

    json o_updated = json::array();
    for (const auto& item : oj) {
        if (item.value("id", "") == orgId) continue;
        o_updated.push_back(item);
    }

    std::ofstream ooutfile(get_config_path());
    ooutfile << o_updated.dump(4);
}

// Checks if the user is authenticated by listing organizations.
// Returns true if the command succeeds (exit code 0), false otherwise.
bool ConnectionManager::verify_auth() {
    int ret = system("gcloud auth print-access-token > /dev/null 2>&1");
    // WEXITSTATUS requires <sys/wait.h> which is already included.
    // If ret == 0, then we successfully got a token.
    if (ret == -1) return false;
    return (WEXITSTATUS(ret) == 0);
}

// Initiates the gcloud authentication flow via a terminal dialog.
void ConnectionManager::authenticate_user(Gtk::Window& parent, std::function<void()> on_success) {
    Gtk::Dialog dialog("Authenticate with Google Cloud", parent, true);
    dialog.set_default_size(600, 400);

    Gtk::Box* content = dialog.get_content_area();
    VteTerminal* terminal = VTE_TERMINAL(vte_terminal_new());
    Gtk::Widget* term_widget = Glib::wrap(GTK_WIDGET(terminal));

    content->pack_start(*term_widget, Gtk::PACK_EXPAND_WIDGET);
    term_widget->show();

    // Add URL matching for right-click 'Copy Link' functionality
    GError* url_err = nullptr;
    VteRegex* url_regex = vte_regex_new_for_match(
        "(https?://[^\\s'\"\\(\\)]+)", -1, 0, &url_err
    );
    if (url_regex) {
        vte_terminal_match_add_regex(terminal, url_regex, 0);
        vte_regex_unref(url_regex);
    }

    // Add right-click context menu (Copy/Paste/Copy Link)
    term_widget->add_events(Gdk::BUTTON_PRESS_MASK);
    term_widget->signal_button_press_event().connect([terminal](GdkEventButton* event) -> bool {
        if (event->type == GDK_BUTTON_PRESS && event->button == 3) {
            auto menu = Gtk::manage(new Gtk::Menu());

            char* match = vte_terminal_match_check_event(terminal, (GdkEvent*)event, nullptr);
            if (match) {
                std::string url = match;
                g_free(match);
                auto link_item = Gtk::manage(new Gtk::MenuItem("Copy Link"));
                link_item->signal_activate().connect([url]() {
                    Gtk::Clipboard::get(GDK_SELECTION_CLIPBOARD)->set_text(url);
                });
                menu->append(*link_item);
                menu->append(*Gtk::manage(new Gtk::SeparatorMenuItem()));
            }

            auto copy_item = Gtk::manage(new Gtk::MenuItem("Copy Selection"));
            copy_item->signal_activate().connect([terminal]() {
                vte_terminal_copy_clipboard_format(terminal, VTE_FORMAT_TEXT);
            });
            menu->append(*copy_item);

            auto paste_item = Gtk::manage(new Gtk::MenuItem("Paste Clipboard"));
            paste_item->signal_activate().connect([terminal]() {
                vte_terminal_paste_clipboard(terminal);
            });
            menu->append(*paste_item);

            menu->show_all();
            menu->popup(event->button, event->time);
            return true;
        }
        return false;
    }, false);

    Gtk::Box* button_box = dialog.get_action_area();
    Gtk::Button* copy_btn = Gtk::manage(new Gtk::Button("Copy Selection"));
    Gtk::Button* paste_btn = Gtk::manage(new Gtk::Button("Paste Clipboard"));

    button_box->pack_start(*copy_btn, Gtk::PACK_SHRINK);
    button_box->pack_start(*paste_btn, Gtk::PACK_SHRINK);

    copy_btn->show();
    paste_btn->show();

    copy_btn->signal_clicked().connect([terminal](){
        vte_terminal_copy_clipboard_format(terminal, VTE_FORMAT_TEXT);
    });

    paste_btn->signal_clicked().connect([terminal](){
        vte_terminal_paste_clipboard(terminal);
    });

    // Instructions
    auto label = Gtk::manage(new Gtk::Label("Log in via the link above.\nUse the buttons or right-click the terminal to Copy/Paste or Copy the Link."));
    content->pack_start(*label, Gtk::PACK_SHRINK);
    label->show();

    dialog.add_button("Close", Gtk::RESPONSE_CLOSE);

    char* argv_browser[] = {
        (char*)"gcloud", (char*)"auth", (char*)"login", (char*)"--no-launch-browser",
        NULL
    };

    vte_terminal_spawn_async(terminal,
        VTE_PTY_DEFAULT,
        NULL,
        argv_browser,
        NULL,
        (GSpawnFlags)0,
        NULL, NULL,
        NULL,
        -1,
        NULL,
        NULL, NULL
    );

    dialog.run();

    if (verify_auth()) {
        if (on_success) on_success();
    } else {
        Gtk::MessageDialog err(parent, "Authentication failed or incomplete.", false, Gtk::MESSAGE_WARNING);
        err.run();
    }
}

void ConnectionManager::configure_default_project(Gtk::Window& parent, std::function<void()> on_done) {
    Gtk::Dialog dialog("Set Default Project", parent, true);
    dialog.set_default_size(400, 180);

    auto content = dialog.get_content_area();
    content->set_spacing(10);
    content->set_margin_top(15);
    content->set_margin_bottom(15);
    content->set_margin_start(15);
    content->set_margin_end(15);

    auto label = Gtk::manage(new Gtk::Label("Enter the Project ID to set as default and quota project:"));
    label->set_line_wrap(true);
    auto entry = Gtk::manage(new Gtk::Entry());
    entry->set_placeholder_text("my-gcp-project-id");

    content->pack_start(*label, Gtk::PACK_SHRINK);
    content->pack_start(*entry, Gtk::PACK_SHRINK);

    dialog.add_button("Cancel", Gtk::RESPONSE_CANCEL);
    auto ok_btn = dialog.add_button("Set Default", Gtk::RESPONSE_OK);
    ok_btn->set_sensitive(false);

    // Enable button only when text is present
    entry->signal_changed().connect([entry, ok_btn]() {
        ok_btn->set_sensitive(entry->get_text_length() > 0);
    });

    dialog.show_all_children();

    if (dialog.run() == Gtk::RESPONSE_OK) {
        std::string selected_pid = entry->get_text();
        if (!selected_pid.empty()) {
            // Set default project
            exec_command("gcloud config set project " + selected_pid + " --quiet");
            // Set quota project
            exec_command("gcloud auth application-default set-quota-project " + selected_pid + " --quiet");

            Gtk::MessageDialog info(parent, "Project configured.", false, Gtk::MESSAGE_INFO, Gtk::BUTTONS_OK);
            info.set_secondary_text("Default project and quota project set to: " + selected_pid);
            info.run();
        }
    }

    if (on_done) on_done();
}

// Launches a dialog to discover and add new compute instances.
// Fetches instances from GCP and allows the user to save them locally.
void ConnectionManager::manage_add_connection(Gtk::Window& parent, const std::string& project_id, const std::string& project_name, std::function<void()> on_save) {
    Gtk::Dialog dialog("Add Connections to " + project_name, parent, true);
    dialog.set_default_size(650, 500);

    Gtk::Box* content = dialog.get_content_area();
    content->set_spacing(10);
    content->set_margin_start(12);
    content->set_margin_end(12);
    content->set_margin_top(12);
    content->set_margin_bottom(12);

    auto loading_box = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 10));
    auto spinner = Gtk::manage(new Gtk::Spinner());
    auto loading_label = Gtk::manage(new Gtk::Label("Fetching compute instances..."));
    loading_box->pack_start(*spinner, Gtk::PACK_SHRINK);
    loading_box->pack_start(*loading_label, Gtk::PACK_SHRINK);
    content->pack_start(*loading_box, Gtk::PACK_SHRINK);

    // Selection Columns
    class InstanceColumns : public Gtk::TreeModel::ColumnRecord {
    public:
        InstanceColumns() { add(m_col_selected); add(m_col_name); add(m_col_zone); add(m_col_status); }
        Gtk::TreeModelColumn<bool> m_col_selected;
        Gtk::TreeModelColumn<std::string> m_col_name;
        Gtk::TreeModelColumn<std::string> m_col_zone;
        Gtk::TreeModelColumn<std::string> m_col_status;
    };
    InstanceColumns inst_cols;

    auto sw = Gtk::manage(new Gtk::ScrolledWindow());
    auto inst_list = Gtk::manage(new Gtk::TreeView());
    auto refInstModel = Gtk::ListStore::create(inst_cols);
    inst_list->set_model(refInstModel);
    inst_list->append_column_editable("", inst_cols.m_col_selected);

    auto n_cell = Gtk::manage(new Gtk::CellRendererText());
    auto n_col = Gtk::manage(new Gtk::TreeViewColumn("Name", *n_cell));
    n_col->add_attribute(n_cell->property_text(), inst_cols.m_col_name);
    n_col->set_sort_column(inst_cols.m_col_name);
    inst_list->append_column(*n_col);

    auto z_cell = Gtk::manage(new Gtk::CellRendererText());
    auto z_col = Gtk::manage(new Gtk::TreeViewColumn("Zone", *z_cell));
    z_col->add_attribute(z_cell->property_text(), inst_cols.m_col_zone);
    z_col->set_sort_column(inst_cols.m_col_zone);
    inst_list->append_column(*z_col);

    sw->add(*inst_list);
    sw->set_min_content_height(250);
    content->pack_start(*sw, Gtk::PACK_EXPAND_WIDGET);

    // Connection Type and Port
    auto settings_box = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 15));

    auto type_box = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 5));
    type_box->pack_start(*Gtk::manage(new Gtk::Label("Type:")), Gtk::PACK_SHRINK);
    Gtk::ComboBoxText type_combo;
    type_combo.append("SSH", "SSH");
    type_combo.append("RDP", "RDP");
    type_combo.set_active(0);
    type_box->pack_start(type_combo, Gtk::PACK_SHRINK);

    auto port_box = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 5));
    port_box->pack_start(*Gtk::manage(new Gtk::Label("Port:")), Gtk::PACK_SHRINK);
    Gtk::Entry port_entry;
    port_entry.set_text("22");
    port_box->pack_start(port_entry, Gtk::PACK_SHRINK);

    type_combo.signal_changed().connect([&type_combo, &port_entry]() {
        if (type_combo.get_active_text() == "SSH") port_entry.set_text("22");
        else if (type_combo.get_active_text() == "RDP") port_entry.set_text("3389");
    });

    settings_box->pack_start(*type_box, Gtk::PACK_SHRINK);
    settings_box->pack_start(*port_box, Gtk::PACK_SHRINK);
    content->pack_start(*settings_box, Gtk::PACK_SHRINK);

    dialog.add_button("Cancel", Gtk::RESPONSE_CANCEL);
    Gtk::Button* ok_button = (Gtk::Button*)dialog.add_button("OK", Gtk::RESPONSE_OK);
    ok_button->set_sensitive(false);

    dialog.show_all_children();
    spinner->start();

    // Use a shared state for projects_json and fetch_done
    struct FetchState {
        std::string json;
        bool done = false;
        std::mutex mutex;
    };
    auto state = std::make_shared<FetchState>();

    std::thread fetch_thread([project_id, state]() {
        std::string res = ConnectionManager::exec_command("gcloud compute instances list --project=" + project_id + " --format=json --quiet");
        std::lock_guard<std::mutex> lock(state->mutex);
        state->json = res;
        state->done = true;
    });

    auto conn_sig = Glib::signal_timeout().connect([state, refInstModel, spinner, loading_box, loading_label, ok_button, &inst_cols]() {
        if (!state) return false;
        std::lock_guard<std::mutex> lock(state->mutex);

        if (state->done) {
            spinner->stop();
            if (!state->json.empty()) {
                try {
                    auto j = json::parse(state->json);
                    for (const auto& inst : j) {
                        auto r = *(refInstModel->append());
                        r[inst_cols.m_col_selected] = false;
                        r[inst_cols.m_col_name] = inst.value("name", "");
                        std::string zone_url = inst.value("zone", "");
                        r[inst_cols.m_col_zone] = (zone_url.find_last_of('/') != std::string::npos) ? zone_url.substr(zone_url.find_last_of('/') + 1) : zone_url;
                        r[inst_cols.m_col_status] = inst.value("status", "");
                    }
                    if (refInstModel->children().empty()) {
                        loading_label->set_text("No instances found.");
                    } else {
                        loading_box->hide();
                        ok_button->set_sensitive(true);
                    }
                } catch (...) {
                    loading_label->set_text("Error parsing instances.");
                }
            } else {
                loading_label->set_text("Failed to fetch instances.");
            }
            return false;
        }
        return true;
    }, 100);

    if (dialog.run() == Gtk::RESPONSE_OK) {
        std::vector<ConnectionInfo> connections;
        std::string type = type_combo.get_active_text();
        int port = std::stoi(port_entry.get_text());

        auto children = refInstModel->children();
        for (auto it = children.begin(); it != children.end(); ++it) {
            Gtk::TreeModel::Row r = *it;
            if (r[inst_cols.m_col_selected]) {
                ConnectionInfo ci;
                ci.id = r[inst_cols.m_col_name];
                ci.name = ci.id;
                ci.zone = r[inst_cols.m_col_zone];
                ci.port = port;
                ci.type = type;
                ci.projectId = project_id;
                connections.push_back(ci);
            }
        }
        save_connections(project_id, connections);
        if (on_save) on_save();
    }

    if (fetch_thread.joinable()) fetch_thread.join();
    conn_sig.disconnect();
}

// Initiates an SSH session through an IAP tunnel in a VTE terminal.
// Tracks the spawned process and triggers a callback when the session ends.
void ConnectionManager::open_ssh_session(Gtk::Box& session_container, const ConnectionInfo& conn, std::function<void()> on_exit) {
    // Clear box
    auto children = session_container.get_children();
    for (auto child : children) session_container.remove(*child);

    // Create VTE Terminal
    VteTerminal* terminal = VTE_TERMINAL(vte_terminal_new());
    Gtk::Widget* term_widget = Glib::wrap(GTK_WIDGET(terminal));

    session_container.pack_start(*term_widget, Gtk::PACK_EXPAND_WIDGET);
    term_widget->show();

    // Add URL matching for right-click 'Copy Link' functionality
    GError* url_err = nullptr;
    VteRegex* url_regex = vte_regex_new_for_match(
        "(https?://[^\\s'\"\\(\\)]+)", -1, 0, &url_err
    );
    if (url_regex) {
        vte_terminal_match_add_regex(terminal, url_regex, 0);
        vte_regex_unref(url_regex);
    }

    // Add right-click context menu (Copy/Paste/Copy Link)
    term_widget->add_events(Gdk::BUTTON_PRESS_MASK);
    term_widget->signal_button_press_event().connect([terminal](GdkEventButton* event) -> bool {
        if (event->type == GDK_BUTTON_PRESS && event->button == 3) {
            auto menu = Gtk::manage(new Gtk::Menu());

            char* match = vte_terminal_match_check_event(terminal, (GdkEvent*)event, nullptr);
            if (match) {
                std::string url = match;
                g_free(match);
                auto link_item = Gtk::manage(new Gtk::MenuItem("Copy Link"));
                link_item->signal_activate().connect([url]() {
                    Gtk::Clipboard::get(GDK_SELECTION_CLIPBOARD)->set_text(url);
                });
                menu->append(*link_item);
                menu->append(*Gtk::manage(new Gtk::SeparatorMenuItem()));
            }

            auto copy_item = Gtk::manage(new Gtk::MenuItem("Copy Selection"));
            copy_item->signal_activate().connect([terminal]() {
                vte_terminal_copy_clipboard_format(terminal, VTE_FORMAT_TEXT);
            });
            menu->append(*copy_item);

            auto paste_item = Gtk::manage(new Gtk::MenuItem("Paste Clipboard"));
            paste_item->signal_activate().connect([terminal]() {
                vte_terminal_paste_clipboard(terminal);
            });
            menu->append(*paste_item);

            menu->show_all();
            menu->popup(event->button, event->time);
            return true;
        }
        return false;
    }, false);

    // Connect exit signal
    if (on_exit) {
        auto cb_ptr = new std::function<void()>(on_exit);
        g_signal_connect_data(terminal, "child-exited", G_CALLBACK(+[](VteTerminal*, gint, gpointer data) {
            auto cb = static_cast<std::function<void()>*>(data);
            (*cb)();
            delete cb;
        }), cb_ptr, NULL, (GConnectFlags)0);
    }

    // Wrap the gcloud command in a shell script.
    // If the command fails (exit code != 0), it prints a message and waits for a keypress.
    std::string full_cmd = "gcloud compute ssh " + conn.id +
                        " --project " + conn.projectId +
                        " --zone " + conn.zone +
                        " --tunnel-through-iap || { echo -e \"\\r\\n\\r\\n[iapRemote] Session failed. Press any key to close this tab...\"; read -n 1 -s; }";

    char* argv[] = {
        (char*)"/bin/bash",
        (char*)"-c",
        (char*)full_cmd.c_str(),
        NULL
    };

    vte_terminal_spawn_async(terminal,
        VTE_PTY_DEFAULT,
        NULL,
        argv,
        NULL,
        (GSpawnFlags)0,
        NULL, NULL,
        NULL,
        -1,
        NULL,
        NULL, NULL
    );
}

// Finds an available local TCP port to use for the IAP tunnel.
// Temporarily binds a socket to port 0 to let the OS assign a random port.
static int get_free_port() {
    int sock = ::socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in serv_addr;
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = 0;
    ::bind(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr));
    socklen_t len = sizeof(serv_addr);
    ::getsockname(sock, (struct sockaddr *)&serv_addr, &len);
    int port = ntohs(serv_addr.sin_port);
    ::close(sock);
    return port;
}

// Establishes an RDP connection by piping through a local IAP tunnel.
// Spawns xfreerdp and embeds it into the GTK UI using an X11 socket.
bool ConnectionManager::prompt_rdp_credentials(Gtk::Window& parent, ConnectionInfo& conn) {
    Gtk::Dialog login_dialog("RDP Credentials for " + conn.id, parent, true);
    Gtk::Grid grid;
    grid.set_row_spacing(10);
    grid.set_column_spacing(10);
    grid.set_margin_top(20);
    grid.set_margin_bottom(20);
    grid.set_margin_left(20);
    grid.set_margin_right(20);

    Gtk::Label user_label("Username:");
    user_label.set_halign(Gtk::ALIGN_START);
    Gtk::Entry user_entry;
    user_entry.set_text(conn.username.empty() ? ("user_" + conn.id) : conn.username);
    user_entry.set_hexpand(true);

    Gtk::Label pass_label("Password:");
    pass_label.set_halign(Gtk::ALIGN_START);
    Gtk::Entry pass_entry;
    pass_entry.set_text(conn.password);
    pass_entry.set_visibility(false);
    pass_entry.set_hexpand(true);

    Gtk::CheckButton save_check("Save credentials");
    save_check.set_active(!conn.username.empty());

    Gtk::Label header_label("Enter credentials for " + conn.id);
    header_label.set_margin_bottom(10);
    header_label.get_style_context()->add_class("h1");

    grid.attach(header_label, 0, 0, 2, 1);
    grid.attach(user_label, 0, 1, 1, 1);
    grid.attach(user_entry, 1, 1, 1, 1);
    grid.attach(pass_label, 0, 2, 1, 1);
    grid.attach(pass_entry, 1, 2, 1, 1);
    grid.attach(save_check, 1, 3, 1, 1);

    login_dialog.get_content_area()->pack_start(grid, Gtk::PACK_EXPAND_WIDGET);
    login_dialog.add_button("Cancel", Gtk::RESPONSE_CANCEL);
    login_dialog.add_button("Save/Connect", Gtk::RESPONSE_OK);
    login_dialog.show_all_children();

    if (login_dialog.run() == Gtk::RESPONSE_OK) {
        conn.username = user_entry.get_text();
        conn.password = pass_entry.get_text();
        if (save_check.get_active()) {
            save_connections(conn.projectId, {conn});
        }
        return true;
    }
    return false;
}

void ConnectionManager::open_rdp_session(Gtk::Box& session_container, const ConnectionInfo& conn, std::function<void()> on_exit) {
    ConnectionInfo current_conn = conn;

    if (current_conn.username.empty() || current_conn.password.empty()) {
        Gtk::Window* toplevel = dynamic_cast<Gtk::Window*>(session_container.get_toplevel());
        if (!prompt_rdp_credentials(*toplevel, current_conn)) {
            if (on_exit) on_exit();
            return; // User cancelled
        }
    }

    std::string username = current_conn.username;
    std::string password = current_conn.password;

    // 1. Clear current area and show loading status
    auto children = session_container.get_children();
    for (auto child : children) session_container.remove(*child);

    auto loading_box = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_VERTICAL, 10));
    loading_box->set_valign(Gtk::ALIGN_CENTER);
    auto spinner = Gtk::manage(new Gtk::Spinner());
    auto label = Gtk::manage(new Gtk::Label("Initializing IAP Tunnel for RDP..."));

    loading_box->pack_start(*spinner, Gtk::PACK_SHRINK);
    loading_box->pack_start(*label, Gtk::PACK_SHRINK);
    session_container.pack_start(*loading_box, Gtk::PACK_EXPAND_WIDGET);
    session_container.show_all();
    spinner->start();

    int local_port = get_free_port();

    // 2. Start gcloud tunnel
    if (m_debug) std::cerr << "DEBUG: Starting tunnel for " << conn.id << " on port " << local_port << std::endl;

    std::vector<std::string> tunnel_argv = {
        "gcloud", "compute", "start-iap-tunnel",
        conn.id, "3389",
        "--local-host-port=localhost:" + std::to_string(local_port),
        "--project=" + conn.projectId,
        "--zone=" + conn.zone,
        "--quiet"
    };

    GPid tunnel_pid;
    try {
        Glib::spawn_async("", tunnel_argv, Glib::SPAWN_SEARCH_PATH | Glib::SPAWN_DO_NOT_REAP_CHILD, sigc::slot<void>(), &tunnel_pid);
        m_active_pids.push_back(tunnel_pid);
    } catch (const std::exception& e) {
        label->set_text("Failed to launch gcloud tunnel: " + std::string(e.what()));
        spinner->stop();
        if (on_exit) {
            Glib::signal_timeout().connect_once([on_exit]() {
                on_exit();
            }, 3000);
        }
        return;
    }

    auto exit_cb_shared = std::make_shared<std::function<void()>>(on_exit);

    // 3. Poll for tunnel connectivity
    Glib::signal_timeout().connect([&session_container, local_port, tunnel_pid, exit_cb_shared, loading_box, username, password]() -> bool {
        // Check if tunnel process is still alive
        if (::kill(tunnel_pid, 0) != 0) {
            if (m_debug) std::cerr << "DEBUG: Tunnel process " << tunnel_pid << " detected as DEAD during polling." << std::endl;

            if (*exit_cb_shared) (*exit_cb_shared)();
            return false;
        }

        int sock = ::socket(AF_INET, SOCK_STREAM, 0);
        struct sockaddr_in serv_addr;
        serv_addr.sin_family = AF_INET;
        serv_addr.sin_port = htons(local_port);
        inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr);

        bool connected = (::connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) == 0);
        ::close(sock);

        if (connected) {
            if (m_debug) std::cerr << "DEBUG: Port " << local_port << " is listening. Proceeding to add RDP." << std::endl;

            session_container.remove(*loading_box);

#ifndef IS_MACOS
            Gtk::Socket* socket_widget = Gtk::manage(new Gtk::Socket());
            session_container.pack_start(*socket_widget, Gtk::PACK_EXPAND_WIDGET);
            session_container.show_all();

            auto on_realize_logic = [&session_container, local_port, socket_widget, tunnel_pid, exit_cb_shared, username, password]() {
                uint64_t xid = socket_widget->get_id();
#else
            auto info_label = Gtk::manage(new Gtk::Label("RDP Session opened in separate window.\nClose the RDP window to end the session."));
            info_label->set_line_wrap();
            session_container.pack_start(*info_label, Gtk::PACK_EXPAND_WIDGET);
            session_container.show_all();

            auto on_realize_logic = [&session_container, local_port, tunnel_pid, exit_cb_shared, username, password]() {
                uint64_t xid = 0;
#endif
                if (m_debug) std::cerr << "DEBUG: RDP logic starting. XID: " << xid << ". Launching xfreerdp." << std::endl;


                Glib::signal_timeout().connect_once([&session_container, local_port, xid, tunnel_pid, exit_cb_shared, username, password]() {
                    if (m_debug) std::cerr << "DEBUG: Launching xfreerdp now..." << std::endl;

                    std::vector<std::string> rdp_argv = {
                        "xfreerdp",
                        "/v:127.0.0.1:" + std::to_string(local_port),
#ifndef IS_MACOS
                        "/parent-window:" + std::to_string(xid),
#endif
                        "/cert-ignore",
                        "/dynamic-resolution",
                        "+home-drive",
                        "/u:" + username,
                        "/p:" + password,
                        "/sec:nla",
                        "/audio-mode:0"
                    };

                    GPid rdp_pid;
                    try {
                        Glib::spawn_async("", rdp_argv, Glib::SPAWN_SEARCH_PATH | Glib::SPAWN_DO_NOT_REAP_CHILD, sigc::slot<void>(), &rdp_pid);
                        m_active_pids.push_back(rdp_pid);

                        Glib::signal_child_watch().connect([&session_container, tunnel_pid, exit_cb_shared](GPid pid, int status) {
                            int exit_code = 0;
                            if (WIFEXITED(status)) {
                                exit_code = WEXITSTATUS(status);
                            }
                            if (m_debug) std::cerr << "DEBUG: RDP process exited. Raw status: " << status << ", Exit code: " << exit_code << std::endl;


                            bool failure = (exit_code != 0 && exit_code != 12 && exit_code != 13);
                            if (failure) {
                                auto label = Gtk::manage(new Gtk::Label("RDP Connection failed. Check your credentials and network settings."));
                                label->set_line_wrap();
                                label->override_color(Gdk::RGBA("red"));
                                session_container.pack_start(*label, Gtk::PACK_SHRINK);
                                session_container.show_all();
                            }

                            ::kill(tunnel_pid, SIGKILL);
                            Glib::spawn_close_pid(pid);
                            Glib::spawn_close_pid(tunnel_pid);

                            if (failure) {
                                Glib::signal_timeout().connect_once([exit_cb_shared]() {
                                    if (*exit_cb_shared) (*exit_cb_shared)();
                                }, 3000);
                            } else {
                                if (*exit_cb_shared) (*exit_cb_shared)();
                            }
                        }, rdp_pid);

                    } catch (const std::exception& e) {
                        if (m_debug) std::cerr << "DEBUG: Failed to launch xfreerdp: " << e.what() << std::endl;

                        ::kill(tunnel_pid, SIGTERM);
                        if (*exit_cb_shared) (*exit_cb_shared)();
                    }
                }, 1000);
            };

#ifndef IS_MACOS
            if (socket_widget->get_realized()) {
                on_realize_logic();
            } else {
                socket_widget->signal_realize().connect(on_realize_logic);
            }
#else
            on_realize_logic();
#endif
        }
        return !connected;
    }, 500);
}

// Terminates all active background processes tracked by the manager.
// Sends SIGTERM to active PIDs and clears the tracking list during shutdown.
void ConnectionManager::cleanup() {
    for (auto pid : m_active_pids) {
        ::kill(pid, SIGTERM);
        Glib::spawn_close_pid(pid);
    }
    m_active_pids.clear();
}

static const unsigned char g_key[] = "iapRemote-secure-storage-key-77"; // 32 bytes approx

// Obfuscates a string value using AES-256-CBC encryption.
// Returns a Base64 encoded string containing the IV and the ciphertext.
std::string ConnectionManager::encrypt_value(const std::string& value) {
    if (value.empty()) return "";

    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    unsigned char iv[16];
    if (!RAND_bytes(iv, sizeof(iv))) return "";

    EVP_EncryptInit_ex(ctx, EVP_aes_256_cbc(), NULL, g_key, iv);

    std::vector<unsigned char> encrypted(value.size() + EVP_MAX_BLOCK_LENGTH);
    int len;
    EVP_EncryptUpdate(ctx, encrypted.data(), &len, (unsigned char*)value.c_str(), value.size());
    int ciphertext_len = len;
    EVP_EncryptFinal_ex(ctx, encrypted.data() + len, &len);
    ciphertext_len += len;

    EVP_CIPHER_CTX_free(ctx);

    // Combine IV + Ciphertext
    std::vector<unsigned char> combined;
    combined.insert(combined.end(), iv, iv + 16);
    combined.insert(combined.end(), encrypted.begin(), encrypted.begin() + ciphertext_len);

    // Base64 Encode
    BIO *bmem, *b64;
    BUF_MEM *bptr;
    b64 = BIO_new(BIO_f_base64());
    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
    bmem = BIO_new(BIO_s_mem());
    b64 = BIO_push(b64, bmem);
    BIO_write(b64, combined.data(), combined.size());
    BIO_flush(b64);
    BIO_get_mem_ptr(b64, &bptr);

    std::string result(bptr->data, bptr->length);
    BIO_free_all(b64);

    return result;
}

// Reverses the obfuscation of an encrypted string value.
// Handles legacy plaintext gracefully while restoring the original password string.
std::string ConnectionManager::decrypt_value(const std::string& value) {
    if (value.empty()) return "";

    // Base64 Decode
    BIO *b64, *bmem;
    std::vector<unsigned char> decode_buf(value.size());
    b64 = BIO_new(BIO_f_base64());
    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
    bmem = BIO_new_mem_buf(value.c_str(), value.size());
    b64 = BIO_push(b64, bmem);
    int decoded_len = BIO_read(b64, decode_buf.data(), value.size());
    BIO_free_all(b64);

    if (decoded_len < 16) return value; // Not an encrypted string or too short

    unsigned char iv[16];
    memcpy(iv, decode_buf.data(), 16);

    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    EVP_DecryptInit_ex(ctx, EVP_aes_256_cbc(), NULL, g_key, iv);

    std::vector<unsigned char> decrypted(decoded_len);
    int len;
    EVP_DecryptUpdate(ctx, decrypted.data(), &len, decode_buf.data() + 16, decoded_len - 16);
    int plaintext_len = len;
    if (EVP_DecryptFinal_ex(ctx, decrypted.data() + len, &len) <= 0) {
        EVP_CIPHER_CTX_free(ctx);
        return value; // Decryption failed, return original (maybe it was already plaintext)
    }
    plaintext_len += len;
    EVP_CIPHER_CTX_free(ctx);

    return std::string((char*)decrypted.data(), plaintext_len);
}

std::string ConnectionManager::get_folders_config_path() {
    std::string home = std::getenv("HOME");
    return home + "/.config/iapRemote/folders.json";
}

std::vector<FolderInfo> ConnectionManager::load_folders(const std::string& parentId) {
    std::vector<FolderInfo> folders;
    std::ifstream file(get_folders_config_path());
    if (!file.is_open()) return folders;

    try {
        json j;
        file >> j;
        for (auto& item : j) {
            if (item.value("parentId", "") == parentId) {
                folders.push_back({
                    item.value("id", ""),
                    item.value("name", ""),
                    parentId,
                    item.value("is_expanded", false)
                });
            }
        }
    } catch (...) {}
    return folders;
}

void ConnectionManager::save_folders(const std::string& parentId, const std::vector<FolderInfo>& newFolders) {
    std::ifstream infile(get_folders_config_path());
    json j = json::array();
    if (infile.is_open()) {
        try { infile >> j; } catch (...) { j = json::array(); }
    }
    infile.close();

    std::unordered_map<std::string, FolderInfo> parentFolders;
    json others = json::array();
    for (auto& item : j) {
        if (item.value("parentId", "") == parentId) {
            std::string fid = item.value("id", "");
            parentFolders[fid] = {
                fid,
                item.value("name", ""),
                parentId,
                item.value("is_expanded", false)
            };
        } else {
            others.push_back(item);
        }
    }

    for (const auto& f : newFolders) {
        // If we already have it, we might want to preserve is_expanded if 'f' doesn't specify it,
        // BUT 'newFolders' comes from the UI which probably doesn't track is_expanded during 'save_folders' operations (like add/delete),
        // or it might come from a fresh listing where we don't know the state.
        // Actually, 'newFolders' is passed when adding/bulk saving.
        // Let's assume 'f' is authoritative EXCEPT for expanded state if it's not tracked there.
        // However, ConnectionManager::save_folders is called after "Add Folder" where we create a new FolderInfo.
        // The simple fix is to use f.is_expanded if we have it, or keep existing.
        // Since FolderInfo now has is_expanded = false by default, we should be careful.
        // But for a NEW folder, false is correct. For existing, we might be overwriting.
        // Let's check if it exists in parentFolders.
        if(parentFolders.count(f.id)) {
             FolderInfo existing = parentFolders[f.id];
             FolderInfo updated = f;
             updated.is_expanded = existing.is_expanded; // Preserve existing state unless we explicitly want to change it (which we don't here)
             parentFolders[f.id] = updated;
        } else {
            parentFolders[f.id] = f;
        }
    }

    json updated = others;
    for (const auto& pair : parentFolders) {
        const auto& f = pair.second;
        updated.push_back({
            {"id", f.id},
            {"name", f.name},
            {"parentId", parentId},
            {"is_expanded", f.is_expanded}
        });
    }

    std::ofstream outfile(get_folders_config_path());
    outfile << updated.dump(4);
}

void ConnectionManager::save_folder_order(const std::string& parentId, const std::vector<std::string>& folderIds) {
    std::ifstream file(get_folders_config_path());
    if (!file.is_open()) return;

    json j;
    try { file >> j; } catch (...) { return; }
    file.close();

    std::map<std::string, json> folder_items;
    json others = json::array();

    for (const auto& item : j) {
        if (item.value("parentId", "") == parentId) {
            folder_items[item.value("id", "")] = item;
        } else {
            others.push_back(item);
        }
    }

    json updated = others;
    for (const auto& fid : folderIds) {
        if (folder_items.count(fid)) {
            updated.push_back(folder_items[fid]);
        }
    }

    for (auto const& [key, val] : folder_items) {
        bool found = false;
        for(const auto& fid : folderIds) if(fid == key) found = true;
        if(!found) updated.push_back(val);
    }

    std::ofstream outfile(get_folders_config_path());
    outfile << updated.dump(4);
}

void ConnectionManager::delete_folder(const std::string& folderId) {
    // 1. Delete associated projects
    auto projects = load_projects(folderId);
    for (const auto& p : projects) {
        delete_project(p.id);
    }

    // 2. Delete child folders (recursive)
    auto childFolders = load_folders(folderId);
    for (const auto& f : childFolders) {
        delete_folder(f.id);
    }

    // 3. Delete the folder itself
    std::ifstream ffile(get_folders_config_path());
    if (!ffile.is_open()) return;

    json fj;
    try { ffile >> fj; } catch(...) { return; }
    ffile.close();

    json f_updated = json::array();
    for (const auto& item : fj) {
        if (item.value("id", "") == folderId) continue;
        f_updated.push_back(item);
    }

    std::ofstream foutfile(get_folders_config_path());
    foutfile << f_updated.dump(4);
}

void ConnectionManager::set_node_expanded(const std::string& type, const std::string& id, bool expanded) {
    if (type == "org") {
        std::ifstream file(get_config_path());
        if (!file.is_open()) {
            std::cerr << "Failed to open org config for editing: " << get_config_path() << std::endl;
            return;
        }
        json j;
        try { file >> j; } catch (const std::exception& e) {
             std::cerr << "Failed to parse org config: " << e.what() << std::endl;
             return;
        }
        file.close();

        bool changed = false;
        for (auto& item : j) {
            if (item.value("id", "") == id) {
                item["is_expanded"] = expanded;
                changed = true;
                break;
            }
        }

        if (changed) {
            std::ofstream outfile(get_config_path());
            outfile << j.dump(4);
        }
    } else if (type == "folder") {
        std::ifstream file(get_folders_config_path());
        if (!file.is_open()) return;
        json j;
        try { file >> j; } catch (...) { return; }
        file.close();

        bool changed = false;
        for (auto& item : j) {
            if (item.value("id", "") == id) {
                item["is_expanded"] = expanded;
                changed = true;
                break;
            }
        }

        if (changed) {
            std::ofstream outfile(get_folders_config_path());
            outfile << j.dump(4);
        }
    } else if (type == "project") {
        std::ifstream file(get_projects_config_path());
        if (!file.is_open()) return;
        json j;
        try { file >> j; } catch (...) { return; }
        file.close();

        bool changed = false;
        for (auto& item : j) {
            if (item.value("id", "") == id) {
                item["is_expanded"] = expanded;
                changed = true;
                break;
            }
        }

        if (changed) {
            std::ofstream outfile(get_projects_config_path());
            outfile << j.dump(4);
        }
    }
}
