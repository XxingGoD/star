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
  -h, --help     show help
  -V, --version  show version
  --box <ref>    select the current Box
  --plain        disable terminal styling
  --json         emit NDJSON events
  --trace        include capability caller chains
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

auto print_family_help(std::string_view family, std::ostream& out) -> bool {
    if (family == "tool") out << tool_help;
    else if (family == "field") out << field_help;
    else if (family == "box") out << box_help;
    else if (family == "interface") out << interface_help;
    else return false;
    return true;
}

class HumanEventSink final : public star::runtime::EventSink {
public:
    HumanEventSink(std::ostream& output, std::ostream& errors)
        : output_(output), errors_(errors) {}

    void emit(const Event& event) override {
        if (event.kind == EventKind::log) {
            output_ << '[' << event.payload.value("level", "info") << "] "
                    << event.payload.value("message", "") << '\n';
        } else if (event.kind == EventKind::progress) {
            output_ << event.payload.value("phase", "work") << ' '
                    << event.payload.value("percent", 0) << "% "
                    << event.payload.value("message", "") << '\n';
        } else if (event.kind == EventKind::error) {
            errors_ << event.payload.value("code", "E_INTERNAL") << ": "
                    << event.payload.value("message", "operation failed") << '\n';
        } else if (event.kind == EventKind::result &&
                   event.payload.value("success", false) &&
                   event.payload.contains("data") &&
                   !event.payload["data"].is_null()) {
            output_ << event.payload["data"].dump(2) << '\n';
        }
    }

private:
    std::ostream& output_;
    std::ostream& errors_;
};

struct GlobalArgs {
    bool json = false;
    bool trace = false;
    std::optional<std::string> box;
    std::span<const std::string_view> command;
};

auto parse_global(std::span<const std::string_view> args)
    -> star::runtime::Result<GlobalArgs> {
    GlobalArgs result{.command = args};
    std::size_t index = 0;
    while (index < args.size()) {
        if (args[index] == "--json") result.json = true;
        else if (args[index] == "--trace") result.trace = true;
        else if (args[index] == "--plain") {}
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
    args = global->command;
    if (args.empty() || is_help(args.front())) {
        out << root_help;
        return 0;
    }
    const auto command = args.front();
    if (command == "version" || command == "-V" || command == "--version") {
        out << info::name << ' ' << info::version << '\n';
        return 0;
    }
    if (command == "doctor") {
        out << "star " << info::version << '\n'
            << "protocol " << info::protocol_version << '\n'
            << "status ok\n";
        return 0;
    }
    if ((command == "tool" || command == "field" || command == "box" ||
         command == "interface") &&
        (args.size() == 1 || is_help(args[1]))) {
        print_family_help(command, out);
        return 0;
    }

    auto roots = options.extension_roots.empty()
        ? default_extension_roots()
        : std::move(options.extension_roots);
    auto backends = box::Registry::builtins(options.include_fake_backend);
    capability::Registry capabilities;
    if (auto result = box::register_capabilities(capabilities, backends); !result) {
        err << result.error().message << '\n';
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
        err << tool_invoke.error().message << '\n';
        return 1;
    }
    capability::Dispatcher dispatcher(capabilities);
    dispatcher_pointer = &dispatcher;
    lua_runtime::Runtime lua(dispatcher);

    HumanEventSink human_events(out, err);
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
        err << "error: invalid " << command << " command\n";
        print_family_help(command, err);
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
        err << "error: invalid box command\n";
        out << box_help;
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
        err << "error: invalid interface command\n";
        return 64;
    }

    err << "error: unknown command '" << command << "'\n"
        << "hint: run 'star --help'\n";
    return 64;
}

} // namespace star::cli
