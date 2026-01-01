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
            orgs.push_back({item.value("id", ""), item.value("name", "")});
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
        j.push_back({{"id", item.id}, {"name", item.name}});
    }

    std::ofstream file(get_config_path());
    file << j.dump(4);
}

// Fetches the list of projects associated with a specific organization.
// Filters the global project list by the provided organization ID.
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

// Updates the project list for a specific organization in storage.
// Merges new project data with existing records to keep the config synchronized.
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
                projectId,
                item.value("username", ""),
                decrypt_value(item.value("password", ""))
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
            {"projectId", projectId},
            {"username", c.username},
            {"password", encrypt_value(c.password)}
        });
    }

    std::ofstream outfile(get_connections_config_path());
    outfile << updated.dump(4);
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

    // Connect exit signal
    if (on_exit) {
        auto cb_ptr = new std::function<void()>(on_exit);
        g_signal_connect_data(terminal, "child-exited", G_CALLBACK(+[](VteTerminal*, gint, gpointer data) {
            auto cb = static_cast<std::function<void()>*>(data);
            (*cb)();
            delete cb;
        }), cb_ptr, NULL, (GConnectFlags)0);
    }

    // Spawn SSH command
    char* argv[] = {
        (char*)"gcloud", (char*)"compute", (char*)"ssh",
        (char*)conn.id.c_str(),
        (char*)"--project", (char*)conn.projectId.c_str(),
        (char*)"--zone", (char*)conn.zone.c_str(),
        (char*)"--tunnel-through-iap",
        NULL
    };

    vte_terminal_spawn_async(terminal,
        VTE_PTY_DEFAULT,
        NULL, // working directory
        argv,
        NULL, // envv
        (GSpawnFlags)0,
        NULL, NULL, // child setup
        NULL, // child pid
        -1, // timeout
        NULL, // cancellable
        NULL, NULL // callback
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
void ConnectionManager::open_rdp_session(Gtk::Box& session_container, const ConnectionInfo& conn, std::function<void()> on_exit) {
    std::string username = conn.username;
    std::string password = conn.password;

    if (username.empty() || password.empty()) {
        // 0. Ask for credentials via a simple dialog
        Gtk::Window* toplevel = dynamic_cast<Gtk::Window*>(session_container.get_toplevel());
        Gtk::Dialog login_dialog("RDP Credentials for " + conn.id, *toplevel, true);
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
        user_entry.set_text("user_" + conn.id);
        user_entry.set_hexpand(true);

        Gtk::Label pass_label("Password:");
        pass_label.set_halign(Gtk::ALIGN_START);
        Gtk::Entry pass_entry;
        pass_entry.set_visibility(false);
        pass_entry.set_hexpand(true);

        Gtk::CheckButton save_check("Save credentials");

        Gtk::Label header_label("Enter credentials for " + conn.id);
        header_label.set_margin_bottom(10);
        header_label.get_style_context()->add_class("h1"); // Try to use a heading style if available

        grid.attach(header_label, 0, 0, 2, 1);
        grid.attach(user_label, 0, 1, 1, 1);
        grid.attach(user_entry, 1, 1, 1, 1);
        grid.attach(pass_label, 0, 2, 1, 1);
        grid.attach(pass_entry, 1, 2, 1, 1);
        grid.attach(save_check, 1, 3, 1, 1);

        login_dialog.get_content_area()->pack_start(grid, Gtk::PACK_EXPAND_WIDGET);

        login_dialog.add_button("Cancel", Gtk::RESPONSE_CANCEL);
        login_dialog.add_button("Connect", Gtk::RESPONSE_OK);
        login_dialog.show_all_children();


        if (login_dialog.run() == Gtk::RESPONSE_OK) {
            username = user_entry.get_text();
            password = pass_entry.get_text();
            if (save_check.get_active()) {
                ConnectionInfo updated_conn = conn;
                updated_conn.username = username;
                updated_conn.password = password;
                save_connections(conn.projectId, {updated_conn});
            }
        } else {
            return; // User cancelled
        }
    }


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
            if (m_debug) std::cerr << "DEBUG: Port " << local_port << " is listening. Proceeding to add socket." << std::endl;

            session_container.remove(*loading_box);

            Gtk::Socket* socket_widget = Gtk::manage(new Gtk::Socket());
            session_container.pack_start(*socket_widget, Gtk::PACK_EXPAND_WIDGET);
            session_container.show_all();

            auto on_realize_logic = [&session_container, local_port, socket_widget, tunnel_pid, exit_cb_shared, username, password]() {
                uint64_t xid = socket_widget->get_id();
                if (m_debug) std::cerr << "DEBUG: Socket realized. XID: " << xid << ". Launching xfreerdp." << std::endl;


                Glib::signal_timeout().connect_once([&session_container, local_port, xid, tunnel_pid, exit_cb_shared, username, password]() {
                    if (m_debug) std::cerr << "DEBUG: Launching xfreerdp now..." << std::endl;

                    std::vector<std::string> rdp_argv = {
                        "xfreerdp",
                        "/v:127.0.0.1:" + std::to_string(local_port),
                        "/parent-window:" + std::to_string(xid),
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

            if (socket_widget->get_realized()) {
                on_realize_logic();
            } else {
                socket_widget->signal_realize().connect(on_realize_logic);
            }

            return false;
        }


        return true;
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








