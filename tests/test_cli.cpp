import std;
import star.cli;

namespace {

void require(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

struct RunResult {
    int code;
    std::string out;
    std::string err;
};

auto invoke(std::initializer_list<std::string_view> args) -> RunResult {
    std::vector<std::string_view> values(args);
    std::ostringstream out;
    std::ostringstream err;
    const auto code = star::cli::run(values, out, err);
    return {code, out.str(), err.str()};
}

auto strip_ansi(std::string_view text) -> std::string {
    std::string result;
    result.reserve(text.size());
    for (std::size_t index = 0; index < text.size();) {
        if (text[index] == '\x1b' && index + 1 < text.size() &&
            text[index + 1] == '[') {
            index += 2;
            while (index < text.size() &&
                   !(text[index] >= '@' && text[index] <= '~')) {
                ++index;
            }
            if (index < text.size()) ++index;
            continue;
        }
        result.push_back(text[index++]);
    }
    return result;
}

} // namespace

int main() {
    const auto help = invoke({});
    require(help.code == 0, "root help exit code");
    require(help.out.contains("tool        manage"),
            "root help contains tool family");

    const auto version = invoke({"--version"});
    require(version.code == 0, "version exit code");
    require(version.out == "star 0.1.0\n", "version output");

    const auto doctor = invoke({"doctor"});
    require(doctor.code == 0, "doctor exit code");
    require(doctor.out.contains("repository"), "doctor repository root");
    require(doctor.out.contains("status ok"), "doctor status");

    const auto tool = invoke({"tool", "--help"});
    require(tool.code == 0, "tool help exit code");
    require(tool.out.contains("star tool <list|info|add|remove|run>"),
            "tool help surface");

    const auto colored_help = invoke({"--color", "always", "--help"});
    require(colored_help.code == 0, "colored help exit code");
    require(colored_help.out.contains("\x1b[1;36mStar\x1b[0m"),
            "colored help contains brand styling");
    require(strip_ansi(colored_help.out) == help.out,
            "colored help preserves plain text");

    const auto plain_help = invoke({"--color", "always", "--plain", "--help"});
    require(!plain_help.out.contains("\x1b["), "plain disables styling");
    require(plain_help.out == help.out, "plain help is stable");

    const auto colored_list = invoke({
        "--color=always", "box", "backend", "list",
    });
    const auto plain_list = invoke({"--plain", "box", "backend", "list"});
    require(colored_list.code == 0, "colored JSON result exit code");
    require(colored_list.out.contains("\x1b[36m\"name\"\x1b[0m"),
            "JSON keys use the terminal palette");
    require(strip_ansi(colored_list.out) == plain_list.out,
            "colored JSON result preserves plain text");

    const auto json = invoke({
        "--color", "always", "--json", "box", "backend", "list",
    });
    require(json.code == 0, "NDJSON exit code");
    require(!json.out.contains("\x1b["), "NDJSON never contains styling");
    require(json.out.contains("\"kind\":\"result\""),
            "NDJSON result remains structured");

    const auto interface = invoke({"--color", "always", "interface", "version"});
    require(interface.code == 0, "interface version exit code");
    require(!interface.out.contains("\x1b["),
            "interface protocol never contains styling");

    const auto unknown = invoke({"missing"});
    require(unknown.code == 64, "unknown command exit code");
    require(unknown.err.contains("unknown command"), "unknown command error");

    const auto colored_unknown = invoke({"--color", "always", "missing"});
    require(colored_unknown.code == 64, "colored unknown command exit code");
    require(colored_unknown.err.contains("\x1b[31merror:\x1b[0m"),
            "errors use the terminal palette");
    require(strip_ansi(colored_unknown.err) == unknown.err,
            "colored error preserves plain text");

    const auto bad_color = invoke({"--color", "sometimes", "--help"});
    require(bad_color.code == 64, "invalid color mode exit code");
    require(bad_color.err.contains("expected auto, always, or never"),
            "invalid color mode guidance");
}
