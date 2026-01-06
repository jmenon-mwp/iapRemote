Name:           iapRemote
Version:        %{?pkg_version}%{!?pkg_version:1.0.0}
Release:        1%{?dist}
Summary:        Secure IAP Tunneling Client for GCP
License:        Apache-2.0
URL:            https://github.com/jmenon/iapRemote

# Disable debuginfo generation as it's very slow for large static binaries
%define debug_package %{nil}

%description
A desktop utility to manage and connect to Google Cloud instances
via Identity-Aware Proxy (IAP) tunnels for SSH and RDP.

%prep
# No prep needed

%build
# Process is handled outside by the build script

%install
# Create directories in the package buildroot
mkdir -p %{buildroot}%{_bindir}
mkdir -p %{buildroot}%{_datadir}/applications
mkdir -p %{buildroot}%{_datadir}/icons/hicolor/scalable/apps
mkdir -p %{buildroot}%{_datadir}/iapRemote

# Copy from our pre-built project directory (passed as %{project_root})
cp %{project_root}/build_rpm_dist/iapRemote %{buildroot}%{_bindir}/
cp %{project_root}/packaging/rpm_dist/iapRemote.desktop %{buildroot}%{_datadir}/applications/
cp %{project_root}/icon.svg %{buildroot}%{_datadir}/icons/hicolor/scalable/apps/iapRemote.svg
cp %{project_root}/styles.css %{buildroot}%{_datadir}/iapRemote/

%files
%{_bindir}/iapRemote
%{_datadir}/applications/iapRemote.desktop
%{_datadir}/icons/hicolor/scalable/apps/iapRemote.svg
%{_datadir}/iapRemote/styles.css

%changelog
* Fri Jan 02 2026 Jayan Menon <jmoolayil@gmail.com> - 1.0.0-1
- Initial RPM release optimized for fast container builds
