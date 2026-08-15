export module star.schema;

import std;
import star.extension.manifest;
import star.runtime.result;

export namespace star::schema {

namespace detail {

auto invalid(std::string message) -> runtime::Result<void> {
    return std::unexpected(runtime::Error{
        .code = runtime::ErrorCode::invalid_input,
        .message = std::move(message),
    });
}

auto invalid_value(std::string message) -> runtime::Result<runtime::Json> {
    return std::unexpected(runtime::Error{
        .code = runtime::ErrorCode::invalid_input,
        .message = std::move(message),
    });
}

auto type_matches(std::string_view type, const runtime::Json& value) -> bool {
    if (type == "object") return value.is_object();
    if (type == "array") return value.is_array();
    if (type == "string") return value.is_string();
    if (type == "integer") return value.is_number_integer();
    if (type == "number") return value.is_number();
    if (type == "boolean") return value.is_boolean();
    if (type == "null") return value.is_null();
    return false;
}

auto validate_node(const runtime::Json& schema, runtime::Json& value,
                   std::string path) -> runtime::Result<void> {
    if (!schema.is_object()) return invalid(path + ": schema must be an object");
    if (schema.contains("$ref")) {
        return std::unexpected(runtime::Error{
            .code = runtime::ErrorCode::unsupported,
            .message = path + ": JSON Schema $ref is not supported in v0.1",
        });
    }
    if (const auto type = schema.find("type"); type != schema.end()) {
        if (!type->is_string() || !type_matches(type->get<std::string>(), value)) {
            return invalid(path + ": expected type " +
                           (type->is_string() ? type->get<std::string>() : "<invalid>"));
        }
    }
    if (const auto choices = schema.find("enum"); choices != schema.end()) {
        if (!choices->is_array() ||
            std::ranges::none_of(*choices, [&](const auto& item) {
                return item == value;
            })) {
            return invalid(path + ": value is not in enum");
        }
    }

    if (value.is_object()) {
        if (const auto properties = schema.find("properties");
            properties != schema.end()) {
            if (!properties->is_object()) {
                return invalid(path + ": properties must be an object");
            }
            for (auto property = properties->begin(); property != properties->end();
                 ++property) {
                if (!value.contains(property.key()) &&
                    property.value().contains("default")) {
                    value[property.key()] = property.value()["default"];
                }
            }
        }
        if (const auto required = schema.find("required"); required != schema.end()) {
            if (!required->is_array()) return invalid(path + ": required must be an array");
            for (const auto& name : *required) {
                if (!name.is_string() || !value.contains(name.get<std::string>())) {
                    return invalid(path + ": missing required property " +
                                   (name.is_string() ? name.get<std::string>() : "<invalid>"));
                }
            }
        }
        const auto properties = schema.find("properties");
        for (auto item = value.begin(); item != value.end(); ++item) {
            if (properties != schema.end() && properties->is_object() &&
                properties->contains(item.key())) {
                auto result = validate_node((*properties)[item.key()], item.value(),
                                            path + "." + item.key());
                if (!result) return result;
            } else if (schema.value("additionalProperties", true) == false) {
                return invalid(path + ": unknown property " + item.key());
            }
        }
    }

    if (value.is_array()) {
        if (const auto minimum = schema.find("minItems"); minimum != schema.end() &&
            minimum->is_number_unsigned() && value.size() < minimum->get<std::size_t>()) {
            return invalid(path + ": array is shorter than minItems");
        }
        if (const auto items = schema.find("items"); items != schema.end()) {
            for (std::size_t index = 0; index < value.size(); ++index) {
                auto result = validate_node(*items, value[index],
                                            std::format("{}[{}]", path, index));
                if (!result) return result;
            }
        }
    }

    if (value.is_string()) {
        const auto length = value.get_ref<const std::string&>().size();
        if (const auto minimum = schema.find("minLength"); minimum != schema.end() &&
            minimum->is_number_unsigned() && length < minimum->get<std::size_t>()) {
            return invalid(path + ": string is shorter than minLength");
        }
        if (const auto maximum = schema.find("maxLength"); maximum != schema.end() &&
            maximum->is_number_unsigned() && length > maximum->get<std::size_t>()) {
            return invalid(path + ": string is longer than maxLength");
        }
    }

    if (value.is_number()) {
        const auto number = value.get<double>();
        if (const auto minimum = schema.find("minimum"); minimum != schema.end() &&
            minimum->is_number() && number < minimum->get<double>()) {
            return invalid(path + ": number is below minimum");
        }
        if (const auto maximum = schema.find("maximum"); maximum != schema.end() &&
            maximum->is_number() && number > maximum->get<double>()) {
            return invalid(path + ": number is above maximum");
        }
    }
    return {};
}

auto property_name(std::string_view option, const runtime::Json& properties)
    -> std::optional<std::string> {
    auto candidate = std::string(option);
    if (properties.contains(candidate)) return candidate;
    std::ranges::replace(candidate, '-', '_');
    if (properties.contains(candidate)) return candidate;
    return std::nullopt;
}

auto scalar(std::string_view text, const runtime::Json& property)
    -> runtime::Result<runtime::Json> {
    const auto type = property.value("type", "string");
    try {
        if (type == "string") return runtime::Json(std::string(text));
        if (type == "integer") {
            std::size_t used = 0;
            const auto value = std::stoll(std::string(text), &used);
            if (used != text.size()) return invalid_value("invalid integer: " + std::string(text));
            return runtime::Json(value);
        }
        if (type == "number") {
            std::size_t used = 0;
            const auto value = std::stod(std::string(text), &used);
            if (used != text.size()) return invalid_value("invalid number: " + std::string(text));
            return runtime::Json(value);
        }
        if (type == "boolean") {
            if (text == "true") return runtime::Json(true);
            if (text == "false") return runtime::Json(false);
            return invalid_value("invalid boolean: " + std::string(text));
        }
    } catch (const std::exception&) {
        return invalid_value("invalid " + type + ": " + std::string(text));
    }
    return std::unexpected(runtime::Error{
        .code = runtime::ErrorCode::unsupported,
        .message = "CLI projection does not support property type " + type,
    });
}

} // namespace detail

auto load(const std::filesystem::path& path) -> runtime::Result<runtime::Json> {
    std::ifstream input(path);
    if (!input) {
        return std::unexpected(runtime::Error{
            .code = runtime::ErrorCode::invalid_input,
            .message = "cannot open JSON Schema: " + path.string(),
        });
    }
    try {
        return runtime::Json::parse(input);
    } catch (const std::exception& error) {
        return std::unexpected(runtime::Error{
            .code = runtime::ErrorCode::invalid_input,
            .message = "invalid JSON Schema " + path.string() + ": " + error.what(),
        });
    }
}

auto validate(const runtime::Json& schema, runtime::Json& value)
    -> runtime::Result<void> {
    return detail::validate_node(schema, value, "$");
}

auto command_input(const extension::Command& command,
                   std::span<const std::string_view> args)
    -> runtime::Result<runtime::Json> {
    auto schema = load(command.input_schema);
    if (!schema) return std::unexpected(schema.error());
    runtime::Json value = runtime::Json::object();

    if (!args.empty() && args.front() == "--input-json") {
        if (args.size() != 2) {
            return std::unexpected(runtime::Error{
                .code = runtime::ErrorCode::invalid_input,
                .message = "--input-json requires exactly one JSON value",
            });
        }
        try {
            value = runtime::Json::parse(args[1]);
        } catch (const std::exception& error) {
            return std::unexpected(runtime::Error{
                .code = runtime::ErrorCode::invalid_input,
                .message = "invalid --input-json: " + std::string(error.what()),
            });
        }
    } else {
        if (!args.empty() && args.front() == "--") args = args.subspan(1);
        const auto properties = schema->value("properties", runtime::Json::object());
        std::size_t positional = 0;
        bool options = true;
        for (std::size_t index = 0; index < args.size(); ++index) {
            const auto token = args[index];
            if (options && token == "--") {
                options = false;
                continue;
            }
            if (options && token.starts_with("--")) {
                auto option = token.substr(2);
                bool negated = false;
                if (option.starts_with("no-")) {
                    negated = true;
                    option.remove_prefix(3);
                }
                const auto name = detail::property_name(option, properties);
                if (!name) {
                    return std::unexpected(runtime::Error{
                        .code = runtime::ErrorCode::invalid_input,
                        .message = "unknown command option --" + std::string(option),
                    });
                }
                const auto& property = properties[*name];
                const auto property_type = property.value("type", "string");
                if (property_type == "boolean") {
                    if (value.contains(*name)) {
                        return std::unexpected(runtime::Error{
                            .code = runtime::ErrorCode::invalid_input,
                            .message = "duplicate option --" + std::string(option),
                        });
                    }
                    value[*name] = !negated;
                    continue;
                }
                if (negated || index + 1 >= args.size()) {
                    return std::unexpected(runtime::Error{
                        .code = runtime::ErrorCode::invalid_input,
                        .message = "option --" + std::string(option) + " requires a value",
                    });
                }
                const auto item_schema = property_type == "array"
                    ? property.value("items", runtime::Json{{"type", "string"}})
                    : property;
                auto parsed = detail::scalar(args[++index], item_schema);
                if (!parsed) return std::unexpected(parsed.error());
                if (property_type == "array") {
                    if (!value.contains(*name)) value[*name] = runtime::Json::array();
                    value[*name].push_back(std::move(*parsed));
                } else {
                    if (value.contains(*name)) {
                        return std::unexpected(runtime::Error{
                            .code = runtime::ErrorCode::invalid_input,
                            .message = "duplicate option --" + std::string(option),
                        });
                    }
                    value[*name] = std::move(*parsed);
                }
                continue;
            }

            if (positional >= command.cli_positionals.size()) {
                return std::unexpected(runtime::Error{
                    .code = runtime::ErrorCode::invalid_input,
                    .message = "too many positional command arguments",
                });
            }
            const auto& name = command.cli_positionals[positional++];
            if (!properties.contains(name)) {
                return std::unexpected(runtime::Error{
                    .code = runtime::ErrorCode::invalid_input,
                    .message = "cli_positionals refers to unknown property " + name,
                });
            }
            auto parsed = detail::scalar(token, properties[name]);
            if (!parsed) return std::unexpected(parsed.error());
            value[name] = std::move(*parsed);
        }
    }

    auto valid = validate(*schema, value);
    if (!valid) return std::unexpected(valid.error());
    return value;
}

} // namespace star::schema
