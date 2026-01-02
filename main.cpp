#include "Connections.h"
#include <nlohmann/json.hpp>
#include <iostream>
#include <filesystem>
#include <thread>
#include <map>
#include <regex>
#include <optional>
#include "Icon.h"

using json = nlohmann::json;

class MainWindow : public Gtk::Window {
public:

    // The destructor ensures all background processes are cleaned up before the application exits.
    // Calls ConnectionManager::cleanup to kill active tunnels and terminal sessions.
    virtual ~MainWindow() {
        ConnectionManager::cleanup();
    }

    // The constructor initializes the main application window and sets up the GTK UI layout.
    // Configures the menu, toolbar, and the split-pane view for connections and sessions.
    MainWindow() : m_width(800), m_height(600) {

        ConnectionManager::init();
        set_title("IAP Remote Desktop & SSH Manager");
        set_default_size(m_width, m_height);

        // Main Container (Vertical)
        add(m_main_vbox);

        // MenuBar
        auto file_menu = Gtk::manage(new Gtk::Menu());
        auto file_item = Gtk::manage(new Gtk::MenuItem("_File", true));
        file_item->set_submenu(*file_menu);
        auto add_org_item = Gtk::manage(new Gtk::MenuItem("Add Organization"));
        auto prefs_item = Gtk::manage(new Gtk::MenuItem("Preferences"));
        auto quit_item = Gtk::manage(new Gtk::MenuItem("Quit"));
        file_menu->append(*add_org_item);
        file_menu->append(*prefs_item);
        file_menu->append(separator);
        file_menu->append(*quit_item);
        add_org_item->signal_activate().connect(sigc::mem_fun(*this, &MainWindow::on_add_organization_click));
        prefs_item->signal_activate().connect(sigc::mem_fun(*this, &MainWindow::on_preferences_click));
        quit_item->signal_activate().connect(sigc::mem_fun(*this, &MainWindow::on_quit_click));
        m_menubar.append(*file_item);

        // Help Menu
        auto help_menu = Gtk::manage(new Gtk::Menu());
        auto help_item = Gtk::manage(new Gtk::MenuItem("_Help", true));
        help_item->set_submenu(*help_menu);
        auto usage_item = Gtk::manage(new Gtk::MenuItem("Usage"));
        auto about_item = Gtk::manage(new Gtk::MenuItem("About"));
        help_menu->append(*usage_item);
        help_menu->append(*about_item);
        usage_item->signal_activate().connect(sigc::mem_fun(*this, &MainWindow::on_usage_click));
        about_item->signal_activate().connect(sigc::mem_fun(*this, &MainWindow::on_about_click));
        m_menubar.append(*help_item);
        m_main_vbox.pack_start(m_menubar, Gtk::PACK_SHRINK);

        // Main layout: Horizontal Paned
        m_paned.set_orientation(Gtk::ORIENTATION_HORIZONTAL);
        m_main_vbox.pack_start(m_paned, Gtk::PACK_EXPAND_WIDGET);

        // Left Box (Organization Tree)
        m_left_box.set_size_request(250, -1);
        m_left_box.set_name("left_pane");

        // TreeView setup
        m_refTreeModel = Gtk::TreeStore::create(m_columns);
        m_treeview.set_model(m_refTreeModel);
        m_treeview.append_column("Connections", m_columns.m_col_name);
        m_treeview.signal_row_activated().connect(sigc::mem_fun(*this, &MainWindow::on_connection_double_clicked));
        m_treeview.signal_button_press_event().connect(sigc::mem_fun(*this, &MainWindow::on_tree_button_press_event), false);
        m_scrolled_window.add(m_treeview);
        m_scrolled_window.set_policy(Gtk::POLICY_AUTOMATIC, Gtk::POLICY_AUTOMATIC);
        m_left_box.pack_start(m_scrolled_window, Gtk::PACK_EXPAND_WIDGET);

        load_tree_data();

        // Right Box (Session Container)
        m_right_box.set_name("right_pane");
        m_notebook.set_scrollable(true);
        m_notebook.set_tab_pos(Gtk::POS_TOP);
        m_right_box.pack_start(m_notebook, Gtk::PACK_EXPAND_WIDGET);

        // Add to paned
        m_paned.pack1(m_left_box, false, false);
        m_paned.pack2(m_right_box, true, true);

        m_prefs = ConnectionManager::load_preferences();
        if (m_prefs.save_window_size) {
            set_default_size(m_prefs.window_width, m_prefs.window_height);
            m_paned.set_position(m_prefs.sidebar_width);
        } else {
            m_paned.set_position(250);
        }

        // Load CSS for styling
        auto css_provider = Gtk::CssProvider::create();
        std::string css_path = "styles.css";
        if (!std::filesystem::exists(css_path)) {
            css_path = "/usr/share/iapRemote/styles.css";
        }

        if (std::filesystem::exists(css_path)) {
            try {
                css_provider->load_from_path(css_path);
                auto screen = Gdk::Screen::get_default();
                Gtk::StyleContext::add_provider_for_screen(screen, css_provider, GTK_STYLE_PROVIDER_PRIORITY_USER);
            } catch (const std::exception& ex) {
                std::cerr << "CSS Error: " << ex.what() << std::endl;
            }
        }

        // Load and Set Icon
        try {
            auto loader = Gdk::PixbufLoader::create();
            loader->write((const guint8*)APP_ICON_SVG.c_str(), APP_ICON_SVG.length());
            loader->close();
            m_app_icon = loader->get_pixbuf();
            if (m_app_icon) {
                set_icon(m_app_icon);
            }
        } catch (const std::exception& ex) {
            std::cerr << "Icon Error: " << ex.what() << std::endl;
        }

        show_all_children();
    }

