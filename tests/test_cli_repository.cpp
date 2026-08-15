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
                ("star-cli-repository-" + std::to_string(
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

void make_tool(const std::filesystem::path& root) {
    write(root / "star.toml", R"(schema = "star.extension/v1"
kind = "tool"
id = "demo.managed"
name = "Managed Tool"
version = "1.0.0"
api = ">=1.0 <2.0"
entry = "main.lua"
permissions = []
requires_tools = []

[[commands]]
name = "echo"
input_schema = "schemas/input.json"
cli_positionals = ["value"]
destructive = false
)");
    write(root / "schemas/input.json", R"({
  "type": "object",
  "required": ["value"],
  "additionalProperties": false,
  "properties": {"value": {"type": "string", "minLength": 1}}
})");
    write(root / "main.lua", R"(return { commands = {
  echo = function(ctx, args) return { value = string.upper(args.value) } end
} }
)");
}

struct RunResult {
    int code;
    std::string out;
    std::string err;
};

auto invoke(std::initializer_list<std::string_view> args,
            const std::filesystem::path& repository,
            const std::filesystem::path& empty_root) -> RunResult {
    std::vector<std::string_view> values(args);
    std::ostringstream out;
    std::ostringstream err;
    const auto code = star::cli::run(values, out, err, {
        .extension_roots = {empty_root},
        .repository_root = repository,
    });
    return {code, out.str(), err.str()};
}

} // namespace

int main() {
    TempDirectory temporary;
    const auto source = temporary.path() / "source";
    const auto repository = temporary.path() / "star/repository";
    const auto empty_root = temporary.path() / "empty";
    std::filesystem::create_directories(empty_root);
    make_tool(source);

    const auto doctor = invoke({"doctor"}, repository, empty_root);
    require(doctor.code == 0 &&
            doctor.out.contains(std::filesystem::absolute(repository).string()),
            "doctor reports the absolute managed repository root");

    const auto added = invoke(
        {"tool", "add", source.string()}, repository, empty_root);
    require(added.code == 0, added.err.empty() ? added.out : added.err);
    require(added.out.contains("\"status\": \"installed\""),
            "tool add reports installed status");
    require(std::filesystem::is_regular_file(
                repository / "tools/demo.managed/star.toml"),
            "tool add stores the package in the Star repository");

    const auto listed = invoke({"tool", "list"}, repository, empty_root);
    require(listed.code == 0, "managed tool list exit code");
    require(listed.out.contains("demo.managed"),
            "managed tool is discovered without its source path");
    require(listed.out.contains("\"managed\": true"),
            "managed tool is identified in list output");

    const auto info = invoke(
        {"tool", "info", "demo.managed"}, repository, empty_root);
    require(info.code == 0 && info.out.contains("Managed Tool"),
            "managed tool manifest is available");

    const auto run = invoke(
        {"tool", "run", "demo.managed", "echo", "--", "star"},
        repository, empty_root);
    require(run.code == 0, run.err.empty() ? run.out : run.err);
    require(run.out.contains("STAR"),
            "Star invokes the repository copy of the tool");

    const auto duplicate = invoke(
        {"tool", "add", source.string()}, repository, empty_root);
    require(duplicate.code == 64, "duplicate tool add exit code");
    require(duplicate.err.contains("already installed"),
            "duplicate tool add explains conflict");

    const auto json_list = invoke(
        {"--color", "always", "--json", "tool", "list"},
        repository, empty_root);
    require(json_list.code == 0, "managed tool NDJSON list exit code");
    require(json_list.out.contains("\"kind\":\"result\""),
            "managed tool list emits a result event");
    require(!json_list.out.contains("\x1b["),
            "managed tool NDJSON remains unstyled");

    const auto removed = invoke(
        {"tool", "remove", "demo.managed"}, repository, empty_root);
    require(removed.code == 0, removed.err.empty() ? removed.out : removed.err);
    require(removed.out.contains("\"status\": \"removed\""),
            "tool remove reports removed status");
    require(!std::filesystem::exists(repository / "tools/demo.managed"),
            "tool remove removes the managed package");

    const auto after_remove = invoke(
        {"tool", "run", "demo.managed", "echo", "--", "star"},
        repository, empty_root);
    require(after_remove.code != 0, "removed tool cannot be invoked");
    require(after_remove.err.contains("extension not found"),
            "removed tool reports not found");
}
