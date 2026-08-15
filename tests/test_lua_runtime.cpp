import std;
import star.capability.dispatcher;
import star.extension.manifest;
import star.lua.runtime;
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

class TempDirectory {
public:
    TempDirectory() {
        path_ = std::filesystem::temp_directory_path() /
                ("star-lua-" + std::to_string(
                    std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(path_);
    }
    ~TempDirectory() { std::filesystem::remove_all(path_); }
    auto path() const -> const std::filesystem::path& { return path_; }

private:
    std::filesystem::path path_;
};

void write(const std::filesystem::path& path, std::string_view content) {
    std::ofstream output(path);
    output << content;
}

} // namespace

int main() {
    star::capability::Registry registry;
    auto added = registry.add({
        .name = "demo.upper",
        .description = "uppercase text",
        .required_permissions = {"tool.invoke"},
        .handler = [](const auto&, const auto& args) {
            auto text = args.at("text").template get<std::string>();
            std::ranges::transform(text, text.begin(), [](unsigned char ch) {
                return static_cast<char>(std::toupper(ch));
            });
            return star::runtime::Result<star::runtime::Json>{
                star::runtime::Json{{"text", text}}};
        },
    });
    require(added.has_value(), "register child capability");
    star::capability::Dispatcher dispatcher(registry);
    star::lua_runtime::Runtime runtime(dispatcher);

    TempDirectory temporary;
    const auto entry = temporary.path() / "main.lua";
    write(entry, R"(local extension = { commands = {} }
function extension.commands.echo(ctx, args)
    assert(os == nil and io == nil and debug == nil and package == nil)
    ctx:log("info", "running echo")
    local child = ctx:call("demo.upper", { text = args.value })
    return { value = args.value, upper = child.text, list = {1, 2, 3} }
end
return extension
)");

    star::extension::Manifest manifest{
        .schema = "star.extension/v1",
        .kind = star::extension::Kind::tool,
        .id = "demo.echo",
        .name = "Echo",
        .version = "1.0.0",
        .api = ">=1.0 <2.0",
        .root = temporary.path(),
        .entry = entry,
        .permissions = {"tool.invoke"},
        .commands = {{.name = "echo"}},
    };
    star::runtime::VectorEventSink events;
    star::runtime::ExecutionContext context{
        .request_id = "request-lua",
        .trace_id = "trace-lua",
        .source = "test",
        .permissions = {"tool.invoke"},
        .events = &events,
    };
    const auto result = runtime.invoke(
        manifest, "echo", star::runtime::Json{{"value", "star"}}, context);
    require(result.has_value(), result ? "Lua result" : result.error().message);
    require((*result)["upper"] == "STAR", "ctx:call result returned to Lua");
    require((*result)["list"].size() == 3, "Lua arrays convert to JSON");
    require(events.events().size() == 1, "Lua log emitted one event");
    require(events.events().front().payload["message"] == "running echo",
            "Lua log payload");

    auto denied = context;
    denied.permissions.clear();
    const auto denied_result = runtime.invoke(
        manifest, "echo", star::runtime::Json{{"value", "star"}}, denied);
    require(!denied_result, "effective permission intersection enforced");
    require(denied_result.error().code == star::runtime::ErrorCode::tool,
            "Lua call failure becomes tool error");
}