    // Populates the sidebar with organizations, projects, and instances from local configuration.
    // Iterates through stored JSON files to build the hierarchical tree structure.
    void load_tree_data() {
        m_refTreeModel->clear();
        auto orgs = ConnectionManager::load_organizations();
        for (const auto& org : orgs) {
            Gtk::TreeModel::Row row = *(m_refTreeModel->append());
            row[m_columns.m_col_id] = org.id;
            row[m_columns.m_col_name] = org.name;
            row[m_columns.m_col_type] = "org";

            auto projects = ConnectionManager::load_projects(org.id);
            for (const auto& proj : projects) {
                Gtk::TreeModel::Row prow = *(m_refTreeModel->append(row.children()));
                prow[m_columns.m_col_id] = proj.id;
                prow[m_columns.m_col_name] = proj.name;
                prow[m_columns.m_col_type] = "project";

                auto connections = ConnectionManager::load_connections(proj.id);
                for (const auto& conn : connections) {
                    Gtk::TreeModel::Row crow = *(m_refTreeModel->append(prow.children()));
                    crow[m_columns.m_col_id] = conn.id;
                    crow[m_columns.m_col_name] = conn.name;

                    crow[m_columns.m_col_type] = "connection";
                    crow[m_columns.m_col_zone] = conn.zone;
                    crow[m_columns.m_col_project_id] = proj.id;
                    crow[m_columns.m_col_port] = conn.port;
                    crow[m_columns.m_col_conn_type] = conn.type;
                    crow[m_columns.m_col_username] = conn.username;
                    crow[m_columns.m_col_password] = conn.password;
                }
            }
        }
        m_treeview.expand_all();
    }

    // Displays a modal dialog to allow the user to add a new GCP organization.
    // Saves the organization details to a local configuration file for persistence.
    void on_add_organization_click() {
        // Simple dialog to fetch and add organization
        std::string output = ConnectionManager::exec_command("gcloud organizations list --format=\"json\" --quiet");

        if (output.empty()) {
            Gtk::MessageDialog md(*this, "Error", false, Gtk::MESSAGE_ERROR);
            md.set_secondary_text("Could not fetch organizations from gcloud.");
            md.run();
            return;
        }

        try {
            auto j = nlohmann::json::parse(output);
            Gtk::Dialog dialog("Add Organization", *this, true);
            Gtk::ComboBoxText combo;

            for (const auto& org : j) {
                std::string id = org.value("name", "");
                if (id.find("organizations/") == 0) id = id.substr(14);
                combo.append(id, org.value("displayName", "Unnamed"));
            }
            combo.set_active(0);

            dialog.get_content_area()->pack_start(*Gtk::manage(new Gtk::Label("Select Organization to add:")), Gtk::PACK_SHRINK);
            dialog.get_content_area()->pack_start(combo, Gtk::PACK_SHRINK);
            dialog.add_button("Cancel", Gtk::RESPONSE_CANCEL);
            dialog.add_button("OK", Gtk::RESPONSE_OK);
            dialog.show_all_children();

            if (dialog.run() == Gtk::RESPONSE_OK) {
                OrganizationInfo org;
                org.id = combo.get_active_id();
                org.name = combo.get_active_text();
                ConnectionManager::save_organization(org);
                load_tree_data();
            }
        } catch (...) {
            Gtk::MessageDialog md(*this, "Error", false, Gtk::MESSAGE_ERROR);
            md.set_secondary_text("Failed to parse organization list.");
            md.run();
        }
    }

