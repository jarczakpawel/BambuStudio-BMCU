#include "LinuxPluginHost.hpp"
#include "../../src/slic3r/Utils/PJarczakLinuxBridge/PJarczakLinuxSoBridgeRpcProtocol.hpp"

#include <iostream>
#include <mutex>
#include <string>
#include <thread>

using namespace Slic3r::PJarczakLinuxBridge;

int main()
{
    std::ios::sync_with_stdio(false);
    std::cout.setf(std::ios::unitbuf);

    LinuxPluginHost host;
    std::mutex out_mutex;
    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty())
            continue;
        RpcFrame req;
        std::string err;
        if (!decode_frame(line, req, err)) {
            RpcFrame resp;
            resp.id = 0;
            resp.method = "reply";
            resp.payload = {{"ok", false}, {"error", err}};
            std::lock_guard<std::mutex> lock(out_mutex);
            std::cout << encode_frame(resp);
            continue;
        }

        std::thread([req, &host, &out_mutex]() {
            RpcFrame resp;
            resp.id = req.id;
            resp.method = "reply";
            resp.payload = host.handle(req.method, req.payload);
            std::lock_guard<std::mutex> lock(out_mutex);
            std::cout << encode_frame(resp);
        }).detach();
    }
    return 0;
}
