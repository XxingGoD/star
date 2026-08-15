module;

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <cerrno>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

export module star.process;

import std;
import star.runtime.result;

export namespace star::process {

struct ProcessSpec {
    std::vector<std::string> argv;
    std::optional<std::filesystem::path> workdir;
    bool capture_output = false;
};

struct ProcessResult {
    int exit_code = 0;
    std::string output;
};

namespace detail {

#ifdef _WIN32

auto utf8_to_wide(std::string_view value) -> std::wstring {
    if (value.empty()) return {};
    const auto count = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), nullptr, 0);
    if (count <= 0) return {};
    std::wstring result(static_cast<std::size_t>(count), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                        static_cast<int>(value.size()), result.data(), count);
    return result;
}

auto quote_windows_arg(std::wstring_view value) -> std::wstring {
    if (value.find_first_of(L" \t\"") == std::wstring_view::npos) {
        return std::wstring(value);
    }
    std::wstring result = L"\"";
    std::size_t slashes = 0;
    for (const auto ch : value) {
        if (ch == L'\\') {
            ++slashes;
            continue;
        }
        if (ch == L'\"') {
            result.append(slashes * 2 + 1, L'\\');
            result.push_back(L'\"');
        } else {
            result.append(slashes, L'\\');
            result.push_back(ch);
        }
        slashes = 0;
    }
    result.append(slashes * 2, L'\\');
    result.push_back(L'\"');
    return result;
}

auto command_line(const std::vector<std::string>& argv) -> std::wstring {
    std::wstring result;
    for (std::size_t index = 0; index < argv.size(); ++index) {
        if (index != 0) result.push_back(L' ');
        result += quote_windows_arg(utf8_to_wide(argv[index]));
    }
    return result;
}

auto windows_error(std::string message) -> runtime::Error {
    return {
        .code = runtime::ErrorCode::internal,
        .message = std::move(message) + " (win32=" +
                   std::to_string(GetLastError()) + ")",
    };
}

#endif

} // namespace detail

auto run(const ProcessSpec& spec) -> runtime::Result<ProcessResult> {
    if (spec.argv.empty() || spec.argv.front().empty()) {
        return std::unexpected(runtime::Error{
            .code = runtime::ErrorCode::invalid_input,
            .message = "process argv must contain an executable",
        });
    }

#ifdef _WIN32
    SECURITY_ATTRIBUTES security{
        .nLength = sizeof(SECURITY_ATTRIBUTES),
        .lpSecurityDescriptor = nullptr,
        .bInheritHandle = TRUE,
    };
    HANDLE read_pipe = nullptr;
    HANDLE write_pipe = nullptr;
    if (spec.capture_output) {
        if (!CreatePipe(&read_pipe, &write_pipe, &security, 0)) {
            return std::unexpected(detail::windows_error("CreatePipe failed"));
        }
        SetHandleInformation(read_pipe, HANDLE_FLAG_INHERIT, 0);
    }

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    if (spec.capture_output) {
        startup.dwFlags |= STARTF_USESTDHANDLES;
        startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
        startup.hStdOutput = write_pipe;
        startup.hStdError = write_pipe;
    }
    PROCESS_INFORMATION process{};
    auto command = detail::command_line(spec.argv);
    const auto workdir = spec.workdir ? spec.workdir->wstring() : std::wstring{};
    const auto created = CreateProcessW(
        nullptr, command.data(), nullptr, nullptr,
        spec.capture_output ? TRUE : FALSE, 0, nullptr,
        workdir.empty() ? nullptr : workdir.c_str(), &startup, &process);
    if (write_pipe) CloseHandle(write_pipe);
    if (!created) {
        if (read_pipe) CloseHandle(read_pipe);
        return std::unexpected(detail::windows_error("CreateProcessW failed"));
    }

    std::string output;
    if (read_pipe) {
        std::array<char, 4096> buffer{};
        DWORD count = 0;
        while (ReadFile(read_pipe, buffer.data(),
                        static_cast<DWORD>(buffer.size()), &count, nullptr) &&
               count != 0) {
            output.append(buffer.data(), count);
        }
        CloseHandle(read_pipe);
    }
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD exit_code = 1;
    GetExitCodeProcess(process.hProcess, &exit_code);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return ProcessResult{static_cast<int>(exit_code), std::move(output)};
#else
    int output_pipe[2] = {-1, -1};
    if (spec.capture_output && ::pipe(output_pipe) != 0) {
        return std::unexpected(runtime::Error{
            .code = runtime::ErrorCode::internal,
            .message = "pipe failed: " + std::string(std::strerror(errno)),
        });
    }

    const auto child = ::fork();
    if (child < 0) {
        if (output_pipe[0] >= 0) ::close(output_pipe[0]);
        if (output_pipe[1] >= 0) ::close(output_pipe[1]);
        return std::unexpected(runtime::Error{
            .code = runtime::ErrorCode::internal,
            .message = "fork failed: " + std::string(std::strerror(errno)),
        });
    }

    if (child == 0) {
        if (spec.capture_output) {
            ::close(output_pipe[0]);
            ::dup2(output_pipe[1], STDOUT_FILENO);
            ::dup2(output_pipe[1], STDERR_FILENO);
            ::close(output_pipe[1]);
        }
        if (spec.workdir && ::chdir(spec.workdir->c_str()) != 0) {
            _exit(126);
        }
        std::vector<char*> argv;
        argv.reserve(spec.argv.size() + 1);
        for (const auto& arg : spec.argv) {
            argv.push_back(const_cast<char*>(arg.c_str()));
        }
        argv.push_back(nullptr);
        ::execvp(argv.front(), argv.data());
        _exit(errno == ENOENT ? 127 : 126);
    }

    std::string output;
    if (spec.capture_output) {
        ::close(output_pipe[1]);
        std::array<char, 4096> buffer{};
        while (true) {
            const auto count = ::read(output_pipe[0], buffer.data(), buffer.size());
            if (count > 0) {
                output.append(buffer.data(), static_cast<std::size_t>(count));
            } else if (count < 0 && errno == EINTR) {
                continue;
            } else {
                break;
            }
        }
        ::close(output_pipe[0]);
    }

    int status = 0;
    while (::waitpid(child, &status, 0) < 0 && errno == EINTR) {}
    const auto exit_code = WIFEXITED(status)
        ? WEXITSTATUS(status)
        : 128 + (WIFSIGNALED(status) ? WTERMSIG(status) : 0);
    return ProcessResult{exit_code, std::move(output)};
#endif
}

} // namespace star::process
