# iapRemote - Google Cloud IAP Remote Desktop & SSH Manager

**iapRemote** is a modern, native Linux application designed to simplify remote access to Google Cloud Compute Engine instances. It leverages Google Cloud's Identity-Aware Proxy (IAP) to establish secure, encrypted tunnels for both SSH and RDP sessions without requiring public IP addresses or complex VPN configurations.

## 🚀 Key Features

*   **Integrated IAP Support**: Automatically handles the lifecycle of `gcloud` IAP tunnels for you.
*   **Tabbed Interface**: Manage multiple active SSH and RDP sessions simultaneously in a clean, organized tabbed layout.
*   **Secure Credential Storage**: RDP credentials can be saved locally with reversible AES-256 encryption to keep yours sensitive data safe from casual inspection.
*   **Hierarchical Navigation**: Easily browse through your Google Cloud Organizations, Projects, and Instances in a unified sidebar.
*   **Native Terminal**: High-performance integrated SSH terminal powered by the VTE library.
*   **Embedded RDP**: Seamlessly docks your RDP windows directly into the application for a unified experience.

## 🛠 Prerequisites

Before compiling or running **iapRemote**, ensure your system meets the following requirements:

### System Tools
*   **Google Cloud SDK**: You must have `gcloud` installed and authenticated (`gcloud auth login`).
*   **xfreerdp**: Required for RDP sessions.

### Development Libraries
The application relies on several modern C++ and Linux desktop libraries:
*   **GTKmm 3.0**: The C++ wrapper for the GTK+ toolkit used for the user interface.
*   **VTE 2.91**: For the integrated high-performance terminal.
*   **OpenSSL (libcrypto)**: Used for secure password encryption.
*   **nlohmann-json**: A header-only library for JSON configuration management.
*   **google-cloud-cpp**: Specifically the IAP component for tunnel management.

## 🏗 How to Compile

We use **CMake** to manage the build process. Follow these simple steps to build the application from source:

1.  **Initialize the Build Environment**:
    Create a build directory if you prefer to keep your source clean, or simply run the configuration in the root directory.

2.  **Configure the Project**:
    Run the following command to detect your libraries and generate the build system:
    ```bash
    cmake .
    ```

3.  **Build the Application**:
    Compile the source code into the final executable:
    ```bash
    make
    ```

4.  **Launch iapRemote**:
    Once built, you can start the application by running:
    ```bash
    ./iapRemote
    ```
    *(Optional: Run with `--debug` to see detailed connection logs in your terminal.)*

## 📁 Configuration
The application stores its configuration files (JSON) in your home directory:
`~/.config/iapRemote/`

This includes your saved organizations, project mappings, and encrypted connection details.

---
*Built with ❤️ for GCP Power Users.*