    // Launches a multi-tier selection process to discovery and add GCP projects to an organization.
    // Communicates with gcloud to list and store project metadata locally.
    void on_add_project_click() {
        auto iter = m_treeview.get_selection()->get_selected();
        if (!iter) return;
        Gtk::TreeModel::Row row = *iter;
        std::string org_id = row[m_columns.m_col_id];
        std::string org_name = row[m_columns.m_col_name];

        Gtk::Dialog dialog("Add Projects to " + org_name, *this, true);
        dialog.set_default_size(550, 450);

        Gtk::Box* content = dialog.get_content_area();
        content->set_spacing(10);
        content->set_margin_start(12);
        content->set_margin_end(12);
        content->set_margin_top(12);
        content->set_margin_bottom(12);

        auto loading_box = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 10));
        auto spinner = Gtk::manage(new Gtk::Spinner());
        auto loading_label = Gtk::manage(new Gtk::Label("Fetching projects from organization..."));

        loading_box->pack_start(*spinner, Gtk::PACK_SHRINK);
        loading_box->pack_start(*loading_label, Gtk::PACK_SHRINK);
        content->pack_start(*loading_box, Gtk::PACK_SHRINK);

        auto filter_entry = Gtk::manage(new Gtk::SearchEntry());
        filter_entry->set_placeholder_text("Filter projects (Regex)...");
        content->pack_start(*filter_entry, Gtk::PACK_SHRINK);

        auto sw = Gtk::manage(new Gtk::ScrolledWindow());
        auto project_list = Gtk::manage(new Gtk::TreeView());
        auto refProjectModel = Gtk::ListStore::create(m_proj_cols);
        auto filter_model = Gtk::TreeModelFilter::create(refProjectModel);

        filter_model->set_visible_func([this, filter_entry](const Gtk::TreeModel::const_iterator& iter) -> bool {
            std::string text = filter_entry->get_text();
            if (text.empty()) return true;
            try {
                std::regex re(text, std::regex::icase);
                Gtk::TreeModel::Row row = *iter;
                std::string name = row[m_proj_cols.m_col_name];
                std::string id = row[m_proj_cols.m_col_id];
                return std::regex_search(name, re) || std::regex_search(id, re);
            } catch (...) {
                return true;
            }
        });

        filter_entry->signal_search_changed().connect([filter_model](){ filter_model->refilter(); });

        project_list->set_model(filter_model);

        auto toggle_cell = Gtk::manage(new Gtk::CellRendererToggle());
        toggle_cell->signal_toggled().connect([this, filter_model, refProjectModel](const Glib::ustring& path_str) {
            Gtk::TreePath path(path_str);
            auto filter_iter = filter_model->get_iter(path);
            if (filter_iter) {
                auto child_iter = filter_model->convert_iter_to_child_iter(filter_iter);
                if (child_iter) {
                    bool current = (*child_iter)[m_proj_cols.m_col_selected];
                    (*child_iter)[m_proj_cols.m_col_selected] = !current;
                }
            }
        });

        auto toggle_col = Gtk::manage(new Gtk::TreeViewColumn("", *toggle_cell));
        toggle_col->add_attribute(toggle_cell->property_active(), m_proj_cols.m_col_selected);
        project_list->append_column(*toggle_col);

        auto name_cell = Gtk::manage(new Gtk::CellRendererText());
        auto name_col = Gtk::manage(new Gtk::TreeViewColumn("Project Name", *name_cell));
        name_col->add_attribute(name_cell->property_text(), m_proj_cols.m_col_name);
        name_col->set_sort_column(m_proj_cols.m_col_name);
        name_col->set_resizable(true);
        project_list->append_column(*name_col);

        auto id_cell = Gtk::manage(new Gtk::CellRendererText());
        auto id_col = Gtk::manage(new Gtk::TreeViewColumn("Project ID", *id_cell));
        id_col->add_attribute(id_cell->property_text(), m_proj_cols.m_col_id);
        id_col->set_sort_column(m_proj_cols.m_col_id);
        id_col->set_resizable(true);
        project_list->append_column(*id_col);

        sw->add(*project_list);
        sw->set_policy(Gtk::POLICY_AUTOMATIC, Gtk::POLICY_AUTOMATIC);
        sw->set_min_content_height(300);
        content->pack_start(*sw, Gtk::PACK_EXPAND_WIDGET);

        dialog.add_button("Cancel", Gtk::RESPONSE_CANCEL);
        Gtk::Button* ok_button = (Gtk::Button*)dialog.add_button("OK", Gtk::RESPONSE_OK);
        ok_button->set_sensitive(false);

        dialog.show_all_children();
        spinner->start();

        bool fetch_done = false;
        std::string projects_json;
        std::thread fetch_thread([org_id, &projects_json, &fetch_done]() {
            projects_json = ConnectionManager::exec_command("gcloud asset search-all-resources --asset-types=cloudresourcemanager.googleapis.com/Project --scope=organizations/" + org_id + " --format=json --quiet");
            fetch_done = true;
        });


        auto conn = Glib::signal_timeout().connect([&, refProjectModel, spinner, loading_box, loading_label, ok_button]() {
            if (fetch_done) {
                if (!projects_json.empty()) {
                    try {
                        auto j = nlohmann::json::parse(projects_json);
                        for (const auto& p : j) {
                            std::string full_name = p.value("name", "");
                            std::string pid = full_name;
                            if (pid.find_last_of('/') != std::string::npos) {
                                pid = pid.substr(pid.find_last_of('/') + 1);
                            }
                            auto r = *(refProjectModel->append());
                            r[m_proj_cols.m_col_selected] = false;
                            r[m_proj_cols.m_col_name] = p.value("displayName", pid);
                            r[m_proj_cols.m_col_id] = pid;
                        }
                        if (refProjectModel->children().empty()) {
                            loading_label->set_text("No projects found in this organization.");
                            spinner->stop();
                        } else {
                            loading_box->hide();
                            ok_button->set_sensitive(true);
                        }
                    } catch (...) {
                        loading_label->set_text("Error parsing project list (maybe gcloud needs login).");
                        spinner->stop();
                    }
                } else {
                    loading_label->set_text("Failed to fetch projects (check gcloud login).");
                    spinner->stop();
                }
                return false; // Stop timeout
            }
            return true; // Continue polling
        }, 100);

        if (dialog.run() == Gtk::RESPONSE_OK) {
            std::vector<ProjectInfo> selected_projects;
            auto children = refProjectModel->children();
            for (auto it = children.begin(); it != children.end(); ++it) {
                Gtk::TreeModel::Row r = *it;
                if (r[m_proj_cols.m_col_selected]) {
                    selected_projects.push_back({r[m_proj_cols.m_col_id], r[m_proj_cols.m_col_name], org_id});
                }
            }
            ConnectionManager::save_projects(org_id, selected_projects);
            load_tree_data();
        }

        if (fetch_thread.joinable()) fetch_thread.join();
        conn.disconnect();
    }

    // Allows the user to find and add specific compute instances to a managed project.
    // Triggers the connection manager to fetch and display available instances.
    void on_add_connection_click() {
        auto iter = m_treeview.get_selection()->get_selected();
        if (!iter) return;
        Gtk::TreeModel::Row row = *iter;
        std::string project_id = row[m_columns.m_col_id];
        std::string project_name = row[m_columns.m_col_name];

        ConnectionManager::manage_add_connection(*this, project_id, project_name, [this]() {
            load_tree_data();
        });
    }

    // Opens an SSH or RDP session in a new or existing tab when an instance is double-clicked.
    // Managed session lifecycle through a tabbed notebook interface.
    void on_connection_double_clicked(const Gtk::TreeModel::Path& path, Gtk::TreeViewColumn* column) {
        auto iter = m_refTreeModel->get_iter(path);
        if (!iter) return;
        Gtk::TreeModel::Row row = *iter;
        std::string type = row[m_columns.m_col_type];
        if (type != "connection") return;

        ConnectionInfo ci;
        ci.id = row[m_columns.m_col_id];
        ci.name = ci.id;
        ci.zone = row[m_columns.m_col_zone];
        ci.projectId = row[m_columns.m_col_project_id];
        ci.type = row[m_columns.m_col_conn_type];
        ci.port = row[m_columns.m_col_port];
        ci.username = row[m_columns.m_col_username];
        ci.password = row[m_columns.m_col_password];

        std::string key = ci.projectId + "/" + ci.id;

        if (m_connection_to_tab.count(key)) {
            int page_num = m_notebook.page_num(*m_connection_to_tab[key]);
            if (page_num >= 0) {
                m_notebook.set_current_page(page_num);
                return;
            } else {
                m_connection_to_tab.erase(key);
            }
        }

        Gtk::Box* session_box = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_VERTICAL));
        session_box->show();

        std::string tab_label = row[m_columns.m_col_name];
        int page_num = m_notebook.append_page(*session_box, tab_label);

        m_connection_to_tab[key] = session_box;
        m_notebook.show_all();
        m_notebook.set_current_page(page_num);

        if (ci.type == "SSH") {
            ConnectionManager::open_ssh_session(*session_box, ci, [this, key, session_box]() {
                int idx = m_notebook.page_num(*session_box);
                if (idx >= 0) m_notebook.remove_page(idx);
                m_connection_to_tab.erase(key);
            });
        } else if (ci.type == "RDP") {
            ConnectionManager::open_rdp_session(*session_box, ci, [this, key, session_box]() {
                int idx = m_notebook.page_num(*session_box);
                if (idx >= 0) m_notebook.remove_page(idx);
                m_connection_to_tab.erase(key);
            });
        }
    }

    // Placeholder for application-wide settings such as theme or default terminal behavior.
    // Currently defined as a hook for future customization features.
    void on_preferences_click() {
        Gtk::Dialog dialog("Preferences", *this, true);

        auto content = dialog.get_content_area();
        content->set_spacing(15);
        content->set_margin_top(15);
        content->set_margin_bottom(15);
        content->set_margin_start(15);
        content->set_margin_end(15);

        Gtk::CheckButton check_save_window("Save window size on exit");
        check_save_window.set_active(m_prefs.save_window_size);
        content->pack_start(check_save_window, Gtk::PACK_SHRINK);

        dialog.add_button("Cancel", Gtk::RESPONSE_CANCEL);
        dialog.add_button("OK", Gtk::RESPONSE_OK);

        dialog.show_all_children();

        if (dialog.run() == Gtk::RESPONSE_OK) {
            m_prefs.save_window_size = check_save_window.get_active();
            // We save immediately so the flag is updated.
            // Width/Height will be updated on exit, but we can save current state now just in case.
            if (m_prefs.save_window_size) {
                int w, h;
                get_size(w, h);
                m_prefs.window_width = w;
                m_prefs.window_height = h;
                m_prefs.sidebar_width = m_paned.get_position();
            }
            ConnectionManager::save_preferences(m_prefs);
        }
    }

    // Gracefully shuts down the application and its background processes.
    // Ensures the main window is hidden and resources are released.
    void on_quit_click() {
        if (m_prefs.save_window_size) {
            int w, h;
            get_size(w, h);
            m_prefs.window_width = w;
            m_prefs.window_height = h;
            m_prefs.sidebar_width = m_paned.get_position();
            ConnectionManager::save_preferences(m_prefs);
        }
        ConnectionManager::cleanup();
        hide();
    }

    bool on_tree_button_press_event(GdkEventButton* event) {
        if (event->type == GDK_BUTTON_PRESS && event->button == 3) {
            Gtk::TreeModel::Path path;
            Gtk::TreeViewColumn* column;
            int cell_x, cell_y;

            bool row_exists = m_treeview.get_path_at_pos((int)event->x, (int)event->y, path, column, cell_x, cell_y);

            auto menu = Gtk::manage(new Gtk::Menu());

            if (row_exists) {
                m_treeview.get_selection()->select(path);
                auto iter = m_refTreeModel->get_iter(path);
                if (iter) {
                    Gtk::TreeModel::Row row = *iter;
                    std::string type = row[m_columns.m_col_type];

                    if (type == "org") {
                        auto add_proj = Gtk::manage(new Gtk::MenuItem("Add Project"));
                        add_proj->signal_activate().connect(sigc::mem_fun(*this, &MainWindow::on_add_project_click));
                        menu->append(*add_proj);

                        auto del_org = Gtk::manage(new Gtk::MenuItem("Delete Organization"));
                        del_org->signal_activate().connect(sigc::mem_fun(*this, &MainWindow::on_delete_organization_click));
                        menu->append(*del_org);

                        auto reorder_org = Gtk::manage(new Gtk::MenuItem("Reorder Projects"));
                        reorder_org->signal_activate().connect(sigc::mem_fun(*this, &MainWindow::on_reorder_projects_click));
                        menu->append(*reorder_org);

                        auto auth_item = Gtk::manage(new Gtk::MenuItem("Authenticate"));
                        auth_item->signal_activate().connect(sigc::mem_fun(*this, &MainWindow::on_authenticate_click));
                        menu->append(*auth_item);
                    } else if (type == "project") {
                        auto add_conn = Gtk::manage(new Gtk::MenuItem("Add Connection"));
                        add_conn->signal_activate().connect(sigc::mem_fun(*this, &MainWindow::on_add_connection_click));
                        menu->append(*add_conn);

                        auto del_proj = Gtk::manage(new Gtk::MenuItem("Delete Project"));
                        del_proj->signal_activate().connect(sigc::mem_fun(*this, &MainWindow::on_delete_project_click));
                        menu->append(*del_proj);

                        auto reorder_proj = Gtk::manage(new Gtk::MenuItem("Reorder Connections"));
                        reorder_proj->signal_activate().connect(sigc::mem_fun(*this, &MainWindow::on_reorder_connections_click));
                        menu->append(*reorder_proj);
                    } else if (type == "connection") {
                        auto connect_item = Gtk::manage(new Gtk::MenuItem("Connect"));
                        connect_item->signal_activate().connect([this, path, column]() {
                            on_connection_double_clicked(path, column);
                        });
                        menu->append(*connect_item);

                        auto del_conn = Gtk::manage(new Gtk::MenuItem("Delete Connection"));
                        del_conn->signal_activate().connect(sigc::mem_fun(*this, &MainWindow::on_delete_connection_click));
                        menu->append(*del_conn);

                        auto edit_conn = Gtk::manage(new Gtk::MenuItem("Edit Connection"));
                        edit_conn->signal_activate().connect(sigc::mem_fun(*this, &MainWindow::on_edit_connection_click));
                        menu->append(*edit_conn);
                    }
                }
            } else {
                auto add_org = Gtk::manage(new Gtk::MenuItem("Add Organization"));
                add_org->signal_activate().connect(sigc::mem_fun(*this, &MainWindow::on_add_organization_click));
                menu->append(*add_org);
            }

            menu->show_all();
            menu->popup(event->button, event->time);
            return true;
        }
        return false;
    }

    void on_delete_organization_click() {
        auto iter = m_treeview.get_selection()->get_selected();
        if (!iter) return;
        Gtk::TreeModel::Row row = *iter;
        std::string name = row[m_columns.m_col_name];
        std::string id = row[m_columns.m_col_id];

        Gtk::MessageDialog dialog(*this, "Delete Organization?", false, Gtk::MESSAGE_QUESTION, Gtk::BUTTONS_OK_CANCEL);
        dialog.set_secondary_text("Are you sure you want to delete organization '" + name + "'?\nThis will delete ALL projects and connections associated with it.");

        if (dialog.run() == Gtk::RESPONSE_OK) {
            ConnectionManager::delete_organization(id);
            load_tree_data();
        }
    }

    void on_delete_project_click() {
        auto iter = m_treeview.get_selection()->get_selected();
        if (!iter) return;
        Gtk::TreeModel::Row row = *iter;
        std::string name = row[m_columns.m_col_name];
        std::string id = row[m_columns.m_col_id];

        Gtk::MessageDialog dialog(*this, "Delete Project?", false, Gtk::MESSAGE_QUESTION, Gtk::BUTTONS_OK_CANCEL);
        dialog.set_secondary_text("Are you sure you want to delete project '" + name + "'?\nThis will delete ALL connections associated with it.");

        if (dialog.run() == Gtk::RESPONSE_OK) {
            ConnectionManager::delete_project(id);
            load_tree_data();
        }
    }

    void on_delete_connection_click() {
        auto iter = m_treeview.get_selection()->get_selected();
        if (!iter) return;
        Gtk::TreeModel::Row row = *iter;
        std::string name = row[m_columns.m_col_name];
        std::string id = row[m_columns.m_col_id];
        std::string projectId = row[m_columns.m_col_project_id];

        Gtk::MessageDialog dialog(*this, "Delete Connection?", false, Gtk::MESSAGE_QUESTION, Gtk::BUTTONS_OK_CANCEL);
        dialog.set_secondary_text("Are you sure you want to delete connection '" + name + "' for project '" + projectId + "'?");

        if (dialog.run() == Gtk::RESPONSE_OK) {
            ConnectionManager::delete_connection(projectId, id);
            load_tree_data();
        }
    }

    void on_edit_connection_click() {
        auto iter = m_treeview.get_selection()->get_selected();
        if (!iter) return;
        Gtk::TreeModel::Row row = *iter;
        std::string name = row[m_columns.m_col_name];
        std::string id = row[m_columns.m_col_id];
        std::string projectId = row[m_columns.m_col_project_id];
        std::string connType = row[m_columns.m_col_conn_type];
        std::string zone = row[m_columns.m_col_zone];
        int port = row[m_columns.m_col_port];

        if (connType == "SSH") {
            Gtk::MessageDialog msg(*this, "There are no editable settings for SSH connections.", false, Gtk::MESSAGE_INFO, Gtk::BUTTONS_OK);
            msg.run();
        } else if (connType == "RDP") {
            ConnectionInfo ci;
            ci.id = id;
            ci.name = name;
            ci.projectId = projectId;
            ci.type = connType;
            ci.zone = zone;
            ci.port = port;
            ci.username = row[m_columns.m_col_username];
            ci.password = row[m_columns.m_col_password];

            if (ConnectionManager::prompt_rdp_credentials(*this, ci)) {
                load_tree_data();
            }
        }
    }

    // Generic helper for reordering list
    std::optional<std::vector<std::string>> show_reorder_dialog(const std::string& title, const std::vector<std::pair<std::string, std::string>>& items) {
        Gtk::Dialog dialog(title, *this, true);
        dialog.set_default_size(400, 300);

        struct ReorderCols : public Gtk::TreeModel::ColumnRecord {
            ReorderCols() { add(m_name); add(m_id); }
            Gtk::TreeModelColumn<std::string> m_name;
            Gtk::TreeModelColumn<std::string> m_id;
        };
        ReorderCols cols;
        auto list_store = Gtk::ListStore::create(cols);

        for(const auto& item : items) {
            auto r = *(list_store->append());
            r[cols.m_id] = item.first;
            r[cols.m_name] = item.second;
        }

        Gtk::TreeView tv;
        tv.set_model(list_store);
        tv.append_column("Name", cols.m_name);
        tv.append_column("ID", cols.m_id);

        Gtk::ScrolledWindow sw;
        sw.add(tv);
        sw.set_min_content_height(200);

        Gtk::Box* content = dialog.get_content_area();
        content->pack_start(sw, Gtk::PACK_EXPAND_WIDGET);

        Gtk::Box btn_box(Gtk::ORIENTATION_HORIZONTAL);
        btn_box.set_spacing(5);

        Gtk::Button btn_up("Move Up");
        Gtk::Button btn_down("Move Down");
        btn_box.pack_start(btn_up, Gtk::PACK_SHRINK);
        btn_box.pack_start(btn_down, Gtk::PACK_SHRINK);

        content->pack_start(btn_box, Gtk::PACK_SHRINK);

        // Logic
        auto move_row = [&](int direction) { // -1 up, 1 down
            auto sel = tv.get_selection();
            auto it = sel->get_selected();
            if(it) {
                auto path = list_store->get_path(it);
                int idx = path[0]; // Get first index from path
                int new_idx = idx + direction;
                if(new_idx >= 0 && new_idx < list_store->children().size()) {
                    auto it_swap = list_store->get_iter(Gtk::TreePath(std::to_string(new_idx)));
                    list_store->iter_swap(it, it_swap);
                }
            }
        };

        btn_up.signal_clicked().connect([&](){ move_row(-1); });
        btn_down.signal_clicked().connect([&](){ move_row(1); });

        dialog.add_button("Cancel", Gtk::RESPONSE_CANCEL);
        dialog.add_button("Save Order", Gtk::RESPONSE_OK);

        dialog.show_all_children();

        if(dialog.run() == Gtk::RESPONSE_OK) {
            std::vector<std::string> ids;
            auto children = list_store->children();
            for(auto row : children) {
                ids.push_back(row[cols.m_id]);
            }
            return ids;
        }
        return std::nullopt;
    }

    void on_reorder_projects_click() {
        auto iter = m_treeview.get_selection()->get_selected();
        if(!iter) return;
        Gtk::TreeModel::Row row = *iter;
        std::string orgId = row[m_columns.m_col_id];
        std::string orgName = row[m_columns.m_col_name];

        auto projs = ConnectionManager::load_projects(orgId);
        std::vector<std::pair<std::string, std::string>> items;
        for(const auto& p : projs) items.push_back({p.id, p.name});

        auto result_ids = show_reorder_dialog("Reorder Projects for " + orgName, items);
        if(result_ids.has_value()) {
            ConnectionManager::save_project_order(orgId, result_ids.value());
            load_tree_data();
        }
    }

    void on_reorder_connections_click() {
        auto iter = m_treeview.get_selection()->get_selected();
        if(!iter) return;
        Gtk::TreeModel::Row row = *iter;
        std::string projId = row[m_columns.m_col_id];
        std::string projName = row[m_columns.m_col_name];

        auto conns = ConnectionManager::load_connections(projId);
        std::vector<std::pair<std::string, std::string>> items;
        for(const auto& c : conns) items.push_back({c.id, c.name});

        auto result_ids = show_reorder_dialog("Reorder Connections for " + projName, items);
        if(result_ids.has_value()) {
            ConnectionManager::save_connection_order(projId, result_ids.value());
            load_tree_data();
        }
    }

    void on_authenticate_click() {
        // User requested to force authentication when clicking this menu item.
        ConnectionManager::authenticate_user(*this, [this]() {
            Gtk::MessageDialog dialog(*this, "Authentication Successful", false, Gtk::MESSAGE_INFO, Gtk::BUTTONS_OK);
            dialog.run();
            ConnectionManager::configure_default_project(*this, nullptr);
        });
    }

    void on_usage_click() {
        Gtk::Dialog dialog("Usage Documentation", *this, true);
        dialog.set_default_size(650, 550);

        auto content = dialog.get_content_area();
        content->set_spacing(0);

        auto sw = Gtk::manage(new Gtk::ScrolledWindow());
        sw->set_policy(Gtk::POLICY_NEVER, Gtk::POLICY_AUTOMATIC);
        sw->set_border_width(15);

        auto label = Gtk::manage(new Gtk::Label());
        label->set_use_markup(true);
        label->set_line_wrap(true);
        label->set_xalign(0.0);
        label->set_yalign(0.0);
        label->set_markup(
            "<span size='large' weight='bold'>iapRemote Usage Guide</span>\n\n"
            "<b>Introduction</b>\n"
            "iapRemote is a native GTK application designed to manage Google Cloud Platform (GCP) "
            "compute instances via Identity-Aware Proxy (IAP). It provides a unified interface for "
            "both SSH and RDP connections without requiring public IP addresses.\n\n"
            "<b>1. Initial Setup</b>\n"
            "• Ensure the Google Cloud SDK (gcloud) is installed on your system.\n"
            "• Run <tt>gcloud auth login</tt> in a terminal to authenticate your account.\n"
            "• Ensure your IAM user has the <tt>roles/iap.tunnelResourceAccessor</tt> role.\n\n"
            "<b>2. Managing Organizations and Projects</b>\n"
            "• Use <b>File → Add Organization</b> to import your GCP organizations.\n"
            "• Right-click an organization in the sidebar to <b>Add Projects</b>. You can use the search bar and regex filter to find specific projects.\n"
            "• Projects and organizations can be reordered or deleted via the right-click context menu.\n\n"
            "<b>3. Connecting to Instances</b>\n"
            "• Right-click a project and select <b>Add Connection</b> to discover available VM instances.\n"
            "• Choose <b>SSH</b> for a native terminal session (powered by VTE).\n"
            "• Choose <b>RDP</b> for a Remote Desktop session (powered by FreeRDP). RDP sessions are seamlessly embedded into the application tabs.\n\n"
            "<b>4. Session Management</b>\n"
            "• Double-click any instance in the sidebar to initiate a connection.\n"
            "• Each connection opens in a new tab. Closing a tab will automatically terminate the underlying IAP tunnel.\n"
            "• For RDP, you can save your credentials locally. They are stored using <b>AES-256 encryption</b>.\n\n"
            "<b>5. Preferences</b>\n"
            "• Access <b>File → Preferences</b> to configure application behavior, such as saving the window size and sidebar position on exit.\n\n"
            "<b>Troubleshooting</b>\n"
            "If connections fail, verify that your gcloud session hasn't expired by clicking <b>Authenticate</b> in the organization context menu. "
            "You can also run iapRemote with the <tt>--debug</tt> flag to see detailed logs."
        );

        sw->add(*label);
        content->pack_start(*sw, Gtk::PACK_EXPAND_WIDGET);

        dialog.add_button("Close", Gtk::RESPONSE_CLOSE);
        dialog.show_all_children();
        dialog.run();
    }

    void on_about_click() {
        Gtk::AboutDialog about;
        about.set_transient_for(*this);
        about.set_program_name("iapRemote");
        about.set_version("1.0.0");
        about.set_copyright("Copyright © 2026");
        about.set_comments("Unified RDP and SSH over IAP (Identity-Aware Proxy) client for Google Cloud Platform");
        about.set_website("https://github.com/jmenon-mwp/iapRemote");
        about.set_website_label("GitHub Project Page");

        std::vector<Glib::ustring> authors;
        authors.push_back("Jayan Menon with Google Gemini");
        about.set_authors(authors);

        // We will set the logo when we have the icon embedded
        if (m_app_icon) about.set_logo(m_app_icon);

        about.run();
    }

