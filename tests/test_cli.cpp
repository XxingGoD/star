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

} // namespace

int main() {
    const auto help = invoke({});
    require(help.code == 0, "root help exit code");
    require(help.out.contains("tool        discover"),
            "root help contains tool family");

    const auto version = invoke({"--version"});
    require(version.code == 0, "version exit code");
    require(version.out == "star 0.1.0\n", "version output");

    const auto doctor = invoke({"doctor"});
    require(doctor.code == 0, "doctor exit code");
    require(doctor.out.contains("status ok"), "doctor status");

    const auto tool = invoke({"tool", "--help"});
    require(tool.code == 0, "tool help exit code");
    require(tool.out.contains("star tool <list|info|run>"), "tool help surface");

    const auto unknown = invoke({"missing"});
    require(unknown.code == 64, "unknown command exit code");
    require(unknown.err.contains("unknown command"), "unknown command error");
}
