#include "PJarczakLinuxSoBridgeLauncher.hpp"
#include "PJarczakLinuxBridgeConfig.hpp"

#include <windows.h>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>

namespace Slic3r::PJarczakLinuxBridge {

namespace {

std::filesystem::path module_dir()
{
    HMODULE module = nullptr;
    if (!::GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                              reinterpret_cast<LPCWSTR>(&build_default_launch_spec), &module))
        return {};

    std::wstring path(32768, L'\0');
    const DWORD size = ::GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size()));
    if (size == 0)
        return {};
    path.resize(size);
    return std::filesystem::path(path).parent_path();
}

std::string narrow(const std::wstring& s)
{
    if (s.empty())
        return {};
    const int size = ::WideCharToMultiByte(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0, nullptr, nullptr);
    std::string out(size, '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), out.data(), size, nullptr, nullptr);
    return out;
}

std::string to_wsl_path(const std::filesystem::path& p)
{
    const std::wstring ws = p.wstring();
    if (ws.size() >= 2 && ws[1] == L':') {
        std::string out = "/mnt/";
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ws[0]))));
        out.push_back('/');
        for (std::size_t i = 2; i < ws.size(); ++i) {
            const wchar_t ch = ws[i];
            out.push_back(ch == L'\\' ? '/' : static_cast<char>(ch));
        }
        return out;
    }

    std::string out = narrow(ws);
    std::replace(out.begin(), out.end(), '\\', '/');
    return out;
}

}

std::string host_executable_name()
{
    return host_executable_file_name();
}

std::string host_pipe_hint()
{
    return "stdio";
}

LaunchSpec build_default_launch_spec()
{
    const std::filesystem::path plugin_dir = module_dir();
    const std::filesystem::path host_path = plugin_dir / host_executable_file_name();

    const std::string plugin_dir_wsl = to_wsl_path(plugin_dir);
    const std::string host_path_wsl = to_wsl_path(host_path);

    LaunchSpec spec;
    spec.description = "windows via bundled WSL host";
    spec.argv = {"wsl.exe", "--", host_path_wsl};
    spec.env = {
        {"PJARCZAK_BAMBU_PLUGIN_DIR", plugin_dir_wsl},
        {"PJARCZAK_BAMBU_NETWORK_SO", plugin_dir_wsl + "/" + linux_network_library_name()},
        {"PJARCZAK_BAMBU_SOURCE_SO", plugin_dir_wsl + "/" + linux_source_library_name()}
    };
    return spec;
}

}
