import std;
import star.box.backend;
import star.box.capabilities;
import star.capability.dispatcher;
import star.runtime.context;
import star.runtime.event;
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
    const auto parsed = star::box::parse_ref("docker://local/fwlab");
    require(parsed.has_value(), "valid Box reference");
    require(parsed->backend == "docker", "backend parsed");
    require(parsed->authority == "local", "authority parsed");
    require(parsed->resource == "fwlab", "resource parsed");
    require(parsed->string() == "docker://local/fwlab", "reference round trip");
    require(!star::box::parse_ref("docker:fwlab"), "missing authority rejected");

    star::box::CliBackend docker("docker");
    const auto plan = docker.plan_exec(*parsed, {
        .argv = {"binwalk", "$(not-a-shell)", "firmware image.chk"},
        .workdir = "/work hub",
    });
    require(plan.has_value(), "Docker exec plan");
    require(plan->argv == std::vector<std::string>{
        "docker", "exec", "--workdir", "/work hub", "fwlab",
        "binwalk", "$(not-a-shell)", "firmware image.chk"},
        "exec plan preserves argv boundaries");

    auto backends = star::box::Registry::builtins(true);
    require(backends.find("docker") != nullptr, "Docker backend registered");
    require(backends.find("podman") != nullptr, "Podman backend registered");
    require(backends.find("fake") != nullptr, "Fake backend registered");

    star::capability::Registry capabilities;
    require(star::box::register_capabilities(capabilities, backends).has_value(),
            "Box capabilities registered");
    star::capability::Dispatcher dispatcher(capabilities);
    star::runtime::VectorEventSink events;
    star::runtime::ExecutionContext context{
        .request_id = "box-request",
        .trace_id = "box-trace",
        .source = "test",
        .permissions = {"box.exec", "box.inspect"},
        .events = &events,
    };
    const auto result = dispatcher.invoke("box.exec", star::runtime::Json{
        {"ref", "fake://local/test"},
        {"argv", {"echo", "star"}},
        {"capture", true},
    }, context);
    require(result.has_value(), "fake box.exec through dispatcher");
    require((*result)["exitCode"] == 0, "fake exec exit code");
    require((*result)["output"] == "echo star", "fake exec output");
}
