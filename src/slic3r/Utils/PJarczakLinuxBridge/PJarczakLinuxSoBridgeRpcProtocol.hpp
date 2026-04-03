#pragma once

#include <string>
#include <nlohmann/json.hpp>

namespace Slic3r::PJarczakLinuxBridge {

struct RpcFrame {
    int id{0};
    std::string method;
    nlohmann::json payload;
};

std::string encode_frame(const RpcFrame& frame);
bool decode_frame(const std::string& line, RpcFrame& frame, std::string& error);

}
