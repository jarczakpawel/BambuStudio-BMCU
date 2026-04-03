#include "PJarczakLinuxSoBridgeRpcProtocol.hpp"

namespace Slic3r::PJarczakLinuxBridge {

std::string encode_frame(const RpcFrame& frame)
{
    nlohmann::json j;
    j["id"] = frame.id;
    j["method"] = frame.method;
    j["payload"] = frame.payload;
    return j.dump() + "\n";
}

bool decode_frame(const std::string& line, RpcFrame& frame, std::string& error)
{
    try {
        const auto j = nlohmann::json::parse(line);
        frame.id = j.value("id", 0);
        frame.method = j.value("method", std::string());
        frame.payload = j.contains("payload") ? j["payload"] : nlohmann::json::object();
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
}

}
