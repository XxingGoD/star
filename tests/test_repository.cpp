import std;
import star.extension.manifest;
import star.extension.repository;
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
                ("star-repository-" + std::to_string(
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

void make_package(const std::filesystem::path& root, std::string_view id,
                  std::string_view kind = "tool") {
    write(root / "star.toml", std::format(R"(schema = "star.extension/v1"
kind = "{}"
id = "{}"
name = "Repository Test"
version = "1.2.3"
api = ">=1.0 <2.0"
entry = "main.lua"
permissions = []
requires_tools = []

[[commands]]
name = "run"
input_schema = "schemas/input.json"
cli_positionals = ["value"]
destructive = false
)", kind, id));
    write(root / "main.lua", "return { commands = {} }\n");
    write(root / "schemas/input.json", "{\"type\":\"object\"}\n");
    write(root / "assets/data.txt", "managed by star\n");
}

} // namespace

int main() {
    TempDirectory temporary;
    const auto source = temporary.path() / "source";
    const auto repository_path = temporary.path() / "star" / "repository";
    make_package(source, "demo.echo");

    star::extension::Repository repository(repository_path);
    const auto installed = repository.install(
        source, star::extension::Kind::tool);
    require(installed.has_value(), "tool package installs");
    require(installed->root == repository_path / "tools/demo.echo",
            "tool uses the managed repository layout");
    require(std::filesystem::is_regular_file(
                installed->root / "assets/data.txt"),
            "nested package files are copied");
    require(std::filesystem::is_regular_file(source / "star.toml"),
            "source package remains unchanged");

    const auto discovered = star::extension::discover(
        repository.extension_roots(), star::extension::Kind::tool);
    require(discovered.has_value() && discovered->size() == 1,
            "installed tool is discoverable");
    require(discovered->front().id == "demo.echo",
            "installed tool identity is preserved");

    const auto duplicate = repository.install(
        source, star::extension::Kind::tool);
    require(!duplicate, "duplicate install is rejected");
    require(duplicate.error().message.contains("already installed"),
            "duplicate install explains the conflict");

    const auto field_source = temporary.path() / "field-source";
    make_package(field_source, "demo.field", "field");
    const auto wrong_kind = repository.install(
        field_source, star::extension::Kind::tool);
    require(!wrong_kind, "wrong package kind is rejected");

    const auto linked_source = temporary.path() / "linked-source";
    make_package(linked_source, "demo.linked");
    std::error_code symlink_error;
    std::filesystem::create_symlink(
        temporary.path() / "outside", linked_source / "outside.link",
        symlink_error);
    if (!symlink_error) {
        const auto linked = repository.install(
            linked_source, star::extension::Kind::tool);
        require(!linked, "symbolic links are rejected");
    }

    const auto traversal = repository.uninstall(
        star::extension::Kind::tool, "../source");
    require(!traversal, "invalid removal id is rejected");
    require(std::filesystem::is_regular_file(source / "star.toml"),
            "invalid removal cannot escape the repository");

    const auto missing = repository.uninstall(
        star::extension::Kind::tool, "demo.missing");
    require(!missing &&
            missing.error().code == star::runtime::ErrorCode::not_found,
            "missing package reports not found");

    const auto removed = repository.uninstall(
        star::extension::Kind::tool, "demo.echo");
    require(removed.has_value(), "installed package is removed");
    require(!std::filesystem::exists(repository_path / "tools/demo.echo"),
            "removed package is no longer discoverable");
}
