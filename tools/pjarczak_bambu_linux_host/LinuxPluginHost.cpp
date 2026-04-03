#include "LinuxPluginHost.hpp"

#include "../../shared/pjarczak_linux_plugin_bridge_core/BridgeCoreJson.hpp"
#include "../../src/slic3r/Utils/bambu_networking.hpp"
#include "../../src/slic3r/GUI/Printer/BambuTunnel.h"
#include "../../src/slic3r/Utils/PJarczakLinuxBridge/PJarczakLinuxBridgeCompat.hpp"
#include "../../src/slic3r/Utils/PJarczakLinuxBridge/PJarczakLinuxBridgeConfig.hpp"

#include <dlfcn.h>
#include <cstdlib>
#include <cstring>
#include <chrono>

using namespace std::chrono_literals;

namespace Slic3r::PJarczakLinuxBridge {

namespace {

std::string env_or(const char* name, const char* fallback)
{
    if (const char* v = std::getenv(name))
        return v;
    return fallback;
}


std::map<std::string, std::string> json_to_string_map(const nlohmann::json& j)
{
    std::map<std::string, std::string> out;
    if (!j.is_object())
        return out;
    for (auto it = j.begin(); it != j.end(); ++it) {
        if (it.value().is_string())
            out[it.key()] = it.value().get<std::string>();
        else
            out[it.key()] = it.value().dump();
    }
    return out;
}

std::map<std::string, std::map<std::string, std::string>> json_to_nested_string_map(const nlohmann::json& j)
{
    std::map<std::string, std::map<std::string, std::string>> out;
    if (!j.is_object())
        return out;
    for (auto it = j.begin(); it != j.end(); ++it)
        out[it.key()] = json_to_string_map(it.value());
    return out;
}

nlohmann::json nested_string_map_to_json(const std::map<std::string, std::map<std::string, std::string>>& value)
{
    nlohmann::json out = nlohmann::json::object();
    for (const auto& [k, inner] : value)
        out[k] = inner;
    return out;
}

BBL::TaskQueryParams task_query_from_json(const nlohmann::json& j)
{
    BBL::TaskQueryParams p{};
    p.dev_id = j.value("dev_id", std::string());
    p.status = j.value("status", 0);
    p.offset = j.value("offset", 0);
    p.limit = j.value("limit", 20);
    return p;
}

template <typename Invoke>
nlohmann::json wait_string_callback(Invoke&& invoke)
{
    std::mutex m;
    std::condition_variable cv;
    bool ready = false;
    std::string result;
    const int ret = invoke([&](std::string value) {
        {
            std::lock_guard<std::mutex> lock(m);
            result = std::move(value);
            ready = true;
        }
        cv.notify_one();
    });
    if (ret != 0)
        return {{"ok", true}, {"value", ret}};
    std::unique_lock<std::mutex> lock(m);
    if (!cv.wait_for(lock, 30s, [&] { return ready; }))
        return {{"ok", true}, {"value", BBL::BAMBU_NETWORK_ERR_TIMEOUT}};
    return {{"ok", true}, {"value", 0}, {"result", result}};
}

template <typename Invoke>
nlohmann::json wait_string_int_callback(Invoke&& invoke)
{
    std::mutex m;
    std::condition_variable cv;
    bool ready = false;
    std::string result;
    int status = 0;
    const int ret = invoke([&](std::string value, int st) {
        {
            std::lock_guard<std::mutex> lock(m);
            result = std::move(value);
            status = st;
            ready = true;
        }
        cv.notify_one();
    });
    if (ret != 0)
        return {{"ok", true}, {"value", ret}};
    std::unique_lock<std::mutex> lock(m);
    if (!cv.wait_for(lock, 30s, [&] { return ready; }))
        return {{"ok", true}, {"value", BBL::BAMBU_NETWORK_ERR_TIMEOUT}};
    return {{"ok", true}, {"value", 0}, {"result", result}, {"status", status}};
}

template <typename Invoke>
nlohmann::json wait_model_task_callback(Invoke&& invoke)
{
    std::mutex m;
    std::condition_variable cv;
    bool ready = false;
    nlohmann::json subtask = nlohmann::json::object();
    const int ret = invoke([&](Slic3r::BBLModelTask* value) {
        {
            std::lock_guard<std::mutex> lock(m);
            subtask = model_task_to_json(value);
            ready = true;
        }
        cv.notify_one();
    });
    if (ret != 0)
        return {{"ok", true}, {"value", ret}};
    std::unique_lock<std::mutex> lock(m);
    if (!cv.wait_for(lock, 30s, [&] { return ready; }))
        return {{"ok", true}, {"value", BBL::BAMBU_NETWORK_ERR_TIMEOUT}};
    return {{"ok", true}, {"value", 0}, {"subtask", subtask}};
}

}

LinuxPluginHost::LinuxPluginHost()
{
    load_modules();
}

void LinuxPluginHost::load_modules()
{
    const boost::filesystem::path plugin_folder = boost::filesystem::path(env_or("PJARCZAK_BAMBU_PLUGIN_DIR", "."));
    std::string manifest_reason;
    const bool have_manifest = boost::filesystem::exists(linux_payload_manifest_path(plugin_folder));
    const bool manifest_ok = !have_manifest || validate_linux_payload_set_against_manifest(plugin_folder, &manifest_reason);

    if (!m_network) {
        const auto path = env_or("PJARCZAK_BAMBU_NETWORK_SO", "libbambu_networking.so");
        std::string reason;
        if (manifest_ok && validate_linux_payload_file(path, &reason))
            m_network = dlopen(path.c_str(), RTLD_LAZY);
        if (m_network) {
            using get_version_fn = std::string (*)();
            auto gv = reinterpret_cast<get_version_fn>(dlsym(m_network, "bambu_network_get_version"));
            if (gv) {
                std::string abi_reason;
                const auto actual = gv();
                if (!abi_version_matches_expected(actual, &abi_reason)) {
                    dlclose(m_network);
                    m_network = nullptr;
                }
            }
        }
    }
    if (!m_source) {
        const auto path = env_or("PJARCZAK_BAMBU_SOURCE_SO", "libBambuSource.so");
        std::string reason;
        if (manifest_ok && validate_linux_payload_file(path, &reason))
            m_source = dlopen(path.c_str(), RTLD_LAZY);
    }
}

void* LinuxPluginHost::resolve_network(const char* name)
{
    load_modules();
    return m_network ? dlsym(m_network, name) : nullptr;
}

void* LinuxPluginHost::resolve_source(const char* name)
{
    load_modules();
    return m_source ? dlsym(m_source, name) : nullptr;
}

nlohmann::json LinuxPluginHost::not_supported(const std::string& method) const
{
    return {{"ok", false}, {"error", method + " unsupported in host"}};
}

void LinuxPluginHost::queue_event(std::int64_t agent_handle, const std::string& name, const nlohmann::json& payload)
{
    std::lock_guard<std::mutex> lock(m_events_mutex);
    m_events.push_back({{"agent", agent_handle}, {"name", name}, {"payload", payload}});
}

void LinuxPluginHost::queue_tunnel_event(std::int64_t tunnel_handle, const std::string& name, const nlohmann::json& payload)
{
    std::lock_guard<std::mutex> lock(m_events_mutex);
    m_events.push_back({{"tunnel", tunnel_handle}, {"name", name}, {"payload", payload}});
}

nlohmann::json LinuxPluginHost::drain_events(std::size_t limit)
{
    std::lock_guard<std::mutex> lock(m_events_mutex);
    nlohmann::json arr = nlohmann::json::array();
    while (!m_events.empty() && arr.size() < limit) {
        arr.push_back(m_events.front());
        m_events.pop_front();
    }
    return {{"ok", true}, {"events", arr}};
}

std::shared_ptr<HostJobState> LinuxPluginHost::get_job(std::int64_t job_id)
{
    std::lock_guard<std::mutex> lock(m_state_mutex);
    auto it = m_jobs.find(job_id);
    return it == m_jobs.end() ? nullptr : it->second;
}

void LinuxPluginHost::register_job(const std::shared_ptr<HostJobState>& job)
{
    if (!job)
        return;
    std::lock_guard<std::mutex> lock(m_state_mutex);
    m_jobs[job->job_id] = job;
}

void LinuxPluginHost::unregister_job(std::int64_t job_id)
{
    std::lock_guard<std::mutex> lock(m_state_mutex);
    m_jobs.erase(job_id);
}

void LinuxPluginHost::set_job_cancel(std::int64_t job_id, bool value)
{
    auto job = get_job(job_id);
    if (job)
        job->cancel_requested = value;
}

void LinuxPluginHost::set_job_wait_reply(std::int64_t job_id, std::int64_t request_id, bool value)
{
    auto job = get_job(job_id);
    if (!job)
        return;
    {
        std::lock_guard<std::mutex> lock(job->wait_mutex);
        job->wait_request_id = request_id;
        job->wait_reply_value = value;
        job->wait_reply_ready = true;
    }
    job->wait_cv.notify_all();
}

std::shared_ptr<HostCallbackReplyState> LinuxPluginHost::register_callback_request(std::int64_t request_id)
{
    auto state = std::make_shared<HostCallbackReplyState>();
    std::lock_guard<std::mutex> lock(m_state_mutex);
    m_callback_replies[request_id] = state;
    return state;
}

void LinuxPluginHost::unregister_callback_request(std::int64_t request_id)
{
    std::lock_guard<std::mutex> lock(m_state_mutex);
    m_callback_replies.erase(request_id);
}

void LinuxPluginHost::set_callback_reply(std::int64_t request_id, const std::string& value)
{
    std::shared_ptr<HostCallbackReplyState> state;
    {
        std::lock_guard<std::mutex> lock(m_state_mutex);
        auto it = m_callback_replies.find(request_id);
        if (it == m_callback_replies.end())
            return;
        state = it->second;
    }
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        state->string_value = value;
        state->ready = true;
    }
    state->cv.notify_all();
}

