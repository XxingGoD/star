import std;
import star.process;
import star.runtime.result;

namespace {

void require(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

} // namespace

int main() {
    const auto invalid = star::process::run({});
    require(!invalid, "empty argv is rejected");
    require(invalid.error().code == star::runtime::ErrorCode::invalid_input,
            "empty argv has stable error code");

#ifdef _WIN32
    const auto result = star::process::run({
        .argv = {"where.exe", "cmd.exe"},
        .capture_output = true,
    });
#else
    const auto result = star::process::run({
        .argv = {"/bin/echo", "$(not-a-shell)", "star process"},
        .capture_output = true,
    });
#endif

    require(result.has_value(), "process starts");
    require(result->exit_code == 0, "process exit code");
    require(!result->output.empty(), "captured process output");
#ifndef _WIN32
    require(result->output.contains("$(not-a-shell)"),
            "arguments are not interpreted by a shell");
#endif
}
