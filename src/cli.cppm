export module star.cli;

import std;
import star.info;

namespace {

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
  star box <backend|list|inspect|exec> [ARGS]

COMMANDS:
  backend list              list available Box backends
  list                      list known Boxes
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
    if (family == "tool") {
        out << tool_help;
    } else if (family == "field") {
        out << field_help;
    } else if (family == "box") {
        out << box_help;
    } else if (family == "interface") {
        out << interface_help;
    } else {
        return false;
    }
    return true;
}

} // namespace

export namespace star::cli {

auto run(std::span<const std::string_view> args, std::ostream& out,
         std::ostream& err) -> int {
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

    if (print_family_help(command, out)) {
        if (args.size() == 1 || is_help(args[1])) {
            return 0;
        }
        err << "error: '" << command << ' ' << args[1]
            << "' is not implemented in this build\n";
        return 64;
    }

    err << "error: unknown command '" << command << "'\n"
        << "hint: run 'star --help'\n";
    return 64;
}

} // namespace star::cli
