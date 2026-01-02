Name:           iapRemote
Version:        1.0.0
Release:        1%{?dist}
Summary:        Secure IAP Tunneling Client for GCP

License:        Apache-2.0
URL:            https://github.com/jmenon/iapRemote
# Source0 is expected to be a tarball of the project
Source0:        %{name}-%{version}.tar.gz

BuildRequires:  gcc-c++, cmake, make, pkgconfig
BuildRequires:  gtkmm30-devel, vte291-devel, openssl-devel, nlohmann-json-devel
Requires:       gtkmm30, vte291, openssl-libs, freerdp, google-cloud-sdk

%description
A desktop utility to manage and connect to Google Cloud instances
via Identity-Aware Proxy (IAP) tunnels for SSH and RDP.

%prep
%autosetup

%build
%cmake
%cmake_build

%install
%cmake_install
mkdir -p %{buildroot}%{_datadir}/applications
mkdir -p %{buildroot}%{_datadir}/icons/hicolor/scalable/apps
mkdir -p %{buildroot}%{_datadir}/iapRemote

cp packaging/rpm_dist/iapRemote.desktop %{buildroot}%{_datadir}/applications/
cp icon.svg %{buildroot}%{_datadir}/icons/hicolor/scalable/apps/iapRemote.svg
cp styles.css %{buildroot}%{_datadir}/iapRemote/

%files
%{_bindir}/iapRemote
%{_datadir}/applications/iapRemote.desktop
%{_datadir}/icons/hicolor/scalable/apps/iapRemote.svg
%{_datadir}/iapRemote/styles.css

%changelog
* Fri Jan 02 2026 Jayan Menon <jayan.menon@mavenwave.com> - 1.0.0-1
- Initial RPM release
