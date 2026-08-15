module;

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <cstdio>
#include <io.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

export module star.cli;

import std;
import star.box.backend;
import star.box.capabilities;
import star.capability.dispatcher;
import star.extension.manifest;
import star.info;
import star.lua.runtime;
import star.runtime.context;
import star.runtime.event;
import star.runtime.result;
import star.schema;

namespace {

using star::runtime::Error;
using star::runtime::ErrorCode;
using star::runtime::Event;
using star::runtime::EventKind;
using star::runtime::ExecutionContext;
using star::runtime::Json;

constexpr std::string_view root_help = R"(Star - deterministic tool, field, and box framework

USAGE:
  star [GLOBAL OPTIONS] <COMMAND> [ARGS]

COMMANDS:
  tool        discover and run atomic tool adapters
  field       compose tools into domain workflows
  box         manage backend-neutral execution environments
  interface   programmatic capability interface
  doctor      inspect the local Star environment
  version     print the Star version

GLOBAL OPTIONS:
  -h, --help       show help
  -V, --version    show version
  --box <ref>      select the current Box
  --plain          disable terminal styling (alias: --color never)
  --color <mode>   terminal color: auto, always, or never
  --json           emit NDJSON events
  --trace          include capability caller chains
)";

constexpr std::string_view tool_help = R"(USAGE:
  star tool <list|info|run> [ARGS]

COMMANDS:
  list                      list discovered Tool extensions
  info <tool>               show a Tool manifest
  run <tool> <command> ...  invoke a Tool command
)";

constexpr std::string_view field_help = R"(USAGE:
  star field <list|info|run> [ARGS]

COMMANDS:
  list                         list discovered Field extensions
  info <field>                 show a Field manifest
  run <field> <workflow> ...   invoke a Field workflow
)";

constexpr std::string_view box_help = R"(USAGE:
  star box <backend|inspect|exec> [ARGS]

COMMANDS:
  backend list              list available Box backends
  inspect <box-ref>         inspect one Box
  exec <box-ref> -- <argv>  execute an argv vector in a Box
)";

constexpr std::string_view interface_help = R"(USAGE:
  star interface <version|list|call> [ARGS]

COMMANDS:
  version                         print interface protocol version
  list                            list registered capabilities
  call <capability> --args <json> invoke a capability
)";

auto is_help(std::string_view value) -> bool {
    return value == "help" || value == "-h" || value == "--help";
}

enum class ColorMode { automatic, always, never };

enum class Tone {
    brand,
    heading,
    command,
    value,
    success,
    warning,
    error,
    muted,
    json_key,
    json_string,
    json_literal,
};

constexpr auto ansi_code(Tone tone) -> std::string_view {
    switch (tone) {
    case Tone::brand: return "\x1b[1;36m";
    case Tone::heading: return "\x1b[1;36m";
    case Tone::command: return "\x1b[36m";
    case Tone::value: return "\x1b[33m";
    case Tone::success: return "\x1b[32m";
    case Tone::warning: return "\x1b[33m";
    case Tone::error: return "\x1b[31m";
    case Tone::muted: return "\x1b[2m";
    case Tone::json_key: return "\x1b[36m";
    case Tone::json_string: return "\x1b[32m";
    case Tone::json_literal: return "\x1b[35m";
    }
    return {};
}

class StyledWriter {
public:
    StyledWriter(std::ostream& stream, bool color)
        : stream_(stream), color_(color) {}

    void raw(std::string_view text) { stream_ << text; }

    void styled(Tone tone, std::string_view text) {
        if (!color_) {
            raw(text);
            return;
        }
        stream_ << ansi_code(tone) << text << "\x1b[0m";
    }

private:
    std::ostream& stream_;
    bool color_;
};

