export module star.box.backend;

import std;
import star.process;
import star.runtime.result;

export namespace star::box {

struct BoxRef {
    std::string backend;
    std::string authority;
    std::string resource;

    auto string() const -> std::string {
        return backend + "://" + authority + "/" + resource;
    }
};

auto parse_ref(std::string_view value) -> runtime::Result<BoxRef> {
    const auto scheme_end = value.find("://");
    if (scheme_end == std::string_view::npos || scheme_end == 0) {
        return std::unexpected(runtime::Error{
            .code = runtime::ErrorCode::invalid_input,
            .message = "Box reference must use <backend>://<authority>/<resource>",
        });
    }
    const auto slash = value.find('/', scheme_end + 3);
    if (slash == std::string_view::npos || slash == scheme_end + 3 ||
        slash + 1 >= value.size()) {
        return std::unexpected(runtime::Error{
            .code = runtime::ErrorCode::invalid_input,
            .message = "Box reference requires authority and resource",
        });
    }
    const auto backend = value.substr(0, scheme_end);
    const auto valid_backend = std::ranges::all_of(backend, [](unsigned char ch) {
        return std::isalnum(ch) || ch == '+' || ch == '-' || ch == '.';
    });
    if (!valid_backend) {
        return std::unexpected(runtime::Error{
            .code = runtime::ErrorCode::invalid_input,
            .message = "invalid Box backend name",
        });
    }
    return BoxRef{
        .backend = std::string(backend),
        .authority = std::string(value.substr(scheme_end + 3,
                                              slash - scheme_end - 3)),
        .resource = std::string(value.substr(slash + 1)),
    };
}

struct ExecSpec {
    std::vector<std::string> argv;
    std::optional<std::filesystem::path> workdir;
    bool capture_output = false;
};

class Backend {
public:
    virtual ~Backend() = default;
    virtual auto name() const -> std::string_view = 0;
    virtual auto available() const -> bool = 0;
    virtual auto capabilities() const -> std::vector<std::string> = 0;
    virtual auto inspect(const BoxRef& ref) const
        -> runtime::Result<runtime::Json> = 0;
    virtual auto exec(const BoxRef& ref, const ExecSpec& spec) const
        -> runtime::Result<process::ProcessResult> = 0;
};

namespace detail {

auto executable_exists(std::string_view executable) -> bool {
    const auto* path_value = std::getenv("PATH");
    if (!path_value) return false;
#ifdef _WIN32
    constexpr char separator = ';';
    constexpr std::array<std::string_view, 2> suffixes{"", ".exe"};
#else
    constexpr char separator = ':';
    constexpr std::array<std::string_view, 1> suffixes{""};
#endif
    std::string_view path(path_value);
    while (true) {
        const auto end = path.find(separator);
        const auto directory = path.substr(0, end);
        for (const auto suffix : suffixes) {
            const auto candidate = std::filesystem::path(directory) /
                (std::string(executable) + std::string(suffix));
            std::error_code error;
            if (std::filesystem::is_regular_file(candidate, error)) return true;
        }
        if (end == std::string_view::npos) break;
        path.remove_prefix(end + 1);
    }
    return false;
}

auto backend_error(std::string message) -> runtime::Error {
    return {
        .code = runtime::ErrorCode::backend,
        .message = std::move(message),
    };
}

} // namespace detail

class CliBackend final : public Backend {
public:
    explicit CliBackend(std::string executable)
        : executable_(std::move(executable)) {}

    auto name() const -> std::string_view override { return executable_; }
    auto available() const -> bool override {
        return detail::executable_exists(executable_);
    }
    auto capabilities() const -> std::vector<std::string> override {
        return {"inspect", "exec"};
    }

    auto plan_exec(const BoxRef& ref, const ExecSpec& spec) const
        -> runtime::Result<process::ProcessSpec> {
        if (ref.backend != executable_) {
            return std::unexpected(runtime::Error{
                .code = runtime::ErrorCode::invalid_input,
                .message = "Box reference backend does not match adapter",
            });
        }
        if (ref.authority != "local") {
            return std::unexpected(runtime::Error{
                .code = runtime::ErrorCode::unsupported,
                .message = "v0.1 CLI backends only support local authority",
            });
        }
        if (spec.argv.empty()) {
            return std::unexpected(runtime::Error{
                .code = runtime::ErrorCode::invalid_input,
                .message = "box.exec requires a non-empty argv",
            });
        }
        process::ProcessSpec plan{
            .argv = {executable_, "exec"},
            .capture_output = spec.capture_output,
        };
        if (spec.workdir) {
            plan.argv.push_back("--workdir");
            plan.argv.push_back(spec.workdir->string());
        }
        plan.argv.push_back(ref.resource);
        plan.argv.insert(plan.argv.end(), spec.argv.begin(), spec.argv.end());
        return plan;
    }

