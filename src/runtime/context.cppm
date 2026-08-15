export module star.runtime.context;

import std;
import star.runtime.event;
import star.runtime.result;

export namespace star::runtime {

struct ExecutionContext {
    std::string request_id;
    std::string trace_id;
    std::string source = "star";
    std::vector<std::string> call_chain;
    std::unordered_set<std::string> permissions;
    std::optional<std::string> field;
    std::optional<std::string> box;
    std::filesystem::path workspace_host;
    std::filesystem::path workspace_box;
    std::shared_ptr<std::atomic_bool> cancelled =
        std::make_shared<std::atomic_bool>(false);
    EventSink* events = nullptr;
    std::size_t max_call_depth = 32;

    auto child(std::string child_source) const -> ExecutionContext {
        auto next = *this;
        next.source = std::move(child_source);
        next.call_chain.push_back(next.source);
        return next;
    }

    auto is_cancelled() const -> bool {
        return cancelled && cancelled->load(std::memory_order_relaxed);
    }

    void emit(EventKind kind, Json payload = Json::object()) const {
        if (!events) return;
        events->emit(Event{
            .kind = kind,
            .request_id = request_id,
            .trace_id = trace_id,
            .source = source,
            .payload = std::move(payload),
        });
    }
};

} // namespace star::runtime
