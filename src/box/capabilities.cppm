export module star.box.capabilities;

import std;
import star.box.backend;
import star.capability.dispatcher;
import star.runtime.result;

export namespace star::box {

auto register_capabilities(capability::Registry& capabilities,
                           const Registry& backends) -> runtime::Result<void> {
    auto inspect = capabilities.add({
        .name = "box.inspect",
        .description = "Inspect a Box through its selected backend",
        .required_permissions = {"box.inspect"},
        .handler = [&backends](const auto&, const runtime::Json& args)
            -> runtime::Result<runtime::Json> {
            if (!args.is_object() || !args.contains("ref") ||
                !args["ref"].is_string()) {
                return std::unexpected(runtime::Error{
                    .code = runtime::ErrorCode::invalid_input,
                    .message = "box.inspect requires string property ref",
                });
            }
            auto ref = parse_ref(args["ref"].get<std::string>());
            if (!ref) return std::unexpected(ref.error());
            const auto* backend = backends.find(ref->backend);
            if (!backend) {
                return std::unexpected(runtime::Error{
                    .code = runtime::ErrorCode::not_found,
                    .message = "Box backend not found: " + ref->backend,
                });
            }
            return backend->inspect(*ref);
        },
    });
    if (!inspect) return inspect;

    return capabilities.add({
        .name = "box.exec",
        .description = "Execute an argv vector inside a Box",
        .required_permissions = {"box.exec"},
        .handler = [&backends](const auto&, const runtime::Json& args)
            -> runtime::Result<runtime::Json> {
            if (!args.is_object() || !args.contains("ref") ||
                !args["ref"].is_string() || !args.contains("argv") ||
                !args["argv"].is_array()) {
                return std::unexpected(runtime::Error{
                    .code = runtime::ErrorCode::invalid_input,
                    .message = "box.exec requires ref and argv",
                });
            }
            auto ref = parse_ref(args["ref"].get<std::string>());
            if (!ref) return std::unexpected(ref.error());
            const auto* backend = backends.find(ref->backend);
            if (!backend) {
                return std::unexpected(runtime::Error{
                    .code = runtime::ErrorCode::not_found,
                    .message = "Box backend not found: " + ref->backend,
                });
            }
            ExecSpec spec{
                .argv = args["argv"].get<std::vector<std::string>>(),
                .capture_output = args.value("capture", false),
            };
            if (const auto workdir = args.find("workdir");
                workdir != args.end() && workdir->is_string()) {
                spec.workdir = workdir->get<std::string>();
            }
            auto result = backend->exec(*ref, spec);
            if (!result) return std::unexpected(result.error());
            return runtime::Json{
                {"exitCode", result->exit_code},
                {"output", result->output},
            };
        },
    });
}

} // namespace star::box