nlohmann::json LinuxPluginHost::handle(const std::string& method, const nlohmann::json& payload)
{
    using namespace BBL;

    if (method == "bridge.poll_events")
        return drain_events(payload.value("limit", 64U));

    if (method == "bridge.ping")
        return {{"ok", true}, {"value", "pong"}};

    if (method == "bridge.job_cancel") {
        set_job_cancel(payload.value("job_id", 0LL), payload.value("cancel", true));
        return {{"ok", true}, {"value", 0}};
    }
    if (method == "bridge.job_wait_reply") {
        set_job_wait_reply(payload.value("job_id", 0LL), payload.value("request_id", 0LL), payload.value("reply", true));
        return {{"ok", true}, {"value", 0}};
    }
    if (method == "bridge.callback_reply") {
        set_callback_reply(payload.value("request_id", 0LL), payload.value("value", std::string()));
        return {{"ok", true}, {"value", 0}};
    }

    if (method == "net.create_agent") {
        auto f = net<void* (*)(std::string)>("bambu_network_create_agent");
        if (!f) return not_supported(method);
        void* raw = f(payload.value("log_dir", std::string()));
        const auto id = m_next_agent++;
        {
            std::lock_guard<std::mutex> lock(m_state_mutex);
            m_agents[id] = raw;
            m_country_codes[id] = payload.value("country_code", std::string());
        }
        return {{"ok", true}, {"value", id}};
    }
    if (method == "net.destroy_agent") {
        auto f = net<int (*)(void*)>("bambu_network_destroy_agent");
        if (!f) return not_supported(method);
        const auto id = payload.value("agent", 0LL);
        void* raw = nullptr;
        {
            std::lock_guard<std::mutex> lock(m_state_mutex);
            auto it = m_agents.find(id);
            if (it == m_agents.end()) return {{"ok", false}, {"error", "agent not found"}};
            raw = it->second;
            m_agents.erase(it);
            m_country_codes.erase(id);
        }
        const int ret = f(raw);
        return {{"ok", true}, {"value", ret}};
    }

    const auto lookup_agent = [&]() -> void* {
        const auto id = payload.value("agent", 0LL);
        std::lock_guard<std::mutex> lock(m_state_mutex);
        auto it = m_agents.find(id);
        if (it == m_agents.end())
            return nullptr;
        return it->second;
    };

    const auto agent_id = payload.value("agent", 0LL);

    const auto lookup_tunnel = [&]() -> Bambu_Tunnel {
        const auto id = payload.value("tunnel", 0LL);
        std::lock_guard<std::mutex> lock(m_state_mutex);
        auto it = m_tunnels.find(id);
        return it == m_tunnels.end() ? nullptr : static_cast<Bambu_Tunnel>(it->second);
    };

    if (method == "net.set_config_dir") {
        auto f = net<int (*)(void*, std::string)>("bambu_network_set_config_dir");
        auto a = lookup_agent();
        return f && a ? nlohmann::json{{"ok", true}, {"value", f(a, payload.value("config_dir", std::string()))}} : not_supported(method);
    }
    if (method == "net.set_cert_file") {
        auto f = net<int (*)(void*, std::string, std::string)>("bambu_network_set_cert_file");
        auto a = lookup_agent();
        return f && a ? nlohmann::json{{"ok", true}, {"value", f(a, payload.value("folder", std::string()), payload.value("filename", std::string()))}} : not_supported(method);
    }
    if (method == "net.set_country_code") {
        auto f = net<int (*)(void*, std::string)>("bambu_network_set_country_code");
        auto a = lookup_agent();
        const auto code = payload.value("country_code", std::string());
        { std::lock_guard<std::mutex> lock(m_state_mutex); m_country_codes[agent_id] = code; }
        return f && a ? nlohmann::json{{"ok", true}, {"value", f(a, code)}} : not_supported(method);
    }
    if (method == "net.init_log") { auto f = net<int (*)(void*)>("bambu_network_init_log"); auto a = lookup_agent(); return f && a ? nlohmann::json{{"ok", true}, {"value", f(a)}} : not_supported(method); }
    if (method == "net.start") { auto f = net<int (*)(void*)>("bambu_network_start"); auto a = lookup_agent(); return f && a ? nlohmann::json{{"ok", true}, {"value", f(a)}} : not_supported(method); }
    if (method == "net.connect_server") { auto f = net<int (*)(void*)>("bambu_network_connect_server"); auto a = lookup_agent(); return f && a ? nlohmann::json{{"ok", true}, {"value", f(a)}} : not_supported(method); }
    if (method == "net.is_server_connected") { auto f = net<bool (*)(void*)>("bambu_network_is_server_connected"); auto a = lookup_agent(); return f && a ? nlohmann::json{{"ok", true}, {"value", f(a)}} : not_supported(method); }
    if (method == "net.refresh_connection") { auto f = net<int (*)(void*)>("bambu_network_refresh_connection"); auto a = lookup_agent(); return f && a ? nlohmann::json{{"ok", true}, {"value", f(a)}} : not_supported(method); }
    if (method == "net.start_subscribe") { auto f = net<int (*)(void*, std::string)>("bambu_network_start_subscribe"); auto a = lookup_agent(); return f && a ? nlohmann::json{{"ok", true}, {"value", f(a, payload.value("module", std::string()))}} : not_supported(method); }
    if (method == "net.stop_subscribe") { auto f = net<int (*)(void*, std::string)>("bambu_network_stop_subscribe"); auto a = lookup_agent(); return f && a ? nlohmann::json{{"ok", true}, {"value", f(a, payload.value("module", std::string()))}} : not_supported(method); }
    if (method == "net.add_subscribe") { auto f = net<int (*)(void*, std::vector<std::string>)>("bambu_network_add_subscribe"); auto a = lookup_agent(); return f && a ? nlohmann::json{{"ok", true}, {"value", f(a, payload.value("devs", std::vector<std::string>()))}} : not_supported(method); }
    if (method == "net.del_subscribe") { auto f = net<int (*)(void*, std::vector<std::string>)>("bambu_network_del_subscribe"); auto a = lookup_agent(); return f && a ? nlohmann::json{{"ok", true}, {"value", f(a, payload.value("devs", std::vector<std::string>()))}} : not_supported(method); }
    if (method == "net.enable_multi_machine") { auto f = net<void (*)(void*, bool)>("bambu_network_enable_multi_machine"); auto a = lookup_agent(); if (!f || !a) return not_supported(method); f(a, payload.value("enable", false)); return {{"ok", true}, {"value", 0}}; }
    if (method == "net.send_message") { auto f = net<int (*)(void*, std::string, std::string, int, int)>("bambu_network_send_message"); auto a = lookup_agent(); return f && a ? nlohmann::json{{"ok", true}, {"value", f(a, payload.value("dev_id", std::string()), payload.value("msg", std::string()), payload.value("qos", 0), payload.value("flag", 0))}} : not_supported(method); }
    if (method == "net.connect_printer") { auto f = net<int (*)(void*, std::string, std::string, std::string, std::string, bool)>("bambu_network_connect_printer"); auto a = lookup_agent(); return f && a ? nlohmann::json{{"ok", true}, {"value", f(a, payload.value("dev_id", std::string()), payload.value("dev_ip", std::string()), payload.value("username", std::string()), payload.value("password", std::string()), payload.value("use_ssl", false))}} : not_supported(method); }
    if (method == "net.disconnect_printer") { auto f = net<int (*)(void*)>("bambu_network_disconnect_printer"); auto a = lookup_agent(); return f && a ? nlohmann::json{{"ok", true}, {"value", f(a)}} : not_supported(method); }
    if (method == "net.send_message_to_printer") { auto f = net<int (*)(void*, std::string, std::string, int, int)>("bambu_network_send_message_to_printer"); auto a = lookup_agent(); return f && a ? nlohmann::json{{"ok", true}, {"value", f(a, payload.value("dev_id", std::string()), payload.value("msg", std::string()), payload.value("qos", 0), payload.value("flag", 0))}} : not_supported(method); }
    if (method == "net.update_cert") { auto f = net<int (*)(void*)>("bambu_network_update_cert"); auto a = lookup_agent(); return f && a ? nlohmann::json{{"ok", true}, {"value", f(a)}} : not_supported(method); }
    if (method == "net.install_device_cert") { auto f = net<void (*)(void*, std::string, bool)>("bambu_network_install_device_cert"); auto a = lookup_agent(); if (!f || !a) return not_supported(method); f(a, payload.value("dev_id", std::string()), payload.value("lan_only", false)); return {{"ok", true}, {"value", 0}}; }
    if (method == "net.start_discovery") { auto f = net<bool (*)(void*, bool, bool)>("bambu_network_start_discovery"); auto a = lookup_agent(); return f && a ? nlohmann::json{{"ok", true}, {"value", f(a, payload.value("start", false), payload.value("sending", false))}} : not_supported(method); }

    if (method == "net.set_on_ssdp_msg_fn") {
        auto f = net<int (*)(void*, OnMsgArrivedFn)>("bambu_network_set_on_ssdp_msg_fn");
        auto a = lookup_agent();
        if (!f || !a) return not_supported(method);
        return {{"ok", true}, {"value", f(a, [this, agent_id](std::string dev_info_json_str) { queue_event(agent_id, "on_ssdp_msg", {{"dev_info_json_str", dev_info_json_str}}); })}};
    }
    if (method == "net.set_on_user_login_fn") {
        auto f = net<int (*)(void*, OnUserLoginFn)>("bambu_network_set_on_user_login_fn");
        auto a = lookup_agent();
        if (!f || !a) return not_supported(method);
        return {{"ok", true}, {"value", f(a, [this, agent_id](int online_login, bool login) { queue_event(agent_id, "on_user_login", {{"online_login", online_login}, {"login", login}}); })}};
    }
    if (method == "net.set_on_printer_connected_fn") {
        auto f = net<int (*)(void*, OnPrinterConnectedFn)>("bambu_network_set_on_printer_connected_fn");
        auto a = lookup_agent();
        if (!f || !a) return not_supported(method);
        return {{"ok", true}, {"value", f(a, [this, agent_id](std::string topic_str) { queue_event(agent_id, "on_printer_connected", {{"topic_str", topic_str}}); })}};
    }
    if (method == "net.set_on_server_connected_fn") {
        auto f = net<int (*)(void*, OnServerConnectedFn)>("bambu_network_set_on_server_connected_fn");
        auto a = lookup_agent();
        if (!f || !a) return not_supported(method);
        return {{"ok", true}, {"value", f(a, [this, agent_id](int return_code, int reason_code) { queue_event(agent_id, "on_server_connected", {{"return_code", return_code}, {"reason_code", reason_code}}); })}};
    }
    if (method == "net.set_on_http_error_fn") {
        auto f = net<int (*)(void*, OnHttpErrorFn)>("bambu_network_set_on_http_error_fn");
        auto a = lookup_agent();
        if (!f || !a) return not_supported(method);
        return {{"ok", true}, {"value", f(a, [this, agent_id](unsigned http_code, std::string http_body) { queue_event(agent_id, "on_http_error", {{"http_code", http_code}, {"http_body", http_body}}); })}};
    }
    if (method == "net.set_get_country_code_fn") {
        auto f = net<int (*)(void*, GetCountryCodeFn)>("bambu_network_set_get_country_code_fn");
        auto a = lookup_agent();
        if (!f || !a) return not_supported(method);
        return {{"ok", true}, {"value", f(a, [this, agent_id]() {
            const auto request_id = m_next_callback_request.fetch_add(1);
            auto state = register_callback_request(request_id);
            queue_event(agent_id, "callback.get_country_code", {{"request_id", request_id}});
            std::unique_lock<std::mutex> lock(state->mutex);
            if (!state->cv.wait_for(lock, 30s, [&] { return state->ready; })) {
                unregister_callback_request(request_id);
                std::lock_guard<std::mutex> s_lock(m_state_mutex);
                auto it = m_country_codes.find(agent_id);
                return it == m_country_codes.end() ? std::string() : it->second;
            }
            const auto value = state->string_value;
            lock.unlock();
            unregister_callback_request(request_id);
            return value;
        })}};
    }
    if (method == "net.set_on_subscribe_failure_fn") {
        auto f = net<int (*)(void*, GetSubscribeFailureFn)>("bambu_network_set_on_subscribe_failure_fn");
        auto a = lookup_agent();
        if (!f || !a) return not_supported(method);
        return {{"ok", true}, {"value", f(a, [this, agent_id](std::string topic) { queue_event(agent_id, "on_subscribe_failure", {{"topic", topic}}); })}};
    }
    if (method == "net.set_on_message_fn") {
        auto f = net<int (*)(void*, OnMessageFn)>("bambu_network_set_on_message_fn");
        auto a = lookup_agent();
        if (!f || !a) return not_supported(method);
        return {{"ok", true}, {"value", f(a, [this, agent_id](std::string dev_id, std::string msg) { queue_event(agent_id, "on_message", {{"dev_id", dev_id}, {"msg", msg}}); })}};
    }
    if (method == "net.set_on_user_message_fn") {
        auto f = net<int (*)(void*, OnMessageFn)>("bambu_network_set_on_user_message_fn");
        auto a = lookup_agent();
        if (!f || !a) return not_supported(method);
        return {{"ok", true}, {"value", f(a, [this, agent_id](std::string dev_id, std::string msg) { queue_event(agent_id, "on_user_message", {{"dev_id", dev_id}, {"msg", msg}}); })}};
    }
    if (method == "net.set_on_local_connect_fn") {
        auto f = net<int (*)(void*, OnLocalConnectedFn)>("bambu_network_set_on_local_connect_fn");
        auto a = lookup_agent();
        if (!f || !a) return not_supported(method);
        return {{"ok", true}, {"value", f(a, [this, agent_id](int status, std::string dev_id, std::string msg) { queue_event(agent_id, "on_local_connect", {{"status", status}, {"dev_id", dev_id}, {"msg", msg}}); })}};
    }
    if (method == "net.set_on_local_message_fn") {
        auto f = net<int (*)(void*, OnMessageFn)>("bambu_network_set_on_local_message_fn");
        auto a = lookup_agent();
        if (!f || !a) return not_supported(method);
        return {{"ok", true}, {"value", f(a, [this, agent_id](std::string dev_id, std::string msg) { queue_event(agent_id, "on_local_message", {{"dev_id", dev_id}, {"msg", msg}}); })}};
    }
    if (method == "net.set_queue_on_main_fn") {
        auto f = net<int (*)(void*, QueueOnMainFn)>("bambu_network_set_queue_on_main_fn");
        auto a = lookup_agent();
        if (!f || !a) return not_supported(method);
        return {{"ok", true}, {"value", f(a, [](std::function<void()> fn) { fn(); })}};
    }
    if (method == "net.set_server_callback") {
        auto f = net<int (*)(void*, OnServerErrFn)>("bambu_network_set_server_callback");
        auto a = lookup_agent();
        if (!f || !a) return not_supported(method);
        return {{"ok", true}, {"value", f(a, [this, agent_id](std::string url, int status) { queue_event(agent_id, "on_server_error", {{"url", url}, {"status", status}}); })}};
    }

    if (method == "net.change_user") {
        auto f = net<int (*)(void*, std::string)>("bambu_network_change_user");
        auto a = lookup_agent();
        if (!f || !a) return not_supported(method);
        const int ret = f(a, payload.value("user_info", std::string()));
        nlohmann::json r{{"ok", true}, {"value", ret}};
        auto g1 = net<bool (*)(void*)>("bambu_network_is_user_login"); if (g1) r["logged_in"] = g1(a);
        auto g2 = net<std::string (*)(void*)>("bambu_network_get_user_id"); if (g2) r["user_id"] = g2(a);
        auto g3 = net<std::string (*)(void*)>("bambu_network_get_user_name"); if (g3) r["user_name"] = g3(a);
        auto g4 = net<std::string (*)(void*)>("bambu_network_get_user_avatar"); if (g4) r["user_avatar"] = g4(a);
        auto g5 = net<std::string (*)(void*)>("bambu_network_get_user_nickanme"); if (g5) r["user_nickname"] = g5(a);
        auto g6 = net<std::string (*)(void*)>("bambu_network_build_login_cmd"); if (g6) r["login_cmd"] = g6(a);
        auto g7 = net<std::string (*)(void*)>("bambu_network_build_logout_cmd"); if (g7) r["logout_cmd"] = g7(a);
        auto g8 = net<std::string (*)(void*)>("bambu_network_build_login_info"); if (g8) r["login_info"] = g8(a);
        auto g9 = net<std::string (*)(void*)>("bambu_network_get_bambulab_host"); if (g9) r["bambulab_host"] = g9(a);
        return r;
    }

    if (method == "net.is_user_login") { auto f = net<bool (*)(void*)>("bambu_network_is_user_login"); auto a = lookup_agent(); return f && a ? nlohmann::json{{"ok", true}, {"value", f(a)}} : not_supported(method); }
    if (method == "net.user_logout") { auto f = net<int (*)(void*, bool)>("bambu_network_user_logout"); auto a = lookup_agent(); return f && a ? nlohmann::json{{"ok", true}, {"value", f(a, payload.value("request", false))}} : not_supported(method); }
    if (method == "net.get_user_id") { auto f = net<std::string (*)(void*)>("bambu_network_get_user_id"); auto a = lookup_agent(); return f && a ? nlohmann::json{{"ok", true}, {"value", f(a)}} : not_supported(method); }
    if (method == "net.get_user_name") { auto f = net<std::string (*)(void*)>("bambu_network_get_user_name"); auto a = lookup_agent(); return f && a ? nlohmann::json{{"ok", true}, {"value", f(a)}} : not_supported(method); }
    if (method == "net.get_user_avatar") { auto f = net<std::string (*)(void*)>("bambu_network_get_user_avatar"); auto a = lookup_agent(); return f && a ? nlohmann::json{{"ok", true}, {"value", f(a)}} : not_supported(method); }
    if (method == "net.get_user_nickname") { auto f = net<std::string (*)(void*)>("bambu_network_get_user_nickanme"); auto a = lookup_agent(); return f && a ? nlohmann::json{{"ok", true}, {"value", f(a)}} : not_supported(method); }
    if (method == "net.build_login_cmd") { auto f = net<std::string (*)(void*)>("bambu_network_build_login_cmd"); auto a = lookup_agent(); return f && a ? nlohmann::json{{"ok", true}, {"value", f(a)}} : not_supported(method); }
    if (method == "net.build_logout_cmd") { auto f = net<std::string (*)(void*)>("bambu_network_build_logout_cmd"); auto a = lookup_agent(); return f && a ? nlohmann::json{{"ok", true}, {"value", f(a)}} : not_supported(method); }
    if (method == "net.build_login_info") { auto f = net<std::string (*)(void*)>("bambu_network_build_login_info"); auto a = lookup_agent(); return f && a ? nlohmann::json{{"ok", true}, {"value", f(a)}} : not_supported(method); }
    if (method == "net.ping_bind") { auto f = net<int (*)(void*, std::string)>("bambu_network_ping_bind"); auto a = lookup_agent(); return f && a ? nlohmann::json{{"ok", true}, {"value", f(a, payload.value("ping_code", std::string()))}} : not_supported(method); }
    if (method == "net.bind_detect") { auto f = net<int (*)(void*, std::string, std::string, detectResult&)>("bambu_network_bind_detect"); auto a = lookup_agent(); if (!f || !a) return not_supported(method); detectResult det; const int ret = f(a, payload.value("dev_ip", std::string()), payload.value("sec_link", std::string()), det); return {{"ok", true}, {"value", ret}, {"detect", {{"result_msg", det.result_msg}, {"command", det.command}, {"dev_id", det.dev_id}, {"model_id", det.model_id}, {"dev_name", det.dev_name}, {"version", det.version}, {"bind_state", det.bind_state}, {"connect_type", det.connect_type}}}}; }
    if (method == "net.report_consent") { auto f = net<int (*)(void*, std::string)>("bambu_network_report_consent"); auto a = lookup_agent(); return f && a ? nlohmann::json{{"ok", true}, {"value", f(a, payload.value("expand", std::string()))}} : not_supported(method); }
    if (method == "net.bind") {
        auto f = net<int (*)(void*, std::string, std::string, std::string, std::string, bool, OnUpdateStatusFn)>("bambu_network_bind");
        auto a = lookup_agent();
        if (!f || !a) return not_supported(method);
        const auto job_id = payload.value("client_job_id", 0LL);
        const auto params = payload.value("params", nlohmann::json::object());
        auto job = std::make_shared<HostJobState>();
        job->job_id = job_id;
        job->agent_handle = agent_id;
        job->kind = "bind";
        register_job(job);
        const int ret = f(a, params.value("dev_ip", std::string()), params.value("dev_id", std::string()), params.value("sec_link", std::string()), params.value("timezone", std::string()), params.value("improved", false), [this, job](int status, int code, std::string msg) {
            queue_event(job->agent_handle, "job.update_status", {{"job_id", job->job_id}, {"kind", job->kind}, {"status", status}, {"code", code}, {"msg", msg}});
        });
        unregister_job(job_id);
        return {{"ok", true}, {"value", ret}, {"job_id", job_id}};
    }
    if (method == "net.unbind") { auto f = net<int (*)(void*, std::string)>("bambu_network_unbind"); auto a = lookup_agent(); return f && a ? nlohmann::json{{"ok", true}, {"value", f(a, payload.value("dev_id", std::string()))}} : not_supported(method); }
    if (method == "net.get_bambulab_host") { auto f = net<std::string (*)(void*)>("bambu_network_get_bambulab_host"); auto a = lookup_agent(); return f && a ? nlohmann::json{{"ok", true}, {"value", f(a)}} : not_supported(method); }
    if (method == "net.get_user_selected_machine") { auto f = net<std::string (*)(void*)>("bambu_network_get_user_selected_machine"); auto a = lookup_agent(); return f && a ? nlohmann::json{{"ok", true}, {"value", f(a)}} : not_supported(method); }
    if (method == "net.set_user_selected_machine") { auto f = net<int (*)(void*, std::string)>("bambu_network_set_user_selected_machine"); auto a = lookup_agent(); return f && a ? nlohmann::json{{"ok", true}, {"value", f(a, payload.value("dev_id", std::string()))}} : not_supported(method); }
    if (method == "net.get_studio_info_url") { auto f = net<std::string (*)(void*)>("bambu_network_get_studio_info_url"); auto a = lookup_agent(); return f && a ? nlohmann::json{{"ok", true}, {"value", f(a)}} : not_supported(method); }
    if (method == "net.modify_printer_name") { auto f = net<int (*)(void*, std::string, std::string)>("bambu_network_modify_printer_name"); auto a = lookup_agent(); return f && a ? nlohmann::json{{"ok", true}, {"value", f(a, payload.value("dev_id", std::string()), payload.value("dev_name", std::string()))}} : not_supported(method); }
    if (method == "net.get_task_plate_index") { auto f = net<int (*)(void*, std::string, int*)>("bambu_network_get_task_plate_index"); auto a = lookup_agent(); if (!f || !a) return not_supported(method); int plate_index = -1; const int ret = f(a, payload.value("task_id", std::string()), &plate_index); return {{"ok", true}, {"value", ret}, {"plate_index", plate_index}}; }
    if (method == "net.get_user_info") { auto f = net<int (*)(void*, int*)>("bambu_network_get_user_info"); auto a = lookup_agent(); if (!f || !a) return not_supported(method); int identifier = 0; const int ret = f(a, &identifier); return {{"ok", true}, {"value", ret}, {"identifier", identifier}}; }
    if (method == "net.request_bind_ticket") { auto f = net<int (*)(void*, std::string*)>("bambu_network_request_bind_ticket"); auto a = lookup_agent(); if (!f || !a) return not_supported(method); std::string ticket; const int ret = f(a, &ticket); return {{"ok", true}, {"value", ret}, {"ticket", ticket}}; }
    if (method == "net.query_bind_status") { auto f = net<int (*)(void*, std::vector<std::string>, unsigned int*, std::string*)>("bambu_network_query_bind_status"); auto a = lookup_agent(); if (!f || !a) return not_supported(method); unsigned int http_code = 0; std::string http_body; const int ret = f(a, payload.value("query_list", std::vector<std::string>()), &http_code, &http_body); return {{"ok", true}, {"value", ret}, {"http_code", http_code}, {"http_body", http_body}}; }
    if (method == "net.get_printer_firmware") { auto f = net<int (*)(void*, std::string, unsigned*, std::string*)>("bambu_network_get_printer_firmware"); auto a = lookup_agent(); if (!f || !a) return not_supported(method); unsigned http_code = 0; std::string http_body; const int ret = f(a, payload.value("dev_id", std::string()), &http_code, &http_body); return {{"ok", true}, {"value", ret}, {"http_code", http_code}, {"http_body", http_body}}; }
    if (method == "net.get_my_profile") { auto f = net<int (*)(void*, std::string, unsigned int*, std::string*)>("bambu_network_get_my_profile"); auto a = lookup_agent(); if (!f || !a) return not_supported(method); unsigned int http_code = 0; std::string http_body; const int ret = f(a, payload.value("token", std::string()), &http_code, &http_body); return {{"ok", true}, {"value", ret}, {"http_code", http_code}, {"http_body", http_body}}; }
    if (method == "net.request_setting_id") { auto f = net<std::string (*)(void*, std::string, std::map<std::string, std::string>*, unsigned int*)>("bambu_network_request_setting_id"); auto a = lookup_agent(); if (!f || !a) return not_supported(method); auto values = json_to_string_map(payload.value("values", nlohmann::json::object())); unsigned int http_code = 0; std::string setting_id = f(a, payload.value("name", std::string()), &values, &http_code); return {{"ok", true}, {"value", 0}, {"setting_id", setting_id}, {"http_code", http_code}}; }
    if (method == "net.get_user_presets") { auto f = net<int (*)(void*, std::map<std::string, std::map<std::string, std::string>>*)>("bambu_network_get_user_presets"); auto a = lookup_agent(); if (!f || !a) return not_supported(method); std::map<std::string, std::map<std::string, std::string>> user_presets; const int ret = f(a, &user_presets); return {{"ok", true}, {"value", ret}, {"user_presets", nested_string_map_to_json(user_presets)}}; }
    if (method == "net.get_setting_list") { auto f = net<int (*)(void*, std::string, ProgressFn, WasCancelledFn)>("bambu_network_get_setting_list"); auto a = lookup_agent(); if (!f || !a) return not_supported(method); const auto job_id = payload.value("client_job_id", 0LL); const auto params = payload.value("params", nlohmann::json::object()); auto job = std::make_shared<HostJobState>(); job->job_id = job_id; job->agent_handle = agent_id; job->kind = "get_setting_list"; register_job(job); const int ret = f(a, params.value("bundle_version", std::string()), [this, job](int progress) { queue_event(job->agent_handle, "job.progress", {{"job_id", job->job_id}, {"kind", job->kind}, {"progress", progress}}); }, [job]() { return job->cancel_requested.load(); }); unregister_job(job_id); return {{"ok", true}, {"value", ret}, {"job_id", job_id}}; }
    if (method == "net.get_setting_list2") { auto f = net<int (*)(void*, std::string, CheckFn, ProgressFn, WasCancelledFn)>("bambu_network_get_setting_list2"); auto a = lookup_agent(); if (!f || !a) return not_supported(method); const auto job_id = payload.value("client_job_id", 0LL); const auto params = payload.value("params", nlohmann::json::object()); auto job = std::make_shared<HostJobState>(); job->job_id = job_id; job->agent_handle = agent_id; job->kind = "get_setting_list2"; register_job(job); const int ret = f(a, params.value("bundle_version", std::string()), [this, job](std::map<std::string, std::string> info) { const auto request_id = m_next_wait_request.fetch_add(1); { std::lock_guard<std::mutex> lock(job->wait_mutex); job->wait_request_id = request_id; job->wait_reply_ready = false; job->wait_reply_value = true; } queue_event(job->agent_handle, "job.check", {{"job_id", job->job_id}, {"kind", job->kind}, {"request_id", request_id}, {"info", info}}); std::unique_lock<std::mutex> lock(job->wait_mutex); job->wait_cv.wait(lock, [&] { return job->wait_reply_ready && job->wait_request_id == request_id; }); return job->wait_reply_value; }, [this, job](int progress) { queue_event(job->agent_handle, "job.progress", {{"job_id", job->job_id}, {"kind", job->kind}, {"progress", progress}}); }, [job]() { return job->cancel_requested.load(); }); unregister_job(job_id); return {{"ok", true}, {"value", ret}, {"job_id", job_id}}; }
    if (method == "net.put_setting") { auto f = net<int (*)(void*, std::string, std::string, std::map<std::string, std::string>*, unsigned int*)>("bambu_network_put_setting"); auto a = lookup_agent(); if (!f || !a) return not_supported(method); auto values = json_to_string_map(payload.value("values", nlohmann::json::object())); unsigned int http_code = 0; const int ret = f(a, payload.value("setting_id", std::string()), payload.value("name", std::string()), &values, &http_code); return {{"ok", true}, {"value", ret}, {"http_code", http_code}}; }
    if (method == "net.delete_setting") { auto f = net<int (*)(void*, std::string)>("bambu_network_delete_setting"); auto a = lookup_agent(); return f && a ? nlohmann::json{{"ok", true}, {"value", f(a, payload.value("setting_id", std::string()))}} : not_supported(method); }
    if (method == "net.set_extra_http_header") { auto f = net<int (*)(void*, std::map<std::string, std::string>)>("bambu_network_set_extra_http_header"); auto a = lookup_agent(); if (!f || !a) return not_supported(method); auto headers = json_to_string_map(payload.value("headers", nlohmann::json::object())); return {{"ok", true}, {"value", f(a, headers)}}; }
    if (method == "net.get_my_message") { auto f = net<int (*)(void*, int, int, int, unsigned int*, std::string*)>("bambu_network_get_my_message"); auto a = lookup_agent(); if (!f || !a) return not_supported(method); unsigned int http_code = 0; std::string http_body; const int ret = f(a, payload.value("type", 0), payload.value("after", 0), payload.value("limit", 20), &http_code, &http_body); return {{"ok", true}, {"value", ret}, {"http_code", http_code}, {"http_body", http_body}}; }
    if (method == "net.check_user_task_report") { auto f = net<int (*)(void*, int*, bool*)>("bambu_network_check_user_task_report"); auto a = lookup_agent(); if (!f || !a) return not_supported(method); int task_id = 0; bool printable = false; const int ret = f(a, &task_id, &printable); return {{"ok", true}, {"value", ret}, {"task_id", task_id}, {"printable", printable}}; }
    if (method == "net.get_user_print_info") { auto f = net<int (*)(void*, unsigned int*, std::string*)>("bambu_network_get_user_print_info"); auto a = lookup_agent(); if (!f || !a) return not_supported(method); unsigned int http_code = 0; std::string http_body; const int ret = f(a, &http_code, &http_body); return {{"ok", true}, {"value", ret}, {"http_code", http_code}, {"http_body", http_body}}; }
    if (method == "net.get_user_tasks") { auto f = net<int (*)(void*, TaskQueryParams, std::string*)>("bambu_network_get_user_tasks"); auto a = lookup_agent(); if (!f || !a) return not_supported(method); auto params = task_query_from_json(payload.value("params", nlohmann::json::object())); std::string http_body; const int ret = f(a, params, &http_body); return {{"ok", true}, {"value", ret}, {"http_body", http_body}}; }
    if (method == "net.get_subtask_info") { auto f = net<int (*)(void*, std::string, std::string*, unsigned int*, std::string*)>("bambu_network_get_subtask_info"); auto a = lookup_agent(); if (!f || !a) return not_supported(method); std::string task_json; unsigned int http_code = 0; std::string http_body; const int ret = f(a, payload.value("subtask_id", std::string()), &task_json, &http_code, &http_body); return {{"ok", true}, {"value", ret}, {"task_json", task_json}, {"http_code", http_code}, {"http_body", http_body}}; }
    if (method == "net.get_slice_info") { auto f = net<int (*)(void*, std::string, std::string, int, std::string*)>("bambu_network_get_slice_info"); auto a = lookup_agent(); if (!f || !a) return not_supported(method); std::string slice_json; const int ret = f(a, payload.value("project_id", std::string()), payload.value("profile_id", std::string()), payload.value("plate_index", 0), &slice_json); return {{"ok", true}, {"value", ret}, {"slice_json", slice_json}}; }
    if (method == "net.get_camera_url") { auto f = net<int (*)(void*, std::string, std::function<void(std::string)>)>("bambu_network_get_camera_url"); auto a = lookup_agent(); if (!f || !a) return not_supported(method); return wait_string_callback([&](auto cb) { return f(a, payload.value("dev_id", std::string()), cb); }); }
    if (method == "net.get_camera_url_for_golive") { auto f = net<int (*)(void*, std::string, std::string, std::function<void(std::string)>)>("bambu_network_get_camera_url_for_golive"); auto a = lookup_agent(); if (!f || !a) return not_supported(method); return wait_string_callback([&](auto cb) { return f(a, payload.value("dev_id", std::string()), payload.value("sdev_id", std::string()), cb); }); }
    if (method == "net.get_design_staffpick") { auto f = net<int (*)(void*, int, int, std::function<void(std::string)>)>("bambu_network_get_design_staffpick"); auto a = lookup_agent(); if (!f || !a) return not_supported(method); return wait_string_callback([&](auto cb) { return f(a, payload.value("offset", 0), payload.value("limit", 0), cb); }); }
    if (method == "net.get_model_publish_url") { auto f = net<int (*)(void*, std::string*)>("bambu_network_get_model_publish_url"); auto a = lookup_agent(); if (!f || !a) return not_supported(method); std::string url; const int ret = f(a, &url); return {{"ok", true}, {"value", ret}, {"url", url}}; }
    if (method == "net.get_model_mall_home_url") { auto f = net<int (*)(void*, std::string*)>("bambu_network_get_model_mall_home_url"); auto a = lookup_agent(); if (!f || !a) return not_supported(method); std::string url; const int ret = f(a, &url); return {{"ok", true}, {"value", ret}, {"url", url}}; }
    if (method == "net.get_model_mall_detail_url") { auto f = net<int (*)(void*, std::string*, std::string)>("bambu_network_get_model_mall_detail_url"); auto a = lookup_agent(); if (!f || !a) return not_supported(method); std::string url; const int ret = f(a, &url, payload.value("id", std::string())); return {{"ok", true}, {"value", ret}, {"url", url}}; }
    if (method == "net.get_subtask") { auto f = net<int (*)(void*, Slic3r::BBLModelTask*, OnGetSubTaskFn)>("bambu_network_get_subtask"); auto a = lookup_agent(); if (!f || !a) return not_supported(method); Slic3r::BBLModelTask task{}; if (payload.contains("task") && payload["task"].is_object()) json_to_model_task(payload["task"], task); return wait_model_task_callback([&](auto cb) { return f(a, &task, cb); }); }
    if (method == "net.put_model_mall_rating") { auto f = net<int (*)(void*, int, int, std::string, std::vector<std::string>, unsigned int&, std::string&)>("bambu_network_put_model_mall_rating"); auto a = lookup_agent(); if (!f || !a) return not_supported(method); unsigned int http_code = 0; std::string http_error; const int ret = f(a, payload.value("rating_id", 0), payload.value("score", 0), payload.value("content", std::string()), payload.value("images", std::vector<std::string>()), http_code, http_error); return {{"ok", true}, {"value", ret}, {"http_code", http_code}, {"http_error", http_error}}; }
    if (method == "net.get_oss_config") { auto f = net<int (*)(void*, std::string&, std::string, unsigned int&, std::string&)>("bambu_network_get_oss_config"); auto a = lookup_agent(); if (!f || !a) return not_supported(method); std::string config; unsigned int http_code = 0; std::string http_error; const int ret = f(a, config, payload.value("country_code", std::string()), http_code, http_error); return {{"ok", true}, {"value", ret}, {"config", config}, {"http_code", http_code}, {"http_error", http_error}}; }
    if (method == "net.put_rating_picture_oss") { auto f = net<int (*)(void*, std::string&, std::string&, std::string, int, unsigned int&, std::string&)>("bambu_network_put_rating_picture_oss"); auto a = lookup_agent(); if (!f || !a) return not_supported(method); std::string config = payload.value("config", std::string()); std::string pic_oss_path = payload.value("pic_oss_path", std::string()); unsigned int http_code = 0; std::string http_error; const int ret = f(a, config, pic_oss_path, payload.value("model_id", std::string()), payload.value("profile_id", 0), http_code, http_error); return {{"ok", true}, {"value", ret}, {"config", config}, {"pic_oss_path", pic_oss_path}, {"http_code", http_code}, {"http_error", http_error}}; }
    if (method == "net.get_model_mall_rating") { auto f = net<int (*)(void*, int, std::string&, unsigned int&, std::string&)>("bambu_network_get_model_mall_rating"); auto a = lookup_agent(); if (!f || !a) return not_supported(method); std::string rating_result; unsigned int http_code = 0; std::string http_error; const int ret = f(a, payload.value("job_id", 0), rating_result, http_code, http_error); return {{"ok", true}, {"value", ret}, {"rating_result", rating_result}, {"http_code", http_code}, {"http_error", http_error}}; }
    if (method == "net.get_mw_user_preference") { auto f = net<int (*)(void*, std::function<void(std::string)>)>("bambu_network_get_mw_user_preference"); auto a = lookup_agent(); if (!f || !a) return not_supported(method); return wait_string_callback([&](auto cb) { return f(a, cb); }); }
    if (method == "net.get_mw_user_4ulist") { auto f = net<int (*)(void*, int, int, std::function<void(std::string)>)>("bambu_network_get_mw_user_4ulist"); auto a = lookup_agent(); if (!f || !a) return not_supported(method); return wait_string_callback([&](auto cb) { return f(a, payload.value("seed", 0), payload.value("limit", 0), cb); }); }
    if (method == "net.get_hms_snapshot") { auto f = net<int (*)(void*, std::string, std::string, std::function<void(std::string, int)>)>("bambu_network_get_hms_snapshot"); auto a = lookup_agent(); if (!f || !a) return not_supported(method); return wait_string_int_callback([&](auto cb) { return f(a, payload.value("dev_id", std::string()), payload.value("file_name", std::string()), cb); }); }

    if (method == "net.get_my_token") { auto f = net<int (*)(void*, std::string, unsigned int*, std::string*)>("bambu_network_get_my_token"); auto a = lookup_agent(); if (!f || !a) return not_supported(method); unsigned int http_code = 0; std::string http_body; const int ret = f(a, payload.value("ticket", std::string()), &http_code, &http_body); return {{"ok", true}, {"value", ret}, {"http_code", http_code}, {"http_body", http_body}}; }

    if (method == "net.track_enable") { auto f = net<int (*)(void*, bool)>("bambu_network_track_enable"); auto a = lookup_agent(); return f && a ? nlohmann::json{{"ok", true}, {"value", f(a, payload.value("enable", false))}} : not_supported(method); }
    if (method == "net.track_remove_files") { auto f = net<int (*)(void*)>("bambu_network_track_remove_files"); auto a = lookup_agent(); return f && a ? nlohmann::json{{"ok", true}, {"value", f(a)}} : not_supported(method); }
    if (method == "net.track_event") { auto f = net<int (*)(void*, std::string, std::string)>("bambu_network_track_event"); auto a = lookup_agent(); return f && a ? nlohmann::json{{"ok", true}, {"value", f(a, payload.value("evt_key", std::string()), payload.value("content", std::string()))}} : not_supported(method); }
    if (method == "net.track_header") { auto f = net<int (*)(void*, std::string)>("bambu_network_track_header"); auto a = lookup_agent(); return f && a ? nlohmann::json{{"ok", true}, {"value", f(a, payload.value("header", std::string()))}} : not_supported(method); }
    if (method == "net.track_update_property") { auto f = net<int (*)(void*, std::string, std::string, std::string)>("bambu_network_track_update_property"); auto a = lookup_agent(); return f && a ? nlohmann::json{{"ok", true}, {"value", f(a, payload.value("name", std::string()), payload.value("value", std::string()), payload.value("type", std::string()))}} : not_supported(method); }
    if (method == "net.track_get_property") { auto f = net<int (*)(void*, std::string, std::string&, std::string)>("bambu_network_track_get_property"); auto a = lookup_agent(); if (!f || !a) return not_supported(method); std::string value; const int ret = f(a, payload.value("name", std::string()), value, payload.value("type", std::string())); return {{"ok", true}, {"value", ret}, {"property_value", value}}; }

    if (method == "src.init") { auto f = src<int (*)()>("Bambu_Init"); return f ? nlohmann::json{{"ok", true}, {"value", f()}} : nlohmann::json{{"ok", true}, {"value", 0}}; }
    if (method == "src.deinit") { auto f = src<void (*)()>("Bambu_Deinit"); if (f) f(); return {{"ok", true}, {"value", 0}}; }
    if (method == "src.get_last_error_msg") { auto f = src<const char* (*)()>("Bambu_GetLastErrorMsg"); return f ? nlohmann::json{{"ok", true}, {"message", std::string(f() ? f() : "")}} : nlohmann::json{{"ok", true}, {"message", std::string()}}; }
    if (method == "src.free_log_msg") { return {{"ok", true}, {"value", 0}}; }
    if (method == "src.create") {
        auto f = src<int (*)(Bambu_Tunnel*, const char*)>("Bambu_Create");
        if (!f) return not_supported(method);
        Bambu_Tunnel tunnel = nullptr;
        const int ret = f(&tunnel, payload.value("path", std::string()).c_str());
        if (ret != 0)
            return {{"ok", true}, {"value", ret}, {"tunnel", 0}};
        const auto id = m_next_tunnel++;
        { std::lock_guard<std::mutex> lock(m_state_mutex); m_tunnels[id] = tunnel; }
        return {{"ok", true}, {"value", ret}, {"tunnel", id}};
    }
    if (method == "src.open") { auto f = src<int (*)(Bambu_Tunnel)>("Bambu_Open"); auto t = lookup_tunnel(); return f && t ? nlohmann::json{{"ok", true}, {"value", f(t)}} : not_supported(method); }
    if (method == "src.start_stream") { auto f = src<int (*)(Bambu_Tunnel, bool)>("Bambu_StartStream"); auto t = lookup_tunnel(); return f && t ? nlohmann::json{{"ok", true}, {"value", f(t, payload.value("video", false))}} : not_supported(method); }
    if (method == "src.start_stream_ex") { auto f = src<int (*)(Bambu_Tunnel, int)>("Bambu_StartStreamEx"); auto t = lookup_tunnel(); return f && t ? nlohmann::json{{"ok", true}, {"value", f(t, payload.value("type", 0))}} : not_supported(method); }
    if (method == "src.get_stream_count") { auto f = src<int (*)(Bambu_Tunnel)>("Bambu_GetStreamCount"); auto t = lookup_tunnel(); return f && t ? nlohmann::json{{"ok", true}, {"value", f(t)}} : not_supported(method); }
    if (method == "src.get_stream_info") { auto f = src<int (*)(Bambu_Tunnel, int, Bambu_StreamInfo*)>("Bambu_GetStreamInfo"); auto t = lookup_tunnel(); if (!f || !t) return not_supported(method); Bambu_StreamInfo info{}; const int ret = f(t, payload.value("index", 0), &info); nlohmann::json out{{"ok", true}, {"value", ret}}; if (ret == 0) { nlohmann::json ji{{"type", info.type}, {"sub_type", info.sub_type}, {"format_type", info.format_type}, {"format_size", info.format_size}, {"max_frame_size", info.max_frame_size}, {"format_buffer", info.format_buffer && info.format_size > 0 ? std::string(reinterpret_cast<const char*>(info.format_buffer), info.format_size) : std::string()}}; if (info.type == VIDE) ji.update({{"width", info.format.video.width}, {"height", info.format.video.height}, {"frame_rate", info.format.video.frame_rate}}); else ji.update({{"sample_rate", info.format.audio.sample_rate}, {"channel_count", info.format.audio.channel_count}, {"sample_size", info.format.audio.sample_size}}); out["info"] = ji; } return out; }
    if (method == "src.get_duration") { auto f = src<unsigned long (*)(Bambu_Tunnel)>("Bambu_GetDuration"); auto t = lookup_tunnel(); return f && t ? nlohmann::json{{"ok", true}, {"value", f(t)}} : not_supported(method); }
    if (method == "src.seek") { auto f = src<int (*)(Bambu_Tunnel, unsigned long)>("Bambu_Seek"); auto t = lookup_tunnel(); return f && t ? nlohmann::json{{"ok", true}, {"value", f(t, payload.value("time", 0UL))}} : not_supported(method); }
    if (method == "src.send_message") { auto f = src<int (*)(Bambu_Tunnel, int, const char*, int)>("Bambu_SendMessage"); auto t = lookup_tunnel(); const auto data = payload.value("data", std::string()); return f && t ? nlohmann::json{{"ok", true}, {"value", f(t, payload.value("ctrl", 0), data.c_str(), static_cast<int>(data.size()))}} : not_supported(method); }
    if (method == "src.recv_message") { auto f = src<int (*)(Bambu_Tunnel, int*, char*, int*)>("Bambu_RecvMessage"); auto t = lookup_tunnel(); if (!f || !t) return not_supported(method); int ctrl = 0; int len = payload.value("buffer_size", 65536); std::string buffer(static_cast<size_t>(len), '\0'); const int ret = f(t, &ctrl, buffer.data(), &len); nlohmann::json out{{"ok", true}, {"value", ret}, {"ctrl", ctrl}}; if (ret == 0 && len >= 0) out["data"] = buffer.substr(0, static_cast<size_t>(len)); else out["required_len"] = len; return out; }
    if (method == "src.read_sample") {
        auto f = src<int (*)(Bambu_Tunnel, Bambu_Sample*)>("Bambu_ReadSample");
        auto t = lookup_tunnel();
        if (!f || !t) return not_supported(method);
        Bambu_Sample sample{};
        const int ret = f(t, &sample);
        nlohmann::json j{{"ok", true}, {"value", ret}};
        if (ret == 0 && sample.buffer && sample.size > 0) {
            j["sample"] = {{"itrack", sample.itrack}, {"size", sample.size}, {"flags", sample.flags}, {"decode_time", sample.decode_time}, {"buffer", std::string(reinterpret_cast<const char*>(sample.buffer), sample.size)}};
        }
        return j;
    }
    if (method == "src.close") { auto f = src<void (*)(Bambu_Tunnel)>("Bambu_Close"); auto t = lookup_tunnel(); if (!f || !t) return not_supported(method); f(t); return {{"ok", true}, {"value", 0}}; }
    if (method == "src.destroy") { auto f = src<void (*)(Bambu_Tunnel)>("Bambu_Destroy"); const auto id = payload.value("tunnel", 0LL); Bambu_Tunnel t = nullptr; { std::lock_guard<std::mutex> lock(m_state_mutex); auto it = m_tunnels.find(id); if (it != m_tunnels.end()) { t = static_cast<Bambu_Tunnel>(it->second); m_tunnels.erase(it); } } if (!f || !t) return not_supported(method); f(t); return {{"ok", true}, {"value", 0}}; }
    if (method == "src.set_logger") { auto f = src<void (*)(Bambu_Tunnel, Logger, void*)>("Bambu_SetLogger"); auto free_f = src<void (*)(tchar const*)>("Bambu_FreeLogMsg"); auto t = lookup_tunnel(); const auto tunnel_id = payload.value("tunnel", 0LL); if (!f || !t) return not_supported(method); f(t, [this, tunnel_id, free_f](void*, int level, tchar const* msg) { const std::string copied = std::string(msg ? msg : ""); queue_tunnel_event(tunnel_id, "logger", {{"level", level}, {"message", copied}}); if (free_f && msg) free_f(msg); }, nullptr); return {{"ok", true}, {"value", 0}}; }

    return not_supported(method);
}

}
