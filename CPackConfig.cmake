# CPackConfig.cmake

# Basic package metadata
set(CPACK_GENERATOR "NSIS")
set(CPACK_PACKAGE_NAME "BambuStudio")
set(CPACK_PACKAGE_VENDOR "BambuStudio Team")

# You can override CPACK_PACKAGE_VERSION via project/version files.
# Default/fallback version:
if(NOT CPACK_PACKAGE_VERSION)
  set(CPACK_PACKAGE_VERSION "1.0.0")
endif()

# Output file name
set(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}-${CPACK_PACKAGE_VERSION}-win64")

# NSIS specifics
set(CPACK_NSIS_DISPLAY_NAME "Bambu Studio")
# Default install root: Program Files (64-bit)
set(CPACK_NSIS_INSTALL_ROOT "$PROGRAMFILES64")
# Default installation directory name under Program Files
set(CPACK_NSIS_INSTALLER_ICON "${CMAKE_SOURCE_DIR}/resources/BambuStudio.ico")
set(CPACK_NSIS_MUI_ICON "${CMAKE_SOURCE_DIR}/resources/BambuStudio.ico")
set(CPACK_NSIS_MUI_UNIICON "${CMAKE_SOURCE_DIR}/resources/uninstall.ico")

# Create Start Menu and Desktop shortcuts
# CPACK_NSIS_INSTALLED_ICON_NAME should be the executable name as installed (relative to install prefix)
# If your executable is installed to bin/BambuStudio.exe, set accordingly: "bin\\BambuStudio.exe"
set(CPACK_NSIS_INSTALLED_ICON_NAME "BambuStudio.exe")
set(CPACK_NSIS_CREATE_ICONS "ON")
# Create a Start Menu link: format "installed-file;link-name"
set(CPACK_NSIS_MENU_LINKS "BambuStudio.exe;Bambu Studio")

# Reinstall behavior
set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
set(CPACK_NSIS_INSTALLED_PERMISSIONS "all")

# Allow overwriting existing installations
set(CPACK_NSIS_ALLOW_EXIT_CODE "0" )

# Include component support if you use components in install()
# set(CPACK_COMPONENTS_ALL Runtime Libraries Headers)
# set(CPACK_COMPONENT_INSTALL ON)

# Set package description
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Bambu Studio — 3D printing slicer and UI")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE" CACHE PATH "License file" OPTIONAL)
set(CPACK_RESOURCE_FILE_README "${CMAKE_SOURCE_DIR}/README.md" CACHE PATH "Readme file" OPTIONAL)

# Ensure CPack uses the install tree from CMAKE_INSTALL_PREFIX (set during build)
# Optionally set CPACK_PACKAGE_DIRECTORY to control where the installer file is written
# set(CPACK_PACKAGE_DIRECTORY "${CMAKE_BINARY_DIR}")

include(CPack)
