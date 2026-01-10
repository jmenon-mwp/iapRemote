# GitHub Actions Workflows

This directory contains GitHub Actions workflows for automated package building.

## Workflows

### 1. Build Release Packages (`build-packages.yml`)

**Trigger**: Automatically runs when a new tag is pushed (e.g., `v1.0.0`, `v2.1.3`)

**What it does**:
- Builds packages for three distributions:
  - Debian (using Debian 11 base)
  - Ubuntu (using Ubuntu 20.04 base)
  - Rocky Linux (using Rocky 9 base)
- Uses the tag version (with 'v' prefix) as the package version
- Creates a GitHub Release with all built packages attached
- Runs the same container-based build process as local builds

**Usage**:
```bash
# Create and push a new tag
git tag v1.0.0
git push origin v1.0.0

# The workflow will automatically:
# 1. Build all packages
# 2. Create a GitHub Release
# 3. Upload all packages to the release
```

### 2. Manual Package Build (`manual-build.yml`)

**Trigger**: Manual workflow dispatch (can be run from GitHub Actions UI)

**What it does**:
- Allows building packages on-demand without creating a tag
- Provides flexibility to:
  - Specify a custom version number
  - Build for specific OS (all, debian, ubuntu, or rocky)
- Uploads packages as workflow artifacts

**Usage**:
1. Go to the "Actions" tab in your GitHub repository
2. Select "Manual Package Build" from the workflows list
3. Click "Run workflow"
4. Fill in the parameters:
   - **Version**: The version number to use (e.g., `v1.0.0-beta`)
   - **Target OS**: Choose which OS to build for
5. Click "Run workflow"
6. Download artifacts from the workflow run page

## Build Process

Both workflows use the existing `packaging/build_in_container.sh` script, which:
1. Pulls the appropriate base container image (Debian, Ubuntu, or Rocky Linux)
2. Mounts the source code and vcpkg cache
3. Runs the distribution-specific build script inside the container
4. Outputs packages to the `output/` directory

## Version Handling

### For Tag-Based Builds
The version is automatically extracted from the git tag and keeps the 'v' prefix:
- Tag `v1.0.0` → Package version `v1.0.0`
- Tag `v2.1.3-beta` → Package version `v2.1.3-beta`

### For Manual Builds
You specify the version directly in the workflow dispatch form.

## Package Naming

Packages are named according to the distribution (without specific version numbers):
- **Debian**: `iapremote_<version>_debian_amd64.deb`
  - Example: `iapremote_v1.0.0_debian_amd64.deb`
- **Ubuntu**: `iapremote_<version>_ubuntu_amd64.deb`
  - Example: `iapremote_v1.0.0_ubuntu_amd64.deb`
- **Rocky Linux**: `iapRemote-<version>-1.rhel.x86_64.rpm`
  - Example: `iapRemote-v1.0.0-1.rhel.x86_64.rpm`

## Artifacts

### Release Workflow
All packages are automatically attached to the GitHub Release created for the tag.

### Manual Workflow
Packages are uploaded as workflow artifacts and can be downloaded from the workflow run page. They are retained for 90 days by default.

## Troubleshooting

### Build Failures
If a build fails:
1. Check the workflow logs in the Actions tab
2. Look for errors in the specific build step
3. You can reproduce the issue locally using:
   ```bash
   bash packaging/build_in_container.sh <os> <version> <pkg_version>
   # Examples:
   bash packaging/build_in_container.sh debian 11 v1.0.0
   bash packaging/build_in_container.sh ubuntu 20.04 v1.0.0
   bash packaging/build_in_container.sh rocky 9 v1.0.0
   ```

### Missing Packages
If packages aren't appearing in the release or artifacts:
- Check that the `output/` directory contains the expected files
- Verify the file patterns in the upload steps match your package names

### Version Issues
If the version isn't being set correctly:
- For tag builds: Ensure your tag starts with 'v' (e.g., `v1.0.0`)
- For manual builds: Double-check the version input parameter

## Customization

### Changing Build Distributions
To build for different distribution versions:

1. Edit the workflow files (`.github/workflows/build-packages.yml` and `manual-build.yml`)
2. Change the version numbers in the build commands:
   ```yaml
   # For Debian
   bash packaging/build_in_container.sh debian 12 ${{ steps.get_version.outputs.version }}

   # For Ubuntu
   bash packaging/build_in_container.sh ubuntu 22.04 ${{ steps.get_version.outputs.version }}

   # For Rocky
   bash packaging/build_in_container.sh rocky 8 ${{ steps.get_version.outputs.version }}
   ```

### Changing Build Options
The workflows use the existing build scripts. To modify build options:
- Edit the distribution-specific scripts in `packaging/<distro>_dist/create_*.sh`
- Changes will automatically be picked up by the workflows

## Requirements

### Repository Permissions
The workflows require:
- **Read** access to repository contents
- **Write** access to create releases (for `build-packages.yml`)

### Secrets
No additional secrets are required. The workflows use the automatically provided `GITHUB_TOKEN`.

### Runner Requirements
- Uses `ubuntu-latest` GitHub-hosted runners
- Requires Docker support (provided by default)
- Sufficient disk space for vcpkg and build artifacts (~10GB)

## Distribution Compatibility

The packages are built on specific base distributions but are designed to be compatible with related versions:

- **Debian package**: Built on Debian 11, compatible with Debian 11+ and derivatives
- **Ubuntu package**: Built on Ubuntu 20.04, compatible with Ubuntu 20.04+ and derivatives
- **Rocky package**: Built on Rocky 9, compatible with RHEL 9, Rocky 9, AlmaLinux 9, and similar
