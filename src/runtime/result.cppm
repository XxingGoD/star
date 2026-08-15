export module star.runtime.result;

import std;
import nlohmann.json;

export namespace star::runtime {

using Json = nlohmann::json;

enum class ErrorCode {
    invalid_input,
    not_found,
    unsupported,
    permission,
    policy_denied,
    backend,
    tool,
    cancelled,
    timeout,
    call_cycle,
    internal,
};

struct Error {
    ErrorCode code = ErrorCode::internal;
    std::string message;
    std::string hint;
    bool recoverable = false;
};

auto error_code_name(ErrorCode code) -> std::string_view {
    switch (code) {
    case ErrorCode::invalid_input: return "E_INVALID_INPUT";
    case ErrorCode::not_found: return "E_NOT_FOUND";
    case ErrorCode::unsupported: return "E_UNSUPPORTED";
    case ErrorCode::permission: return "E_PERMISSION";
    case ErrorCode::policy_denied: return "E_POLICY_DENIED";
    case ErrorCode::backend: return "E_BACKEND";
    case ErrorCode::tool: return "E_TOOL";
    case ErrorCode::cancelled: return "E_CANCELLED";
    case ErrorCode::timeout: return "E_TIMEOUT";
    case ErrorCode::call_cycle: return "E_CALL_CYCLE";
    case ErrorCode::internal: return "E_INTERNAL";
    }
    return "E_INTERNAL";
}

template <typename T>
using Result = std::expected<T, Error>;

} // namespace star::runtime
