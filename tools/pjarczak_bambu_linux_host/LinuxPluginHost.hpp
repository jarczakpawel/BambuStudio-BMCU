#pragma once

#include <nlohmann/json.hpp>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <string>

namespace Slic3r::PJarczakLinuxBridge {

struct HostJobState {
    std::int64_t job_id{0};
    std::int64_t agent_handle{0};
    std::string kind;
    std::atomic<bool> cancel_requested{false};
    std::mutex wait_mutex;
    std::condition_variable wait_cv;
    std::int64_t wait_request_id{0};
    bool wait_reply_ready{false};
    bool wait_reply_value{true};
};

struct HostCallbackReplyState {
    std::mutex mutex;
    std::condition_variable cv;
    bool ready{false};
    std::string string_value;
};

class LinuxPluginHost {
public:
    LinuxPluginHost();
    nlohmann::json handle(const std::string& method, const nlohmann::json& payload);

private:
    void load_modules();
    void* resolve_network(const char* name);
    void* resolve_source(const char* name);
    nlohmann::json not_supported(const std::string& method) const;
    void queue_event(std::int64_t agent_handle, const std::string& name, const nlohmann::json& payload);
    void queue_tunnel_event(std::int64_t tunnel_handle, const std::string& name, const nlohmann::json& payload);
    nlohmann::json drain_events(std::size_t limit);
    std::shared_ptr<HostJobState> get_job(std::int64_t job_id);
    void register_job(const std::shared_ptr<HostJobState>& job);
    void unregister_job(std::int64_t job_id);
    void set_job_cancel(std::int64_t job_id, bool value);
    void set_job_wait_reply(std::int64_t job_id, std::int64_t request_id, bool value);
    std::shared_ptr<HostCallbackReplyState> register_callback_request(std::int64_t request_id);
    void unregister_callback_request(std::int64_t request_id);
    void set_callback_reply(std::int64_t request_id, const std::string& value);

    template <typename T>
    T net(const char* name)
    {
        return reinterpret_cast<T>(resolve_network(name));
    }

    template <typename T>
    T src(const char* name)
    {
        return reinterpret_cast<T>(resolve_source(name));
    }

    void* m_network{nullptr};
    void* m_source{nullptr};
    std::int64_t m_next_agent{1};
    std::int64_t m_next_tunnel{1};
    std::map<std::int64_t, void*> m_agents;
    std::map<std::int64_t, void*> m_tunnels;
    std::map<std::int64_t, std::string> m_country_codes;
    std::mutex m_state_mutex;
    std::mutex m_events_mutex;
    std::deque<nlohmann::json> m_events;
    std::map<std::int64_t, std::shared_ptr<HostJobState>> m_jobs;
    std::map<std::int64_t, std::shared_ptr<HostCallbackReplyState>> m_callback_replies;
    std::atomic<std::int64_t> m_next_wait_request{1};
    std::atomic<std::int64_t> m_next_callback_request{1};
};

}