    auto inspect(const BoxRef& ref) const
        -> runtime::Result<runtime::Json> override {
        if (ref.backend != executable_ || ref.authority != "local") {
            return std::unexpected(runtime::Error{
                .code = runtime::ErrorCode::unsupported,
                .message = "backend cannot inspect this Box reference",
            });
        }
        const auto result = process::run({
            .argv = {executable_, "inspect", ref.resource},
            .capture_output = true,
        });
        if (!result) return std::unexpected(result.error());
        if (result->exit_code != 0) {
            return std::unexpected(detail::backend_error(
                executable_ + " inspect failed: " + result->output));
        }
        try {
            return runtime::Json::parse(result->output);
        } catch (const std::exception&) {
            return runtime::Json{{"raw", result->output}};
        }
    }

    auto exec(const BoxRef& ref, const ExecSpec& spec) const
        -> runtime::Result<process::ProcessResult> override {
        auto plan = plan_exec(ref, spec);
        if (!plan) return std::unexpected(plan.error());
        return process::run(*plan);
    }

private:
    std::string executable_;
};

class FakeBackend final : public Backend {
public:
    auto name() const -> std::string_view override { return "fake"; }
    auto available() const -> bool override { return true; }
    auto capabilities() const -> std::vector<std::string> override {
        return {"inspect", "exec"};
    }
    auto inspect(const BoxRef& ref) const
        -> runtime::Result<runtime::Json> override {
        if (ref.backend != "fake") {
            return std::unexpected(runtime::Error{
                .code = runtime::ErrorCode::invalid_input,
                .message = "invalid fake Box reference",
            });
        }
        return runtime::Json{
            {"ref", ref.string()},
            {"state", "running"},
            {"backend_state", "fake-running"},
            {"capabilities", capabilities()},
        };
    }
    auto exec(const BoxRef&, const ExecSpec& spec) const
        -> runtime::Result<process::ProcessResult> override {
        if (spec.argv.empty()) {
            return std::unexpected(runtime::Error{
                .code = runtime::ErrorCode::invalid_input,
                .message = "fake exec requires argv",
            });
        }
        return process::ProcessResult{0, std::accumulate(
            std::next(spec.argv.begin()), spec.argv.end(), spec.argv.front(),
            [](std::string value, const std::string& arg) {
                value += " " + arg;
                return value;
            })};
    }
};

class Registry {
public:
    auto add(std::unique_ptr<Backend> backend) -> runtime::Result<void> {
        if (!backend) {
            return std::unexpected(runtime::Error{
                .code = runtime::ErrorCode::invalid_input,
                .message = "Box backend is required",
            });
        }
        const auto key = std::string(backend->name());
        if (backends_.contains(key)) {
            return std::unexpected(runtime::Error{
                .code = runtime::ErrorCode::invalid_input,
                .message = "duplicate Box backend: " + key,
            });
        }
        backends_.emplace(key, std::move(backend));
        return {};
    }

    auto find(std::string_view name) const -> const Backend* {
        const auto found = backends_.find(std::string(name));
        return found == backends_.end() ? nullptr : found->second.get();
    }

    auto list() const -> std::vector<std::reference_wrapper<const Backend>> {
        std::vector<std::reference_wrapper<const Backend>> result;
        for (const auto& [name, backend] : backends_) result.emplace_back(*backend);
        std::ranges::sort(result, {}, [](const auto& value) {
            return value.get().name();
        });
        return result;
    }

    static auto builtins(bool include_fake = false) -> Registry {
        Registry registry;
        registry.add(std::make_unique<CliBackend>("docker"));
        registry.add(std::make_unique<CliBackend>("podman"));
        if (include_fake) registry.add(std::make_unique<FakeBackend>());
        return registry;
    }

private:
    std::unordered_map<std::string, std::unique_ptr<Backend>> backends_;
};

} // namespace star::box