auto is_terminal_stream(std::ostream& stream) -> bool {
#ifdef _WIN32
    DWORD handle_id = 0;
    int descriptor = -1;
    if (std::addressof(stream) == std::addressof(std::cout)) {
        handle_id = STD_OUTPUT_HANDLE;
        descriptor = _fileno(stdout);
    } else if (std::addressof(stream) == std::addressof(std::cerr) ||
               std::addressof(stream) == std::addressof(std::clog)) {
        handle_id = STD_ERROR_HANDLE;
        descriptor = _fileno(stderr);
    } else {
        return false;
    }
    if (descriptor < 0 || _isatty(descriptor) == 0) return false;
    const auto handle = GetStdHandle(handle_id);
    DWORD mode = 0;
    if (handle == INVALID_HANDLE_VALUE || handle == nullptr ||
        !GetConsoleMode(handle, &mode)) {
        return false;
    }
    return (mode & ENABLE_VIRTUAL_TERMINAL_PROCESSING) != 0 ||
           SetConsoleMode(handle, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
#else
    int descriptor = -1;
    if (std::addressof(stream) == std::addressof(std::cout)) {
        descriptor = STDOUT_FILENO;
    } else if (std::addressof(stream) == std::addressof(std::cerr) ||
               std::addressof(stream) == std::addressof(std::clog)) {
        descriptor = STDERR_FILENO;
    }
    return descriptor >= 0 && ::isatty(descriptor) == 1;
#endif
}

auto color_enabled(ColorMode mode, std::ostream& stream) -> bool {
    if (mode == ColorMode::always) return true;
    if (mode == ColorMode::never) return false;
    if (const auto* no_color = std::getenv("NO_COLOR");
        no_color && *no_color != '\0') {
        return false;
    }
    if (const auto* term = std::getenv("TERM");
        term && std::string_view(term) == "dumb") {
        return false;
    }
    return is_terminal_stream(stream);
}

void render_help(std::string_view help, StyledWriter& writer) {
    std::string_view section;
    std::size_t position = 0;
    bool first_line = true;
    while (position < help.size()) {
        const auto end = help.find('\n', position);
        const auto line = help.substr(position,
            end == std::string_view::npos ? help.size() - position
                                          : end - position);

        if (first_line && line.starts_with("Star")) {
            writer.styled(Tone::brand, "Star");
            writer.raw(line.substr(4));
        } else if (line == "USAGE:" || line == "COMMANDS:" ||
                   line == "GLOBAL OPTIONS:") {
            section = line;
            writer.styled(Tone::heading, line);
        } else if (line.starts_with("  ") && section == "USAGE:") {
            writer.raw("  ");
            const auto content = line.substr(2);
            const auto first_space = content.find(' ');
            if (first_space == std::string_view::npos) {
                writer.styled(Tone::command, content);
            } else {
                writer.styled(Tone::brand, content.substr(0, first_space));
                writer.raw(content.substr(first_space));
            }
        } else if (line.starts_with("  ") &&
                   (section == "COMMANDS:" || section == "GLOBAL OPTIONS:")) {
            const auto content = line.substr(2);
            auto separator = std::string_view::npos;
            for (std::size_t index = 0; index + 1 < content.size(); ++index) {
                if (content[index] == ' ' && content[index + 1] == ' ') {
                    separator = index;
                    break;
                }
            }
            writer.raw("  ");
            if (separator == std::string_view::npos) {
                writer.styled(Tone::command, content);
            } else {
                auto description = separator;
                while (description < content.size() &&
                       content[description] == ' ') {
                    ++description;
                }
                writer.styled(Tone::command, content.substr(0, separator));
                writer.raw(content.substr(separator, description - separator));
                writer.raw(content.substr(description));
            }
        } else {
            writer.raw(line);
        }

        first_line = false;
        if (end == std::string_view::npos) break;
        writer.raw("\n");
        position = end + 1;
    }
}

auto print_family_help(std::string_view family, StyledWriter& writer) -> bool {
    if (family == "tool") render_help(tool_help, writer);
    else if (family == "field") render_help(field_help, writer);
    else if (family == "box") render_help(box_help, writer);
    else if (family == "interface") render_help(interface_help, writer);
    else return false;
    return true;
}

void render_json(const Json& value, StyledWriter& writer, int depth = 0) {
    const auto indent = [&writer](int amount) {
        writer.raw(std::string(static_cast<std::size_t>(amount), ' '));
    };
    if (value.is_object()) {
        writer.raw("{");
        if (!value.empty()) writer.raw("\n");
        std::size_t index = 0;
        for (auto item = value.cbegin(); item != value.cend(); ++item) {
            indent((depth + 1) * 2);
            writer.styled(Tone::json_key, Json(item.key()).dump());
            writer.raw(": ");
            render_json(*item, writer, depth + 1);
            if (++index != value.size()) writer.raw(",");
            writer.raw("\n");
        }
        if (!value.empty()) indent(depth * 2);
        writer.raw("}");
    } else if (value.is_array()) {
        writer.raw("[");
        if (!value.empty()) writer.raw("\n");
        for (std::size_t index = 0; index < value.size(); ++index) {
            indent((depth + 1) * 2);
            render_json(value[index], writer, depth + 1);
            if (index + 1 != value.size()) writer.raw(",");
            writer.raw("\n");
        }
        if (!value.empty()) indent(depth * 2);
        writer.raw("]");
    } else if (value.is_string()) {
        writer.styled(Tone::json_string, value.dump());
    } else if (value.is_number()) {
        writer.styled(Tone::value, value.dump());
    } else if (value.is_boolean() || value.is_null()) {
        writer.styled(value.is_boolean() ? Tone::json_literal : Tone::muted,
                      value.dump());
    } else {
        writer.raw(value.dump());
    }
}

class HumanEventSink final : public star::runtime::EventSink {
public:
    HumanEventSink(std::ostream& output, std::ostream& errors,
                   bool output_color, bool error_color)
        : output_(output, output_color), errors_(errors, error_color) {}

    void emit(const Event& event) override {
        if (event.kind == EventKind::log) {
            const auto level = event.payload.value("level", "info");
            const auto tone = level == "error" ? Tone::error :
                              level == "warn" ? Tone::warning :
                              level == "debug" ? Tone::muted : Tone::command;
            output_.styled(tone, "[" + level + "]");
            output_.raw(" " + event.payload.value("message", "") + "\n");
        } else if (event.kind == EventKind::progress) {
            output_.styled(Tone::command, event.payload.value("phase", "work"));
            output_.raw(" ");
            output_.styled(Tone::value,
                std::to_string(event.payload.value("percent", 0)) + "%");
            output_.raw(" " + event.payload.value("message", "") + "\n");
        } else if (event.kind == EventKind::error) {
            errors_.styled(Tone::error,
                event.payload.value("code", "E_INTERNAL"));
            errors_.raw(": " +
                event.payload.value("message", "operation failed") + "\n");
            const auto hint = event.payload.value("hint", "");
            if (!hint.empty()) {
                errors_.styled(Tone::warning, "hint:");
                errors_.raw(" " + hint + "\n");
            }
        } else if (event.kind == EventKind::result &&
                   event.payload.value("success", false) &&
                   event.payload.contains("data") &&
                   !event.payload["data"].is_null()) {
            render_json(event.payload["data"], output_);
            output_.raw("\n");
        }
    }

private:
    StyledWriter output_;
    StyledWriter errors_;
};

struct GlobalArgs {
    bool json = false;
    bool trace = false;
    ColorMode color = ColorMode::automatic;
    std::optional<std::string> box;
    std::span<const std::string_view> command;
};

auto parse_color(std::string_view value) -> star::runtime::Result<ColorMode> {
    if (value == "auto") return ColorMode::automatic;
    if (value == "always") return ColorMode::always;
    if (value == "never") return ColorMode::never;
    return std::unexpected(Error{
        .code = ErrorCode::invalid_input,
        .message = "invalid color mode '" + std::string(value) +
                   "'; expected auto, always, or never",
    });
}

auto parse_global(std::span<const std::string_view> args)
    -> star::runtime::Result<GlobalArgs> {
    GlobalArgs result{.command = args};
    std::size_t index = 0;
    while (index < args.size()) {
        if (args[index] == "--json") result.json = true;
        else if (args[index] == "--trace") result.trace = true;
        else if (args[index] == "--plain") result.color = ColorMode::never;
        else if (args[index] == "--color") {
            if (++index >= args.size()) {
                return std::unexpected(Error{
                    .code = ErrorCode::invalid_input,
                    .message = "--color requires auto, always, or never",
                });
            }
            auto color = parse_color(args[index]);
            if (!color) return std::unexpected(color.error());
            result.color = *color;
        } else if (args[index].starts_with("--color=")) {
            auto color = parse_color(args[index].substr(
                std::string_view("--color=").size()));
            if (!color) return std::unexpected(color.error());
            result.color = *color;
        }
        else if (args[index] == "--box") {
            if (++index >= args.size()) {
                return std::unexpected(Error{
                    .code = ErrorCode::invalid_input,
                    .message = "--box requires a Box reference",
                });
            }
            result.box = std::string(args[index]);
        } else {
            break;
        }
        ++index;
    }
    result.command = args.subspan(index);
    return result;
}

auto default_extension_roots() -> std::vector<std::filesystem::path> {
    std::vector<std::filesystem::path> roots;
    if (const auto* value = std::getenv("STAR_EXTENSION_PATH")) {
#ifdef _WIN32
        constexpr char separator = ';';
#else
        constexpr char separator = ':';
#endif
        std::string_view paths(value);
        while (true) {
            const auto end = paths.find(separator);
            if (end != 0) roots.emplace_back(paths.substr(0, end));
            if (end == std::string_view::npos) break;
            paths.remove_prefix(end + 1);
        }
    }
    roots.push_back(std::filesystem::current_path() / ".star/extensions");
    if (const auto* home = std::getenv("STAR_HOME")) {
        roots.emplace_back(std::filesystem::path(home) / "extensions");
    } else {
#ifdef _WIN32
        const auto* user_home = std::getenv("USERPROFILE");
#else
        const auto* user_home = std::getenv("HOME");
#endif
        if (user_home) roots.emplace_back(std::filesystem::path(user_home) /
                                     ".star/extensions");
    }
    return roots;
}

auto request_id(std::string_view prefix) -> std::string {
    static std::atomic_uint64_t sequence = 0;
    return std::format("{}-{}", prefix, ++sequence);
}

auto exit_code(const Error& error) -> int {
    switch (error.code) {
    case ErrorCode::invalid_input: return 64;
    case ErrorCode::unsupported: return 3;
    case ErrorCode::policy_denied:
    case ErrorCode::permission: return 2;
    case ErrorCode::cancelled: return 130;
    default: return 1;
    }
}

auto finish(const ExecutionContext& context,
            star::runtime::Result<Json> result) -> int {
    if (!result) {
        context.emit(EventKind::error, Json{
            {"code", star::runtime::error_code_name(result.error().code)},
            {"message", result.error().message},
            {"hint", result.error().hint},
            {"recoverable", result.error().recoverable},
        });
        const auto code = exit_code(result.error());
        context.emit(EventKind::result, Json{
            {"exitCode", code}, {"success", false},
        });
        return code;
    }
    context.emit(EventKind::result, Json{
        {"exitCode", 0}, {"success", true}, {"data", std::move(*result)},
    });
    return 0;
}

auto manifest_json(const star::extension::Manifest& manifest) -> Json {
    Json commands = Json::array();
    for (const auto& command : manifest.commands) {
        commands.push_back({
            {"name", command.name},
            {"description", command.description},
            {"destructive", command.destructive},
            {"cliPositionals", command.cli_positionals},
        });
    }
    return {
        {"schema", manifest.schema},
        {"kind", star::extension::kind_name(manifest.kind)},
        {"id", manifest.id},
        {"name", manifest.name},
        {"version", manifest.version},
        {"api", manifest.api},
        {"description", manifest.description},
        {"permissions", manifest.permissions},
        {"requiresTools", manifest.requires_tools},
        {"commands", std::move(commands)},
        {"root", manifest.root.string()},
    };
}

auto find_manifest(const std::vector<star::extension::Manifest>& manifests,
                   std::string_view id)
    -> star::runtime::Result<std::reference_wrapper<const star::extension::Manifest>> {
    const auto found = std::ranges::find(manifests, id,
                                         &star::extension::Manifest::id);
    if (found == manifests.end()) {
        return std::unexpected(Error{
            .code = ErrorCode::not_found,
            .message = "extension not found: " + std::string(id),
        });
    }
    return std::cref(*found);
}

} // namespace

