#include "PJarczakLinuxSoBridgeLauncher.hpp"

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
    spec.description = "linux direct host";
    spec.argv = {host_executable_name()};
    return spec;
}

}
