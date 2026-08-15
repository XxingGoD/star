export module star.extension.manifest;

import std;
import tomlplusplus;
import star.runtime.result;

export namespace star::extension {

enum class Kind { tool, field };

auto kind_name(Kind kind) -> std::string_view {
    return kind == Kind::tool ? "tool" : "field";
}

struct Command {
    std::string name;
    std::string description;
    std::filesystem::path input_schema;
    std::optional<std::filesystem::path> output_schema;
    std::vector<std::string> cli_positionals;
    bool destructive = false;
};

struct Manifest {
    std::string schema;
    Kind kind = Kind::tool;
    std::string id;
    std::string name;
    std::string version;
    std::string api;
    std::string description;
    std::filesystem::path root;
    std::filesystem::path entry;
    std::vector<std::string> permissions;
    std::vector<std::string> requires_tools;
    std::optional<std::string> default_box_profile;
    std::vector<Command> commands;
};

namespace detail {

auto invalid(std::string message) -> runtime::Result<Manifest> {
    return std::unexpected(runtime::Error{
        .code = runtime::ErrorCode::invalid_input,
        .message = std::move(message),
    });
}

auto required_string(const toml::table& table, std::string_view key)
    -> std::optional<std::string> {
    auto value = table[key].value<std::string>();
    if (!value || value->empty()) return std::nullopt;
    return *value;
}

auto string_array(const toml::node* node)
    -> std::optional<std::vector<std::string>> {
    const auto* array = node ? node->as_array() : nullptr;
    if (!array) return std::nullopt;
    std::vector<std::string> values;
    values.reserve(array->size());
    for (const auto& item : *array) {
        auto value = item.value<std::string>();
        if (!value || value->empty()) return std::nullopt;
        values.push_back(std::move(*value));
    }
    return values;
}

auto valid_id(std::string_view id) -> bool {
    if (id.empty() || id.front() == '.' || id.back() == '.') return false;
    return std::ranges::all_of(id, [](unsigned char ch) {
        return std::isalnum(ch) || ch == '.' || ch == '-' || ch == '_';
    });
}

auto package_path(const std::filesystem::path& root,
                  std::string_view relative) -> std::optional<std::filesystem::path> {
    const auto candidate = (root / relative).lexically_normal();
    const auto relative_to_root = candidate.lexically_relative(root);
    if (relative_to_root.empty() || relative_to_root.is_absolute()) {
        return std::nullopt;
    }
    const auto first = *relative_to_root.begin();
    if (first == "..") return std::nullopt;
    return candidate;
}

} // namespace detail

auto parse_manifest(const std::filesystem::path& manifest_path)
    -> runtime::Result<Manifest> {
    toml::table document;
    try {
        document = toml::parse_file(manifest_path.string());
    } catch (const toml::parse_error& error) {
        return detail::invalid("invalid TOML in " + manifest_path.string() +
                               ": " + std::string(error.description()));
    } catch (const std::exception& error) {
        return detail::invalid("cannot read manifest " + manifest_path.string() +
                               ": " + error.what());
    }

    Manifest manifest;
    manifest.root = std::filesystem::absolute(manifest_path.parent_path())
                        .lexically_normal();

    const auto schema = detail::required_string(document, "schema");
    const auto kind = detail::required_string(document, "kind");
    const auto id = detail::required_string(document, "id");
    const auto name = detail::required_string(document, "name");
    const auto version = detail::required_string(document, "version");
    const auto api = detail::required_string(document, "api");
    const auto entry = detail::required_string(document, "entry");
    if (!schema || !kind || !id || !name || !version || !api || !entry) {
        return detail::invalid("manifest requires schema, kind, id, name, "
                               "version, api, and entry");
    }
    if (*schema != "star.extension/v1") {
        return detail::invalid("unsupported extension schema: " + *schema);
    }
    if (*kind == "tool") {
        manifest.kind = Kind::tool;
    } else if (*kind == "field") {
        manifest.kind = Kind::field;
    } else {
        return detail::invalid("extension kind must be tool or field");
    }
    if (!detail::valid_id(*id)) {
        return detail::invalid("invalid extension id: " + *id);
    }

    const auto resolved_entry = detail::package_path(manifest.root, *entry);
    if (!resolved_entry || !std::filesystem::is_regular_file(*resolved_entry)) {
        return detail::invalid("entry must name a file inside the extension package");
    }

    manifest.schema = *schema;
    manifest.id = *id;
    manifest.name = *name;
    manifest.version = *version;
    manifest.api = *api;
    manifest.entry = *resolved_entry;
    manifest.description = document["description"].value_or(std::string{});
    if (const auto profile = document["default_box_profile"].value<std::string>()) {
        manifest.default_box_profile = *profile;
    }

    if (document.contains("permissions")) {
        const auto values = detail::string_array(document.get("permissions"));
        if (!values) return detail::invalid("permissions must be a string array");
        manifest.permissions = *values;
    }
    if (document.contains("requires_tools")) {
        const auto values = detail::string_array(document.get("requires_tools"));
        if (!values) return detail::invalid("requires_tools must be a string array");
        manifest.requires_tools = *values;
    }

    const auto* commands = document["commands"].as_array();
    if (!commands || commands->empty()) {
        return detail::invalid("manifest must declare at least one command");
    }
    std::unordered_set<std::string> command_names;
    for (const auto& node : *commands) {
        const auto* table = node.as_table();
        if (!table) return detail::invalid("commands entries must be tables");
        const auto command_name = detail::required_string(*table, "name");
        const auto input_schema = detail::required_string(*table, "input_schema");
        if (!command_name || !detail::valid_id(*command_name) || !input_schema) {
            return detail::invalid("each command requires a valid name and input_schema");
        }
        if (!command_names.insert(*command_name).second) {
            return detail::invalid("duplicate command: " + *command_name);
        }
        const auto resolved_input = detail::package_path(manifest.root, *input_schema);
        if (!resolved_input || !std::filesystem::is_regular_file(*resolved_input)) {
            return detail::invalid("input_schema must name a package file for command " +
                                   *command_name);
        }

        Command command{
            .name = *command_name,
            .description = table->get("description")
                ? table->get("description")->value_or(std::string{})
                : std::string{},
            .input_schema = *resolved_input,
            .destructive = table->get("destructive")
                ? table->get("destructive")->value_or(false)
                : false,
        };
        if (const auto output = table->get("output_schema"); output) {
            const auto raw = output->value<std::string>();
            if (!raw) return detail::invalid("output_schema must be a string");
            const auto resolved = detail::package_path(manifest.root, *raw);
            if (!resolved || !std::filesystem::is_regular_file(*resolved)) {
                return detail::invalid("output_schema must name a package file");
            }
            command.output_schema = *resolved;
        }
        if (const auto positionals = table->get("cli_positionals"); positionals) {
            const auto values = detail::string_array(positionals);
            if (!values) {
                return detail::invalid("cli_positionals must be a string array");
            }
            command.cli_positionals = *values;
        }
        manifest.commands.push_back(std::move(command));
    }
    return manifest;
}

auto discover(const std::vector<std::filesystem::path>& roots,
              std::optional<Kind> expected_kind = std::nullopt)
    -> runtime::Result<std::vector<Manifest>> {
    std::vector<std::filesystem::path> candidates;
    for (const auto& root : roots) {
        if (!std::filesystem::exists(root)) continue;
        if (std::filesystem::is_regular_file(root / "star.toml")) {
            candidates.push_back(root / "star.toml");
        }
        for (auto entries = std::filesystem::directory_iterator(root);
             entries != std::default_sentinel; ++entries) {
            const auto& entry = *entries;
            if (entry.is_directory() &&
                std::filesystem::is_regular_file(entry.path() / "star.toml")) {
                candidates.push_back(entry.path() / "star.toml");
            }
        }
    }
    std::ranges::sort(candidates);

    std::vector<Manifest> manifests;
    std::unordered_set<std::string> ids;
    for (const auto& path : candidates) {
        auto manifest = parse_manifest(path);
        if (!manifest) return std::unexpected(manifest.error());
        if (expected_kind && manifest->kind != *expected_kind) continue;
        if (!ids.insert(manifest->id).second) {
            return std::unexpected(runtime::Error{
                .code = runtime::ErrorCode::invalid_input,
                .message = "duplicate extension id: " + manifest->id,
            });
        }
        manifests.push_back(std::move(*manifest));
    }
    std::ranges::sort(manifests, {}, &Manifest::id);
    return manifests;
}

} // namespace star::extension
