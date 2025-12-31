#include <gtkmm.h>
#include <iostream>
#include <filesystem>
#include <vector>
#include <nlohmann/json.hpp>
#include "Connections.h"

using json = nlohmann::json;

// Helper to execute command
static std::string exec_command(const std::string& cmd) {
    std::array<char, 128> buffer;
    std::string result;
    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd.c_str(), "r"), pclose);
    if (!pipe) return "";
    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
        result += buffer.data();
    }
    return result;
}

class MainWindow : public Gtk::Window {
public:
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
        
        m_scrolled_window.add(m_treeview);
        m_scrolled_window.set_policy(Gtk::POLICY_AUTOMATIC, Gtk::POLICY_AUTOMATIC);
        m_left_box.pack_start(m_scrolled_window, Gtk::PACK_EXPAND_WIDGET);

        load_tree_data();
        
        // Right Box (Session Container)
        m_right_box.set_name("right_pane");
        m_right_box.add(*Gtk::manage(new Gtk::Label("Session Area")));

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
        }
    }

    void on_add_organization_click() {
        // Simple dialog to fetch and add organization
        std::string output = exec_command("gcloud organizations list --format=\"json\" --quiet");
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

    void on_preferences_click() {
        std::cout << "Preferences clicked" << std::endl;
    }

    void on_quit_click() {
        hide();
    }

protected:
    // Tree Model Columns
    class ModelColumns : public Gtk::TreeModel::ColumnRecord {
    public:
        ModelColumns() { add(m_col_id); add(m_col_name); }
        Gtk::TreeModelColumn<std::string> m_col_id;
        Gtk::TreeModelColumn<std::string> m_col_name;
    };

    ModelColumns m_columns;
    Glib::RefPtr<Gtk::TreeStore> m_refTreeModel;
    Gtk::TreeView m_treeview;
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
};

int main(int argc, char* argv[]) {
    auto app = Gtk::Application::create(argc, argv, "com.iap.remote");
    MainWindow window;
    return app->run(window);
}
