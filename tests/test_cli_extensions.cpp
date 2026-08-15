import std;
import star.cli;

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
                ("star-cli-" + std::to_string(
                    std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(path_);
    }
    ~TempDirectory() { std::filesystem::remove_all(path_); }
    auto path() const -> const std::filesystem::path& { return path_; }
private:
    std::filesystem::path path_;
};

void write(const std::filesystem::path& path, std::string_view content) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path);
    output << content;
}

struct RunResult { int code; std::string out; std::string err; };

auto invoke(std::initializer_list<std::string_view> args,
            const std::filesystem::path& root) -> RunResult {
    std::vector<std::string_view> values(args);
    std::ostringstream out;
    std::ostringstream err;
    const auto code = star::cli::run(values, out, err, {
        .extension_roots = {root},
        .include_fake_backend = true,
    });
    return {code, out.str(), err.str()};
}

void make_extensions(const std::filesystem::path& root) {
    const auto schema = R"({
  "type": "object",
  "required": ["path"],
  "additionalProperties": false,
  "properties": {"path": {"type": "string", "minLength": 1}}
})";
    const auto tool = root / "demo-echo";
    write(tool / "star.toml", R"(schema = "star.extension/v1"
kind = "tool"
id = "demo.echo"
name = "Echo"
version = "1.0.0"
api = ">=1.0 <2.0"
entry = "main.lua"
permissions = []
requires_tools = []
[[commands]]
name = "echo"
input_schema = "schemas/input.json"
cli_positionals = ["path"]
destructive = false
)");
    write(tool / "schemas/input.json", schema);
    write(tool / "main.lua", R"(return { commands = {
  echo = function(ctx, args) return { path = string.upper(args.path) } end
} }
)");

    const auto field = root / "demo-field";
    write(field / "star.toml", R"(schema = "star.extension/v1"
kind = "field"
id = "demo"
name = "Demo Field"
version = "1.0.0"
api = ">=1.0 <2.0"
entry = "main.lua"
permissions = ["tool.invoke"]
requires_tools = ["demo.echo@>=1.0 <2.0"]
[[commands]]
name = "run"
input_schema = "schemas/input.json"
cli_positionals = ["path"]
destructive = false
)");
    write(field / "schemas/input.json", schema);
    write(field / "main.lua", R"(return { commands = {
  run = function(ctx, args)
    return ctx:call("tool.invoke", {
      tool = "demo.echo", command = "echo", args = args
    })
  end
} }
)");
}

} // namespace

int main() {
    TempDirectory temporary;
    make_extensions(temporary.path());

    const auto list = invoke({"tool", "list"}, temporary.path());
    require(list.code == 0, "tool list exit code");
    require(list.out.contains("demo.echo"), "tool list discovers manifest");

    const auto tool = invoke(
        {"tool", "run", "demo.echo", "echo", "--", "firmware.chk"},
        temporary.path());
    require(tool.code == 0, tool.err.empty() ? tool.out : tool.err);
    require(tool.out.contains("FIRMWARE.CHK"), "tool Lua result rendered");

    const auto field = invoke(
        {"field", "run", "demo", "run", "--", "firmware.chk"},
        temporary.path());
    require(field.code == 0, field.err);
    require(field.out.contains("FIRMWARE.CHK"),
            "Field calls Tool through inherited context");

    const auto backends = invoke({"box", "backend", "list"}, temporary.path());
    require(backends.code == 0, "backend list exit code");
    require(backends.out.contains("fake"), "fake backend listed in tests");

    const auto box = invoke(
        {"--json", "box", "exec", "fake://local/test", "--", "echo", "star"},
        temporary.path());
    require(box.code == 0, "fake box exec exit code");
    require(box.out.contains("\"kind\":\"result\""),
            "box exec emits NDJSON result");

    const auto interface = invoke(
        {"interface", "call", "box.inspect", "--args",
         R"({"ref":"fake://local/test"})"}, temporary.path());
    require(interface.code == 0, "interface capability call");
    require(interface.out.contains("fake-running"), "interface data returned");
}
