export module star.lua.runtime;

import std;
import mcpplibs.capi.lua;
import star.capability.dispatcher;
import star.extension.manifest;
import star.runtime.context;
import star.runtime.event;
import star.runtime.result;

namespace lua = mcpplibs::capi::lua;

export namespace star::lua_runtime {

namespace detail {

struct StateCloser {
    void operator()(lua::State* state) const {
        if (state) lua::close(state);
    }
};

using State = std::unique_ptr<lua::State, StateCloser>;

struct Invocation {
    const capability::Dispatcher* dispatcher;
    const runtime::ExecutionContext* context;
};

auto tool_error(std::string message) -> runtime::Result<runtime::Json> {
    return std::unexpected(runtime::Error{
        .code = runtime::ErrorCode::tool,
        .message = std::move(message),
    });
}

void push_json(lua::State* state, const runtime::Json& value,
               std::size_t depth = 0) {
    if (depth > 64) {
        lua::pushnil(state);
        return;
    }
    if (value.is_null()) {
        lua::pushnil(state);
    } else if (value.is_boolean()) {
        lua::pushboolean(state, value.get<bool>() ? 1 : 0);
    } else if (value.is_number_integer()) {
        lua::pushinteger(state, value.get<lua::Integer>());
    } else if (value.is_number_unsigned()) {
        const auto number = value.get<std::uint64_t>();
        if (number <= static_cast<std::uint64_t>(
                          std::numeric_limits<lua::Integer>::max())) {
            lua::pushinteger(state, static_cast<lua::Integer>(number));
        } else {
            lua::pushnumber(state, static_cast<lua::Number>(number));
        }
    } else if (value.is_number_float()) {
        lua::pushnumber(state, value.get<lua::Number>());
    } else if (value.is_string()) {
        const auto& text = value.get_ref<const std::string&>();
        lua::pushlstring(state, text.data(), text.size());
    } else if (value.is_array()) {
        lua::createtable(state, static_cast<int>(value.size()), 0);
        for (std::size_t index = 0; index < value.size(); ++index) {
            push_json(state, value[index], depth + 1);
            lua::seti(state, -2, static_cast<lua::Integer>(index + 1));
        }
    } else {
        lua::createtable(state, 0, static_cast<int>(value.size()));
        for (auto item = value.begin(); item != value.end(); ++item) {
            push_json(state, item.value(), depth + 1);
            lua::setfield(state, -2, item.key().c_str());
        }
    }
}

auto read_json(lua::State* state, int index, std::size_t depth,
               std::unordered_set<const void*>& visited)
    -> runtime::Result<runtime::Json> {
    if (depth > 64) {
        return std::unexpected(runtime::Error{
            .code = runtime::ErrorCode::invalid_input,
            .message = "Lua value exceeds JSON nesting limit",
        });
    }
    switch (lua::type(state, index)) {
    case lua::TNIL:
        return runtime::Json(nullptr);
    case lua::TBOOLEAN:
        return runtime::Json(lua::toboolean(state, index) != 0);
    case lua::TNUMBER:
        if (lua::isinteger(state, index)) {
            return runtime::Json(lua::tointeger(state, index));
        }
        return runtime::Json(lua::tonumber(state, index));
    case lua::TSTRING: {
        unsigned long long length = 0;
        const auto* value = lua::tolstring(state, index, &length);
        return runtime::Json(std::string(value, length));
    }
    case lua::TTABLE: {
        const auto identity = lua::topointer(state, index);
        if (!visited.insert(identity).second) {
            return std::unexpected(runtime::Error{
                .code = runtime::ErrorCode::invalid_input,
                .message = "cyclic Lua table cannot be converted to JSON",
            });
        }
        const auto absolute = lua::absindex(state, index);
        std::vector<std::pair<std::size_t, runtime::Json>> array_items;
        runtime::Json object = runtime::Json::object();
        bool has_array_keys = false;
        bool has_object_keys = false;
        lua::pushnil(state);
        while (lua::next(state, absolute) != 0) {
            auto child = read_json(state, -1, depth + 1, visited);
            if (!child) {
                lua::pop(state, 2);
                visited.erase(identity);
                return std::unexpected(child.error());
            }
            if (lua::isinteger(state, -2)) {
                const auto key = lua::tointeger(state, -2);
                if (key <= 0) {
                    lua::pop(state, 2);
                    visited.erase(identity);
                    return std::unexpected(runtime::Error{
                        .code = runtime::ErrorCode::invalid_input,
                        .message = "Lua array keys must be positive integers",
                    });
                }
                has_array_keys = true;
                array_items.emplace_back(static_cast<std::size_t>(key),
                                         std::move(*child));
            } else if (lua::type(state, -2) == lua::TSTRING) {
                has_object_keys = true;
                const auto* key = lua::tostring(state, -2);
                object[key] = std::move(*child);
            } else {
                lua::pop(state, 2);
                visited.erase(identity);
                return std::unexpected(runtime::Error{
                    .code = runtime::ErrorCode::invalid_input,
                    .message = "Lua object keys must be strings",
                });
            }
            lua::pop(state, 1);
        }
        visited.erase(identity);
        if (has_array_keys && has_object_keys) {
            return std::unexpected(runtime::Error{
                .code = runtime::ErrorCode::invalid_input,
                .message = "mixed Lua table keys cannot be converted to JSON",
            });
        }
        if (!has_array_keys) return object;
        std::ranges::sort(array_items, {}, &decltype(array_items)::value_type::first);
        runtime::Json array = runtime::Json::array();
        for (std::size_t index = 0; index < array_items.size(); ++index) {
            if (array_items[index].first != index + 1) {
                return std::unexpected(runtime::Error{
                    .code = runtime::ErrorCode::invalid_input,
                    .message = "Lua array keys must be contiguous",
                });
            }
            array.push_back(std::move(array_items[index].second));
        }
        return array;
    }
    default:
        return std::unexpected(runtime::Error{
            .code = runtime::ErrorCode::invalid_input,
            .message = "Lua value is not representable as JSON",
        });
    }
}

auto read_json(lua::State* state, int index)
    -> runtime::Result<runtime::Json> {
    std::unordered_set<const void*> visited;
    return read_json(state, index, 0, visited);
}

auto invocation(lua::State* state) -> Invocation* {
    return static_cast<Invocation*>(
        lua::touserdata(state, lua::upvalueindex(1)));
}

int raw_call(lua::State* state) {
    auto* call = invocation(state);
    if (!call || !call->dispatcher || !call->context ||
        lua::type(state, 2) != lua::TSTRING) {
        lua::pushnil(state);
        lua::pushstring(state, "ctx:call requires a capability string");
        return 2;
    }
    auto args = read_json(state, 3);
    if (!args) {
        lua::pushnil(state);
        lua::pushstring(state, args.error().message.c_str());
        return 2;
    }
    const auto result = call->dispatcher->invoke(
        lua::tostring(state, 2), *args, *call->context);
    if (!result) {
        lua::pushnil(state);
        const auto message = std::string(runtime::error_code_name(result.error().code)) +
                             ": " + result.error().message;
        lua::pushstring(state, message.c_str());
        return 2;
    }
    push_json(state, *result);
    lua::pushnil(state);
    return 2;
}

int log(lua::State* state) {
    auto* call = invocation(state);
    if (!call || !call->context || lua::type(state, 2) != lua::TSTRING ||
        lua::type(state, 3) != lua::TSTRING) {
        return 0;
    }
    call->context->emit(runtime::EventKind::log, runtime::Json{
        {"level", lua::tostring(state, 2)},
        {"message", lua::tostring(state, 3)},
    });
    return 0;
}

int progress(lua::State* state) {
    auto* call = invocation(state);
    if (!call || !call->context || lua::type(state, 2) != lua::TSTRING ||
        !lua::isnumber(state, 3)) {
        return 0;
    }
    const auto percent = std::clamp<int>(
        static_cast<int>(lua::tointeger(state, 3)), 0, 100);
    const auto* message = lua::type(state, 4) == lua::TSTRING
        ? lua::tostring(state, 4)
        : "";
    call->context->emit(runtime::EventKind::progress, runtime::Json{
        {"phase", lua::tostring(state, 2)},
        {"percent", percent},
        {"message", message},
    });
    return 0;
}

void set_closure(lua::State* state, const char* name, lua::CFunction function,
                 Invocation* call) {
    lua::pushlightuserdata(state, call);
    lua::pushcclosure(state, function, 1);
    lua::setfield(state, -2, name);
}

auto push_context(lua::State* state, Invocation* call) -> runtime::Result<void> {
    lua::newtable(state);
    lua::pushstring(state, call->context->request_id.c_str());
    lua::setfield(state, -2, "id");
    lua::pushstring(state, call->context->trace_id.c_str());
    lua::setfield(state, -2, "trace_id");
    lua::pushstring(state, call->context->source.c_str());
    lua::setfield(state, -2, "caller");
    set_closure(state, "__call", raw_call, call);
    set_closure(state, "log", log, call);
    set_closure(state, "progress", progress, call);

    constexpr auto wrapper =
        "return function(self, capability, args) "
        "local value, err = self:__call(capability, args); "
        "if err ~= nil then error(err, 2) end; return value end";
    if (lua::L_loadstring(state, wrapper) != lua::OK ||
        lua::pcall(state, 0, 1, 0) != lua::OK) {
        const auto* message = lua::tostring(state, -1);
        const auto error = runtime::Error{
            .code = runtime::ErrorCode::tool,
            .message = message ? message : "failed to create ctx:call",
        };
        lua::pop(state, 1);
        return std::unexpected(error);
    }
    lua::setfield(state, -2, "call");
    return {};
}

void open_restricted_libraries(lua::State* state) {
    lua::L_requiref(state, "_G", lua::open_base, 1);
    lua::pop(state, 1);
    lua::L_requiref(state, "table", lua::open_table, 1);
    lua::pop(state, 1);
    lua::L_requiref(state, "string", lua::open_string, 1);
    lua::pop(state, 1);
    lua::L_requiref(state, "utf8", lua::open_utf8, 1);
    lua::pop(state, 1);
    lua::L_requiref(state, "math", lua::open_math, 1);
    lua::pop(state, 1);
    lua::pushnil(state);
    lua::setglobal(state, "dofile");
    lua::pushnil(state);
    lua::setglobal(state, "loadfile");
}

auto lua_message(lua::State* state, std::string_view prefix) -> std::string {
    const auto* message = lua::tostring(state, -1);
    return std::string(prefix) + (message ? message : "unknown Lua error");
}

} // namespace detail

class Runtime {
public:
    explicit Runtime(const capability::Dispatcher& dispatcher)
        : dispatcher_(dispatcher) {}

