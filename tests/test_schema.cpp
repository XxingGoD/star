import std;
import star.extension.manifest;
import star.schema;
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
    const auto path = std::filesystem::temp_directory_path() /
                      "star-command-input-schema.json";
    {
        std::ofstream output(path);
        output << R"({
  "type": "object",
  "required": ["path"],
  "additionalProperties": false,
  "properties": {
    "path": {"type": "string", "minLength": 1},
    "depth": {"type": "integer", "minimum": 1, "default": 2},
    "extract": {"type": "boolean", "default": false},
    "signature": {"type": "array", "items": {"type": "string"}}
  }
})";
    }
    star::extension::Command command{
        .name = "scan",
        .input_schema = path,
        .cli_positionals = {"path"},
    };

    const std::array<std::string_view, 8> projected{
        "--", "firmware.chk", "--depth", "3", "--extract",
        "--signature", "trx", "--signature"};
    const std::array<std::string_view, 1> final_signature{"squashfs"};
    std::vector<std::string_view> args(projected.begin(), projected.end());
    args.insert(args.end(), final_signature.begin(), final_signature.end());
    const auto input = star::schema::command_input(command, args);
    require(input.has_value(), input ? "projected input" : input.error().message);
    require((*input)["path"] == "firmware.chk", "positional mapped");
    require((*input)["depth"] == 3, "integer option converted");
    require((*input)["extract"] == true, "boolean option converted");
    require((*input)["signature"].size() == 2, "array option repeated");

    const std::array<std::string_view, 2> json_args{
        "--input-json", R"({"path":"image.bin","extract":true})"};
    const auto json_input = star::schema::command_input(command, json_args);
    require(json_input.has_value(), "JSON input valid");
    require((*json_input)["depth"] == 2, "schema default applied");

    const std::array<std::string_view, 2> invalid_args{"--", ""};
    require(!star::schema::command_input(command, invalid_args),
            "minLength violation rejected");
    std::filesystem::remove(path);
}
