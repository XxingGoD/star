import std;
import star.capability.dispatcher;
import star.runtime.context;
import star.runtime.event;
import star.runtime.result;

namespace {

void require(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

auto terminal_count(const star::runtime::VectorEventSink& sink) -> std::size_t {
    return std::ranges::count_if(sink.events(), [](const auto& event) {
        return event.kind == star::runtime::EventKind::result;
    });
}

auto context(star::runtime::EventSink& sink) -> star::runtime::ExecutionContext {
    return {
        .request_id = "request-1",
        .trace_id = "trace-1",
        .source = "test",
        .permissions = {"demo.read"},
        .events = &sink,
    };
}

} // namespace

int main() {
    star::capability::Registry registry;
    auto added = registry.add({
        .name = "demo.echo",
        .description = "echo input",
        .required_permissions = {"demo.read"},
        .handler = [](const auto&, const auto& args) {
            return star::runtime::Result<star::runtime::Json>{args};
        },
    });
    require(added.has_value(), "register echo capability");

    star::capability::Dispatcher dispatcher(registry);
    star::runtime::VectorEventSink success_events;
    const auto success_context = context(success_events);
    const auto success = dispatcher.dispatch(
        "demo.echo", star::runtime::Json{{"value", 7}}, success_context);
    require(success == 0, "successful dispatch exit code");
    require(terminal_count(success_events) == 1, "one success result event");
    require(success_events.events().back().payload["data"]["value"] == 7,
            "handler result preserved");

    star::runtime::VectorEventSink denied_events;
    auto denied_context = context(denied_events);
    denied_context.permissions.clear();
    const auto denied = dispatcher.dispatch(
        "demo.echo", star::runtime::Json::object(), denied_context);
    require(denied == 1, "permission failure exit code");
    require(denied_events.events().size() == 2, "error followed by result");
    require(denied_events.events().front().payload["code"] == "E_PERMISSION",
            "stable permission error code");
    require(terminal_count(denied_events) == 1, "one failure result event");

    star::runtime::VectorEventSink missing_events;
    const auto missing = dispatcher.dispatch(
        "demo.missing", star::runtime::Json::object(), context(missing_events));
    require(missing == 1, "missing capability exit code");
    require(missing_events.events().front().payload["code"] == "E_NOT_FOUND",
            "stable not found error code");

    auto cycle_context = context(success_events);
    cycle_context.call_chain.push_back("demo.echo");
    const auto cycle = dispatcher.invoke(
        "demo.echo", star::runtime::Json::object(), cycle_context);
    require(!cycle && cycle.error().code == star::runtime::ErrorCode::call_cycle,
            "cycle detection");
}
