#include "PJarczakLinuxSoBridgeLauncher.hpp"

#include <cstdlib>

namespace Slic3r::PJarczakLinuxBridge {

namespace {

std::string shell_quote_single(const std::string& s)
{
    std::string out;
    out.reserve(s.size() + 8);
    out.push_back('\'');
    for (char c : s) {
        if (c == '\'')
            out += "'\"'\"'";
        else
            out.push_back(c);
    }
    out.push_back('\'');
    return out;
}

}

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
    spec.description = "windows via WSL";

    const char* plugin_dir = std::getenv("PJARCZAK_BAMBU_PLUGIN_DIR");
    if (plugin_dir && *plugin_dir) {
        const std::string quoted_plugin_dir = shell_quote_single(plugin_dir);
        const std::string cmd =
            "set -e; "
            "plugin_dir=$(wslpath -a " + quoted_plugin_dir + "); "
            "cd \"$plugin_dir\"; "
            "chmod +x ./" + host_executable_name() + " >/dev/null 2>&1 || true; "
            "exec ./" + host_executable_name();
        spec.argv = {"wsl.exe", "--", "bash", "-lc", cmd};
    } else {
        spec.argv = {"wsl.exe", "--", host_executable_name()};
    }

    return spec;
}

}
