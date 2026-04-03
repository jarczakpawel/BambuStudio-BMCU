#include "PJarczakLinuxSoBridgeRpcClient.hpp"
#include "PJarczakLinuxSoBridgeLauncher.hpp"
#include "PJarczakLinuxSoBridgeRpcProtocol.hpp"

#include <boost/process/environment.hpp>

namespace bp = boost::process;

namespace Slic3r::PJarczakLinuxBridge {

RpcClient& RpcClient::instance()
{
    static RpcClient client;
    return client;
}

RpcClient::~RpcClient()
{
    std::lock_guard<std::mutex> lock(m_state_mutex);
    stop_locked();
}

bool RpcClient::start_locked()
{
    if (m_proc && m_proc->child.running())
        return true;
    try {
        auto spec = build_default_launch_spec();
        if (spec.argv.empty()) {
            m_last_error = "empty launch spec";
            return false;
        }

        bp::environment env = boost::this_process::environment();
        for (const auto& kv : spec.env)
            env[kv.first] = kv.second;

        std::vector<std::string> args;
        for (size_t i = 1; i < spec.argv.size(); ++i)
            args.push_back(spec.argv[i]);

        auto proc = std::make_unique<Proc>();
        proc->child = bp::child(bp::search_path(spec.argv[0]), bp::args(args), bp::std_in < proc->in, bp::std_out > proc->out, env);
        m_proc = std::move(proc);
        m_reader_stop = false;
        if (m_reader.joinable())
            m_reader.join();
        m_reader = std::thread([this] { reader_loop(); });
        m_last_error.clear();
        return true;
    } catch (const std::exception& e) {
        m_last_error = e.what();
        m_proc.reset();
        return false;
    }
}

void RpcClient::stop_locked()
{
    m_reader_stop = true;
    if (m_proc) {
        try {
            if (m_proc->child.running())
                m_proc->child.terminate();
        } catch (...) {}
    }
    if (m_reader.joinable())
        m_reader.join();
    m_proc.reset();
}

bool RpcClient::ensure_started()
{
    std::lock_guard<std::mutex> lock(m_state_mutex);
    return start_locked();
}

bool RpcClient::is_started() const
{
    std::lock_guard<std::mutex> lock(m_state_mutex);
    return m_proc && m_proc->child.running();
}

void RpcClient::reader_loop()
{
    while (!m_reader_stop) {
        std::string line;
        {
            std::lock_guard<std::mutex> lock(m_state_mutex);
            if (!m_proc)
                break;
            if (!std::getline(m_proc->out, line)) {
                m_last_error = "bridge host closed stdout";
                auto pending = m_pending;
                m_pending.clear();
                for (auto& it : pending) {
                    std::lock_guard<std::mutex> plock(it.second->mutex);
                    it.second->payload = {{"ok", false}, {"error", m_last_error}};
                    it.second->ready = true;
                    it.second->cv.notify_all();
                }
                break;
            }
        }

        RpcFrame reply;
        std::string decode_error;
        if (!decode_frame(line, reply, decode_error))
            continue;

        std::shared_ptr<Pending> pending;
        {
            std::lock_guard<std::mutex> lock(m_state_mutex);
            auto it = m_pending.find(reply.id);
            if (it != m_pending.end()) {
                pending = it->second;
                m_pending.erase(it);
            }
        }
        if (!pending)
            continue;
        {
            std::lock_guard<std::mutex> plock(pending->mutex);
            pending->payload = reply.payload;
            pending->ready = true;
        }
        pending->cv.notify_all();
    }
}

nlohmann::json RpcClient::request(const std::string& method, const nlohmann::json& payload)
{
    std::shared_ptr<Pending> pending = std::make_shared<Pending>();
    int id = 0;
    {
        std::lock_guard<std::mutex> lock(m_state_mutex);
        if (!start_locked())
            return {{"ok", false}, {"error", m_last_error}};
        id = m_next_id++;
        m_pending[id] = pending;
    }

    try {
        RpcFrame frame;
        frame.id = id;
        frame.method = method;
        frame.payload = payload;
        {
            std::lock_guard<std::mutex> wlock(m_write_mutex);
            if (!m_proc || !m_proc->child.running())
                throw std::runtime_error("bridge host is not running");
            m_proc->in << encode_frame(frame);
            m_proc->in.flush();
        }
    } catch (const std::exception& e) {
        std::lock_guard<std::mutex> lock(m_state_mutex);
        m_pending.erase(id);
        m_last_error = e.what();
        return {{"ok", false}, {"error", m_last_error}};
    }

    std::unique_lock<std::mutex> plock(pending->mutex);
    pending->cv.wait(plock, [&] { return pending->ready; });
    return pending->payload;
}

int RpcClient::invoke_int(const std::string& method, const nlohmann::json& payload)
{
    const auto j = request(method, payload);
    if (j.contains("ret"))
        return j.value("ret", -1);
    return j.value("value", -1);
}

bool RpcClient::invoke_bool(const std::string& method, const nlohmann::json& payload)
{
    const auto j = request(method, payload);
    return j.value("value", false);
}

std::string RpcClient::invoke_string(const std::string& method, const nlohmann::json& payload)
{
    const auto j = request(method, payload);
    return j.value("value", std::string());
}

nlohmann::json RpcClient::invoke_json(const std::string& method, const nlohmann::json& payload)
{
    return request(method, payload);
}

void RpcClient::invoke_void(const std::string& method, const nlohmann::json& payload)
{
    (void) request(method, payload);
}

std::string RpcClient::last_error() const
{
    std::lock_guard<std::mutex> lock(m_state_mutex);
    return m_last_error;
}

}