    auto invoke(const extension::Manifest& manifest, std::string_view command,
                const runtime::Json& args,
                const runtime::ExecutionContext& parent) const
        -> runtime::Result<runtime::Json> {
        const auto declared = std::ranges::find(
            manifest.commands, command, &extension::Command::name);
        if (declared == manifest.commands.end()) {
            return detail::tool_error("command is not declared: " +
                                      std::string(command));
        }

        auto context = parent.child(
            std::string(extension::kind_name(manifest.kind)) + "." +
            manifest.id + "/" + std::string(command));
        std::unordered_set<std::string> effective;
        for (const auto& permission : manifest.permissions) {
            if (parent.permissions.contains(permission)) {
                effective.insert(permission);
            }
        }
        context.permissions = std::move(effective);
        detail::Invocation invocation{&dispatcher_, &context};

        detail::State state(lua::L_newstate());
        if (!state) return detail::tool_error("cannot create Lua state");
        detail::open_restricted_libraries(state.get());

        if (lua::L_loadfile(state.get(), manifest.entry.string().c_str()) != lua::OK ||
            lua::pcall(state.get(), 0, 1, 0) != lua::OK) {
            return detail::tool_error(
                detail::lua_message(state.get(), "cannot load extension: "));
        }
        if (lua::type(state.get(), -1) != lua::TTABLE) {
            return detail::tool_error("extension entry must return a table");
        }
        const auto extension_index = lua::absindex(state.get(), -1);

        lua::getfield(state.get(), extension_index, "init");
        if (lua::type(state.get(), -1) == lua::TFUNCTION) {
            auto pushed = detail::push_context(state.get(), &invocation);
            if (!pushed) return std::unexpected(pushed.error());
            if (lua::pcall(state.get(), 1, 0, 0) != lua::OK) {
                return detail::tool_error(
                    detail::lua_message(state.get(), "extension init failed: "));
            }
        } else {
            lua::pop(state.get(), 1);
        }

        lua::getfield(state.get(), extension_index, "commands");
        if (lua::type(state.get(), -1) != lua::TTABLE) {
            return detail::tool_error("extension.commands must be a table");
        }
        lua::getfield(state.get(), -1, std::string(command).c_str());
        if (lua::type(state.get(), -1) != lua::TFUNCTION) {
            return detail::tool_error("declared command has no Lua handler: " +
                                      std::string(command));
        }
        auto pushed = detail::push_context(state.get(), &invocation);
        if (!pushed) return std::unexpected(pushed.error());
        detail::push_json(state.get(), args);
        if (lua::pcall(state.get(), 2, 1, 0) != lua::OK) {
            return detail::tool_error(
                detail::lua_message(state.get(), "command failed: "));
        }
        auto result = detail::read_json(state.get(), -1);
        if (!result) {
            return detail::tool_error("invalid command result: " +
                                      result.error().message);
        }
        return result;
    }

private:
    const capability::Dispatcher& dispatcher_;
};

} // namespace star::lua_runtime
