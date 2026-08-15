import std;
import star.extension.manifest;
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
                ("star-manifest-" + std::to_string(
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

auto manifest_text(std::string_view id = "firmware.binwalk") -> std::string {
    return std::format(R"(schema = "star.extension/v1"
kind = "tool"
id = "{}"
name = "Binwalk"
version = "1.0.0"
api = ">=1.0 <2.0"
entry = "main.lua"
permissions = ["box.exec"]
requires_tools = []

[[commands]]
name = "scan"
description = "Scan firmware"
input_schema = "schemas/scan.input.json"
cli_positionals = ["path"]
destructive = false
)", id);
}

void make_package(const std::filesystem::path& root, std::string_view id) {
    write(root / "star.toml", manifest_text(id));
    write(root / "main.lua", "return { commands = {} }\n");
    write(root / "schemas/scan.input.json", "{\"type\":\"object\"}\n");
}

} // namespace

int main() {
    TempDirectory temporary;
    const auto package = temporary.path() / "binwalk";
    make_package(package, "firmware.binwalk");

    const auto manifest = star::extension::parse_manifest(package / "star.toml");
    require(manifest.has_value(), "valid manifest parses");
    require(manifest->kind == star::extension::Kind::tool, "tool kind");
    require(manifest->commands.size() == 1, "one command");
    require(manifest->commands.front().cli_positionals ==
                std::vector<std::string>{"path"},
            "CLI projection parsed");

    make_package(temporary.path() / "alpha", "demo.alpha");
    const auto discovered = star::extension::discover(
        {temporary.path()}, star::extension::Kind::tool);
    require(discovered.has_value(), "discovery succeeds");
    require(discovered->size() == 2, "two tools discovered");
    require(discovered->front().id == "demo.alpha", "deterministic ID order");

    const auto escape = temporary.path() / "escape";
    write(escape / "star.toml", manifest_text("demo.escape"));
    write(escape / "schemas/scan.input.json", "{}\n");
    write(temporary.path() / "outside.lua", "return {}\n");
    auto escaped_text = manifest_text("demo.escape");
    const auto marker = std::string{"entry = \"main.lua\""};
    escaped_text.replace(escaped_text.find(marker), marker.size(),
                         "entry = \"../outside.lua\"");
    write(escape / "star.toml", escaped_text);
    const auto escaped = star::extension::parse_manifest(escape / "star.toml");
    require(!escaped, "entry cannot escape package");

    write(temporary.path() / "broken.toml", "kind = [\n");
    const auto broken = star::extension::parse_manifest(
        temporary.path() / "broken.toml");
    require(!broken, "invalid TOML rejected");
}
