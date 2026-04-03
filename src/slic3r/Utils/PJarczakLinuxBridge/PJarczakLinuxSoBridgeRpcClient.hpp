#pragma once

#include <nlohmann/json.hpp>
#include <boost/process.hpp>
#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace Slic3r::PJarczakLinuxBridge {

class RpcClient {
public:
    static RpcClient& instance();

    bool ensure_started();
    bool is_started() const;
    int invoke_int(const std::string& method, const nlohmann::json& payload = {});
    bool invoke_bool(const std::string& method, const nlohmann::json& payload = {});
    std::string invoke_string(const std::string& method, const nlohmann::json& payload = {});
    nlohmann::json invoke_json(const std::string& method, const nlohmann::json& payload = {});
    void invoke_void(const std::string& method, const nlohmann::json& payload = {});
    std::string last_error() const;

private:
    RpcClient() = default;
    ~RpcClient();
    RpcClient(const RpcClient&) = delete;
    RpcClient& operator=(const RpcClient&) = delete;

    struct Proc {
        boost::process::opstream in;
        boost::process::ipstream out;
        boost::process::child child;
    };

    struct Pending {
        std::mutex mutex;
        std::condition_variable cv;
        bool ready{false};
        nlohmann::json payload;
    };

    nlohmann::json request(const std::string& method, const nlohmann::json& payload);
    bool start_locked();
    void stop_locked();
    void reader_loop();

    mutable std::mutex m_state_mutex;
    std::mutex m_write_mutex;
    std::unique_ptr<Proc> m_proc;
    std::thread m_reader;
    int m_next_id{1};
    std::string m_last_error;
    std::map<int, std::shared_ptr<Pending>> m_pending;
    bool m_reader_stop{false};
};

}