protected:
    // Tree Model Columns
    class ModelColumns : public Gtk::TreeModel::ColumnRecord {
    public:
        // Constructor for the TreeView column record that defines the connection metadata structure.
        // Maps configuration data to visible and hidden columns in the sidebar.
        ModelColumns() {
            add(m_col_id); add(m_col_name); add(m_col_type);
            add(m_col_zone); add(m_col_port); add(m_col_project_id);
            add(m_col_conn_type); add(m_col_username); add(m_col_password);
        }
        Gtk::TreeModelColumn<std::string> m_col_id;
        Gtk::TreeModelColumn<std::string> m_col_name;
        Gtk::TreeModelColumn<std::string> m_col_type;
        Gtk::TreeModelColumn<std::string> m_col_zone;
        Gtk::TreeModelColumn<int> m_col_port;
        Gtk::TreeModelColumn<std::string> m_col_project_id;
        Gtk::TreeModelColumn<std::string> m_col_conn_type;
        Gtk::TreeModelColumn<std::string> m_col_username;
        Gtk::TreeModelColumn<std::string> m_col_password;
    };


    class ProjectModelColumns : public Gtk::TreeModel::ColumnRecord {
    public:
        // Constructor for the project selection list column record.
        // Defines the structure for the checkable project list in the discovery dialog.
        ProjectModelColumns() { add(m_col_selected); add(m_col_id); add(m_col_name); }
        Gtk::TreeModelColumn<bool> m_col_selected;
        Gtk::TreeModelColumn<std::string> m_col_id;
        Gtk::TreeModelColumn<std::string> m_col_name;
    };

    ModelColumns m_columns;
    ProjectModelColumns m_proj_cols;
    Glib::RefPtr<Gtk::TreeStore> m_refTreeModel;
    Gtk::TreeView m_treeview;

    Gtk::ScrolledWindow m_scrolled_window;

    int m_width;
    int m_height;
    Preferences m_prefs;
    Gtk::Box m_main_vbox{Gtk::ORIENTATION_VERTICAL};
    Gtk::MenuBar m_menubar;
    Gtk::SeparatorMenuItem separator;
    Gtk::Paned m_paned;
    Gtk::Box m_left_box{Gtk::ORIENTATION_VERTICAL};
    Gtk::Box m_right_box{Gtk::ORIENTATION_VERTICAL};
    Gtk::Notebook m_notebook;
    std::map<std::string, Gtk::Widget*> m_connection_to_tab;
    Gtk::TreeModel::Path m_drag_src_path;
    Glib::RefPtr<Gdk::Pixbuf> m_app_icon;
};

// The application entry point that initializes the GTK environment and parses command line arguments.
// Supports the --debug flag to enable verbose logging during runtime.
int main(int argc, char* argv[]) {
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--debug") {
            ConnectionManager::set_debug(true);
        }
    }
    auto app = Gtk::Application::create(argc, argv, "com.iap.remote", Gio::APPLICATION_NON_UNIQUE);
    MainWindow window;
    return app->run(window);
}
