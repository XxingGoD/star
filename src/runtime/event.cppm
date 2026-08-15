export module star.runtime.event;

import std;
import nlohmann.json;
import star.runtime.result;

export namespace star::runtime {

enum class EventKind {
    progress,
    log,
    data,
    prompt,
    error,
    heartbeat,
    result,
};

auto event_kind_name(EventKind kind) -> std::string_view {
    switch (kind) {
    case EventKind::progress: return "progress";
    case EventKind::log: return "log";
    case EventKind::data: return "data";
    case EventKind::prompt: return "prompt";
    case EventKind::error: return "error";
    case EventKind::heartbeat: return "heartbeat";
    case EventKind::result: return "result";
    }
    return "error";
}

struct Event {
    EventKind kind;
    std::string request_id;
    std::string trace_id;
    std::string source;
    Json payload = Json::object();

    auto json() const -> Json {
        auto value = payload;
        value["kind"] = event_kind_name(kind);
        value["requestId"] = request_id;
        value["traceId"] = trace_id;
        value["source"] = source;
        return value;
    }
};

class EventSink {
public:
    virtual ~EventSink() = default;
    virtual void emit(const Event& event) = 0;
};

class VectorEventSink final : public EventSink {
public:
    void emit(const Event& event) override { events_.push_back(event); }

    auto events() const -> const std::vector<Event>& { return events_; }

private:
    std::vector<Event> events_;
};

class NdjsonEventSink final : public EventSink {
public:
    explicit NdjsonEventSink(std::ostream& output) : output_(output) {}

    void emit(const Event& event) override {
        output_ << event.json().dump() << '\n';
        output_.flush();
    }

private:
    std::ostream& output_;
};

} // namespace star::runtime
