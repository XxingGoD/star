export module star.capability.dispatcher;

import std;
import star.runtime.context;
import star.runtime.event;
import star.runtime.result;

export namespace star::capability {

using runtime::ExecutionContext;
using runtime::Json;
using runtime::Result;

using Handler = std::function<Result<Json>(const ExecutionContext&, const Json&)>;

struct CapabilitySpec {
    std::string name;
    std::string description;
    std::vector<std::string> required_permissions;
    Handler handler;
};

class Registry {
public:
    auto add(CapabilitySpec spec) -> Result<void> {
        if (spec.name.empty() || !spec.handler) {
            return std::unexpected(runtime::Error{
                .code = runtime::ErrorCode::invalid_input,
                .message = "capability name and handler are required",
            });
        }
        if (specs_.contains(spec.name)) {
            return std::unexpected(runtime::Error{
                .code = runtime::ErrorCode::invalid_input,
                .message = "duplicate capability: " + spec.name,
            });
        }
        specs_.emplace(spec.name, std::move(spec));
        return {};
    }

    auto find(std::string_view name) const -> const CapabilitySpec* {
        const auto found = specs_.find(std::string(name));
        return found == specs_.end() ? nullptr : &found->second;
    }

    auto list() const -> std::vector<std::reference_wrapper<const CapabilitySpec>> {
        std::vector<std::reference_wrapper<const CapabilitySpec>> result;
        result.reserve(specs_.size());
        for (const auto& [name, spec] : specs_) result.emplace_back(spec);
        std::ranges::sort(result, {}, [](const auto& item) {
            return item.get().name;
        });
        return result;
    }

private:
    std::unordered_map<std::string, CapabilitySpec> specs_;
};

class Dispatcher {
public:
    explicit Dispatcher(const Registry& registry) : registry_(registry) {}

    auto invoke(std::string_view name, const Json& args,
                const ExecutionContext& parent) const -> Result<Json> {
        if (parent.is_cancelled()) {
            return std::unexpected(runtime::Error{
                .code = runtime::ErrorCode::cancelled,
                .message = "request was cancelled",
            });
        }
        if (parent.call_chain.size() >= parent.max_call_depth) {
            return std::unexpected(runtime::Error{
                .code = runtime::ErrorCode::internal,
                .message = "maximum capability call depth exceeded",
            });
        }
        if (std::ranges::contains(parent.call_chain, name)) {
            return std::unexpected(runtime::Error{
                .code = runtime::ErrorCode::call_cycle,
                .message = "capability call cycle detected at " + std::string(name),
            });
        }

        const auto* spec = registry_.find(name);
        if (!spec) {
            return std::unexpected(runtime::Error{
                .code = runtime::ErrorCode::not_found,
                .message = "capability not found: " + std::string(name),
            });
        }
        for (const auto& permission : spec->required_permissions) {
            if (!parent.permissions.contains(permission)) {
                return std::unexpected(runtime::Error{
                    .code = runtime::ErrorCode::permission,
                    .message = "missing permission: " + permission,
                });
            }
        }

        auto child = parent.child(spec->name);
        try {
            return spec->handler(child, args);
        } catch (const std::exception& error) {
            return std::unexpected(runtime::Error{
                .code = runtime::ErrorCode::internal,
                .message = "capability threw an exception: " +
                           std::string(error.what()),
            });
        }
    }

    auto dispatch(std::string_view name, const Json& args,
                  const ExecutionContext& context) const -> int {
        const auto result = invoke(name, args, context);
        if (!result) {
            context.emit(runtime::EventKind::error, Json{
                {"code", runtime::error_code_name(result.error().code)},
                {"message", result.error().message},
                {"hint", result.error().hint},
                {"recoverable", result.error().recoverable},
            });
            context.emit(runtime::EventKind::result, Json{
                {"exitCode", 1},
                {"success", false},
            });
            return 1;
        }

        context.emit(runtime::EventKind::result, Json{
            {"exitCode", 0},
            {"success", true},
            {"data", *result},
        });
        return 0;
    }

private:
    const Registry& registry_;
};

} // namespace star::capability