export namespace star::cli {

struct Options {
    std::vector<std::filesystem::path> extension_roots;
    bool include_fake_backend = false;
};

auto run(std::span<const std::string_view> args, std::ostream& out,
         std::ostream& err, Options options = {}) -> int {
    auto global = parse_global(args);
    if (!global) {
        err << star::runtime::error_code_name(global.error().code) << ": "
            << global.error().message << '\n';
        return exit_code(global.error());
    }
    const auto color_mode = global->json ? ColorMode::never : global->color;
    const auto output_color = color_enabled(color_mode, out);
    const auto error_color = color_enabled(color_mode, err);
    StyledWriter output(out, output_color);
    StyledWriter errors(err, error_color);
    args = global->command;
    if (args.empty() || is_help(args.front())) {
        render_help(root_help, output);
        return 0;
    }
    const auto command = args.front();
    if (command == "version" || command == "-V" || command == "--version") {
        output.styled(Tone::brand, info::name);
        output.raw(" ");
        output.styled(Tone::value, info::version);
        output.raw("\n");
        return 0;
    }
    if (command == "doctor") {
        output.styled(Tone::brand, "star");
        output.raw(" ");
        output.styled(Tone::value, info::version);
        output.raw("\n");
        output.styled(Tone::command, "protocol");
        output.raw(" ");
        output.styled(Tone::value, info::protocol_version);
        output.raw("\n");
        output.styled(Tone::command, "status");
        output.raw(" ");
        output.styled(Tone::success, "ok");
        output.raw("\n");
        return 0;
    }
    if ((command == "tool" || command == "field" || command == "box" ||
         command == "interface") &&
        (args.size() == 1 || is_help(args[1]))) {
        print_family_help(command, output);
        return 0;
    }

    auto roots = options.extension_roots.empty()
        ? default_extension_roots()
        : std::move(options.extension_roots);
    auto backends = box::Registry::builtins(options.include_fake_backend);
    capability::Registry capabilities;
    if (auto result = box::register_capabilities(capabilities, backends); !result) {
        errors.styled(Tone::error, result.error().message);
        errors.raw("\n");
        return 1;
    }
    const capability::Dispatcher* dispatcher_pointer = nullptr;
    auto tool_invoke = capabilities.add({
        .name = "tool.invoke",
        .description = "Invoke a Tool command through the inherited context",
        .required_permissions = {"tool.invoke"},
        .handler = [&roots, &dispatcher_pointer](const ExecutionContext& context,
                                                 const Json& input)
            -> star::runtime::Result<Json> {
            if (!dispatcher_pointer || !input.is_object() ||
                !input.contains("tool") || !input["tool"].is_string() ||
                !input.contains("command") || !input["command"].is_string()) {
                return std::unexpected(Error{
                    .code = ErrorCode::invalid_input,
                    .message = "tool.invoke requires tool and command",
                });
            }
            auto manifests = extension::discover(roots, extension::Kind::tool);
            if (!manifests) return std::unexpected(manifests.error());
            auto manifest = find_manifest(*manifests,
                input["tool"].get<std::string>());
            if (!manifest) return std::unexpected(manifest.error());
            const auto command_name = input["command"].get<std::string>();
            const auto found = std::ranges::find(
                manifest->get().commands, command_name, &extension::Command::name);
            if (found == manifest->get().commands.end()) {
                return std::unexpected(Error{
                    .code = ErrorCode::not_found,
                    .message = "Tool command not found: " + command_name,
                });
            }
            auto call_args = input.value("args", Json::object());
            auto command_schema = schema::load(found->input_schema);
            if (!command_schema) return std::unexpected(command_schema.error());
            if (auto valid = schema::validate(*command_schema, call_args); !valid) {
                return std::unexpected(valid.error());
            }
            lua_runtime::Runtime runtime(*dispatcher_pointer);
            return runtime.invoke(manifest->get(), command_name, call_args, context);
        },
    });
    if (!tool_invoke) {
        errors.styled(Tone::error, tool_invoke.error().message);
        errors.raw("\n");
        return 1;
    }
    capability::Dispatcher dispatcher(capabilities);
    dispatcher_pointer = &dispatcher;
    lua_runtime::Runtime lua(dispatcher);

    HumanEventSink human_events(out, err, output_color, error_color);
    runtime::NdjsonEventSink json_events(out);
    runtime::EventSink* event_sink = global->json ?
        static_cast<runtime::EventSink*>(&json_events) :
        static_cast<runtime::EventSink*>(&human_events);
    auto make_context = [&](std::string source,
                            std::unordered_set<std::string> permissions) {
        const auto id = request_id("request");
        return ExecutionContext{
            .request_id = id,
            .trace_id = request_id("trace"),
            .source = std::move(source),
            .permissions = std::move(permissions),
            .box = global->box,
            .workspace_host = std::filesystem::current_path(),
            .workspace_box = std::filesystem::current_path(),
            .events = event_sink,
        };
    };

    if (command == "tool" || command == "field") {
        const auto kind = command == "tool" ? extension::Kind::tool
                                             : extension::Kind::field;
        auto manifests = extension::discover(roots, kind);
        auto context = make_context("star." + std::string(command), {});
        if (!manifests) return finish(context, std::unexpected(manifests.error()));
        const auto subcommand = args[1];
        if (subcommand == "list" && args.size() == 2) {
            Json result = Json::array();
            for (const auto& manifest : *manifests) {
                result.push_back({
                    {"id", manifest.id}, {"name", manifest.name},
                    {"version", manifest.version},
                });
            }
            return finish(context, std::move(result));
        }
        if (subcommand == "info" && args.size() == 3) {
            auto manifest = find_manifest(*manifests, args[2]);
            if (!manifest) return finish(context, std::unexpected(manifest.error()));
            return finish(context, manifest_json(manifest->get()));
        }
        if (subcommand == "run" && args.size() >= 4) {
            auto manifest = find_manifest(*manifests, args[2]);
            if (!manifest) return finish(context, std::unexpected(manifest.error()));
            const auto selected = std::ranges::find(
                manifest->get().commands, args[3], &extension::Command::name);
            if (selected == manifest->get().commands.end()) {
                return finish(context, std::unexpected(Error{
                    .code = ErrorCode::not_found,
                    .message = "extension command not found: " + std::string(args[3]),
                }));
            }
            auto input = schema::command_input(*selected, args.subspan(4));
            if (!input) return finish(context, std::unexpected(input.error()));
            context.permissions.insert(manifest->get().permissions.begin(),
                                       manifest->get().permissions.end());
            if (kind == extension::Kind::field) {
                context.field = manifest->get().id;
            }
            return finish(context, lua.invoke(manifest->get(), args[3],
                                              *input, context));
        }
        errors.styled(Tone::error, "error:");
        errors.raw(" invalid " + std::string(command) + " command\n");
        print_family_help(command, errors);
        return 64;
    }

    if (command == "box") {
        const auto subcommand = args[1];
        if (subcommand == "backend" && args.size() == 3 && args[2] == "list") {
            Json result = Json::array();
            for (const auto& backend : backends.list()) {
                result.push_back({
                    {"name", backend.get().name()},
                    {"available", backend.get().available()},
                    {"capabilities", backend.get().capabilities()},
                });
            }
            return finish(make_context("star.box", {}), std::move(result));
        }
        if (subcommand == "inspect" && args.size() == 3) {
            auto context = make_context("star.box.inspect", {"box.inspect"});
            return finish(context, dispatcher.invoke(
                "box.inspect", Json{{"ref", args[2]}}, context));
        }
        if (subcommand == "exec" && args.size() >= 5 && args[3] == "--") {
            auto ref = box::parse_ref(args[2]);
            auto context = make_context("star.box.exec", {"box.exec"});
            if (!ref) return finish(context, std::unexpected(ref.error()));
            const auto* backend = backends.find(ref->backend);
            if (!backend) {
                return finish(context, std::unexpected(Error{
                    .code = ErrorCode::not_found,
                    .message = "Box backend not found: " + ref->backend,
                }));
            }
            std::vector<std::string> process_args;
            for (const auto value : args.subspan(4)) process_args.emplace_back(value);
            auto result = backend->exec(*ref, {
                .argv = std::move(process_args),
                .capture_output = global->json,
            });
            if (!result) return finish(context, std::unexpected(result.error()));
            if (result->exit_code != 0) {
                context.emit(EventKind::error, Json{
                    {"code", "E_TOOL"},
                    {"message", "Box process exited with a non-zero status"},
                });
            }
            context.emit(EventKind::result, Json{
                {"exitCode", result->exit_code},
                {"success", result->exit_code == 0},
                {"data", Json{{"output", result->output}}},
            });
            return result->exit_code;
        }
        errors.styled(Tone::error, "error:");
        errors.raw(" invalid box command\n");
        render_help(box_help, output);
        return 64;
    }

    if (command == "interface") {
        runtime::NdjsonEventSink interface_events(out);
        auto context = make_context("star.interface", {
            "box.inspect", "box.exec", "tool.invoke",
        });
        context.events = &interface_events;
        if (args[1] == "version" && args.size() == 2) {
            return finish(context, Json{{"protocol", info::protocol_version}});
        }
        if (args[1] == "list" && args.size() == 2) {
            Json result = Json::array();
            for (const auto& spec : capabilities.list()) {
                result.push_back({
                    {"name", spec.get().name},
                    {"description", spec.get().description},
                    {"permissions", spec.get().required_permissions},
                });
            }
            return finish(context, std::move(result));
        }
        if (args[1] == "call" && args.size() == 5 && args[3] == "--args") {
            try {
                return dispatcher.dispatch(args[2], Json::parse(args[4]), context);
            } catch (const std::exception& error) {
                return finish(context, std::unexpected(Error{
                    .code = ErrorCode::invalid_input,
                    .message = "invalid interface args JSON: " +
                               std::string(error.what()),
                }));
            }
        }
        errors.styled(Tone::error, "error:");
        errors.raw(" invalid interface command\n");
        return 64;
    }

    errors.styled(Tone::error, "error:");
    errors.raw(" unknown command '" + std::string(command) + "'\n");
    errors.styled(Tone::warning, "hint:");
    errors.raw(" run 'star --help'\n");
    return 64;
}

} // namespace star::cli
