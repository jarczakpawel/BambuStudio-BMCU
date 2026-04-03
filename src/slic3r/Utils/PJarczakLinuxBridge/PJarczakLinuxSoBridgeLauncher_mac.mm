#include "PJarczakLinuxSoBridgeLauncher.hpp"

#include <cstdlib>

namespace Slic3r::PJarczakLinuxBridge {

std::string host_executable_name()
{
    return "pjarczak_bambu_linux_host";
}

std::string host_pipe_hint()
{
    return "stdio";
}

LaunchSpec build_default_launch_spec()
{
    LaunchSpec spec;
    spec.description = "mac via linux wrapper";

    const char* wrapper = std::getenv("PJARCZAK_BAMBU_HOST_WRAPPER");
    const char* plugin_dir = std::getenv("PJARCZAK_BAMBU_PLUGIN_DIR");

    std::string wrapper_cmd = (wrapper && *wrapper) ? wrapper : "pjarczak-bambu-linux-host-wrapper";
    std::string host_path = (plugin_dir && *plugin_dir)
        ? std::string(plugin_dir) + "/" + host_executable_name()
        : host_executable_name();

    spec.argv = {wrapper_cmd, host_path};
    return spec;
}

}
