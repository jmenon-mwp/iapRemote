#include "Connections.h"
#include <iostream>
#include <filesystem>
#include <thread>
#include <map>



using json = nlohmann::json;

class MainWindow : public Gtk::Window {
public:
    virtual ~MainWindow() {
        ConnectionManager::cleanup();
    }

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
        m_add_project_item = Gtk::manage(new Gtk::MenuItem("Add Project"));
        m_add_connection_item = Gtk::manage(new Gtk::MenuItem("Add Connection"));
        auto prefs_item = Gtk::manage(new Gtk::MenuItem("Preferences"));
        auto quit_item = Gtk::manage(new Gtk::MenuItem("Quit"));

        file_menu->append(*add_org_item);
        file_menu->append(*m_add_project_item);
        file_menu->append(*m_add_connection_item);
        file_menu->append(*prefs_item);
        file_menu->append(separator);
        file_menu->append(*quit_item);

        add_org_item->signal_activate().connect(sigc::mem_fun(*this, &MainWindow::on_add_organization_click));
        m_add_project_item->signal_activate().connect(sigc::mem_fun(*this, &MainWindow::on_add_project_click));
        m_add_project_item->set_sensitive(false);
        m_add_connection_item->signal_activate().connect(sigc::mem_fun(*this, &MainWindow::on_add_connection_click));
        m_add_connection_item->set_sensitive(false);

        prefs_item->signal_activate().connect(sigc::mem_fun(*this, &MainWindow::on_preferences_click));
        quit_item->signal_activate().connect(sigc::mem_fun(*this, &MainWindow::on_quit_click));

        m_menubar.append(*file_item);
        m_main_vbox.pack_start(m_menubar, Gtk::PACK_SHRINK);


        // Toolbar
        m_toolbar.set_name("app_toolbar");
        m_toolbar.set_size_request(-1, 24);
        m_main_vbox.pack_start(m_toolbar, Gtk::PACK_SHRINK);

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

        m_treeview.get_selection()->signal_changed().connect(sigc::mem_fun(*this, &MainWindow::on_tree_selection_changed));
        m_treeview.signal_row_activated().connect(sigc::mem_fun(*this, &MainWindow::on_connection_double_clicked));

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
        m_paned.set_position(250);


        // Load CSS for styling
        auto css_provider = Gtk::CssProvider::create();
        if (std::filesystem::exists("styles.css")) {
            try {
                css_provider->load_from_path("styles.css");
                auto screen = Gdk::Screen::get_default();
                Gtk::StyleContext::add_provider_for_screen(screen, css_provider, GTK_STYLE_PROVIDER_PRIORITY_USER);
            } catch (const std::exception& ex) {
                std::cerr << "CSS Error: " << ex.what() << std::endl;
            }
        }

        show_all_children();
    }

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
                    crow[m_columns.m_col_name] = conn.name + " (" + conn.type + ":" + std::to_string(conn.port) + ")";
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

    void on_tree_selection_changed() {
        auto iter = m_treeview.get_selection()->get_selected();
        if (iter) {
            Gtk::TreeModel::Row row = *iter;
            std::string type = row[m_columns.m_col_type];
            m_add_project_item->set_sensitive(type == "org");
            m_add_connection_item->set_sensitive(type == "project");
        } else {
            m_add_project_item->set_sensitive(false);
            m_add_connection_item->set_sensitive(false);
        }
    }

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
            auto j = json::parse(output);
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

        auto sw = Gtk::manage(new Gtk::ScrolledWindow());
        auto project_list = Gtk::manage(new Gtk::TreeView());
        auto refProjectModel = Gtk::ListStore::create(m_proj_cols);

        project_list->set_model(refProjectModel);
        project_list->append_column_editable("", m_proj_cols.m_col_selected);

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
                        auto j = json::parse(projects_json);
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


    void on_preferences_click() {
        std::cout << "Preferences clicked" << std::endl;
    }

    void on_quit_click() {
        ConnectionManager::cleanup();
        hide();
    }


protected:
    // Tree Model Columns
    class ModelColumns : public Gtk::TreeModel::ColumnRecord {
    public:
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
        ProjectModelColumns() { add(m_col_selected); add(m_col_id); add(m_col_name); }
        Gtk::TreeModelColumn<bool> m_col_selected;
        Gtk::TreeModelColumn<std::string> m_col_id;
        Gtk::TreeModelColumn<std::string> m_col_name;
    };

    ModelColumns m_columns;
    ProjectModelColumns m_proj_cols;
    Glib::RefPtr<Gtk::TreeStore> m_refTreeModel;
    Gtk::TreeView m_treeview;
    Gtk::MenuItem* m_add_project_item;
    Gtk::MenuItem* m_add_connection_item;

    Gtk::ScrolledWindow m_scrolled_window;

    int m_width;
    int m_height;
    Gtk::Box m_main_vbox{Gtk::ORIENTATION_VERTICAL};
    Gtk::MenuBar m_menubar;
    Gtk::Toolbar m_toolbar;
    Gtk::SeparatorMenuItem separator;
    Gtk::Paned m_paned;
    Gtk::Box m_left_box{Gtk::ORIENTATION_VERTICAL};
    Gtk::Box m_right_box{Gtk::ORIENTATION_VERTICAL};
    Gtk::Notebook m_notebook;
    std::map<std::string, Gtk::Widget*> m_connection_to_tab;
};


int main(int argc, char* argv[]) {
    auto app = Gtk::Application::create(argc, argv, "com.iap.remote");
    MainWindow window;
    return app->run(window);
}
