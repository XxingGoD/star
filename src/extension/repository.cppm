export module star.extension.repository;

import std;
import star.extension.manifest;
import star.runtime.result;

namespace {

using star::runtime::Error;
using star::runtime::ErrorCode;

template <typename T>
auto failure(ErrorCode code, std::string message, std::string hint = {})
    -> star::runtime::Result<T> {
    return std::unexpected(Error{
        .code = code,
        .message = std::move(message),
        .hint = std::move(hint),
    });
}

auto valid_id(std::string_view id) -> bool {
    if (id.empty() || id.front() == '.' || id.back() == '.') return false;
    return std::ranges::all_of(id, [](unsigned char character) {
        return std::isalnum(character) || character == '.' ||
               character == '-' || character == '_';
    });
}

auto is_inside(const std::filesystem::path& child,
               const std::filesystem::path& parent) -> bool {
    const auto relative = child.lexically_relative(parent);
    if (relative.empty() || relative.is_absolute()) return false;
    return *relative.begin() != "..";
}

auto validate_package_tree(const std::filesystem::path& source)
    -> star::runtime::Result<bool> {
    const auto root_status = std::filesystem::symlink_status(source);
    if (std::filesystem::is_symlink(root_status) ||
        !std::filesystem::is_directory(root_status)) {
        return failure<bool>(ErrorCode::invalid_input,
            "extension source must be a directory, not a symbolic link: " +
            source.string());
    }

    for (auto entries = std::filesystem::recursive_directory_iterator(source);
         entries != std::default_sentinel; ++entries) {
        const auto status = entries->symlink_status();
        if (std::filesystem::is_symlink(status)) {
            return failure<bool>(ErrorCode::invalid_input,
                "extension packages cannot contain symbolic links: " +
                entries->path().string());
        }
        if (!std::filesystem::is_directory(status) &&
            !std::filesystem::is_regular_file(status)) {
            return failure<bool>(ErrorCode::invalid_input,
                "extension packages can contain only directories and regular files: " +
                entries->path().string());
        }
    }
    return true;
}

void copy_package_tree(const std::filesystem::path& source,
                       const std::filesystem::path& destination) {
    std::filesystem::create_directories(destination);
    for (auto entries = std::filesystem::recursive_directory_iterator(source);
         entries != std::default_sentinel; ++entries) {
        const auto relative = entries->path().lexically_relative(source);
        const auto target = destination / relative;
        const auto status = entries->symlink_status();
        if (std::filesystem::is_directory(status)) {
            std::filesystem::create_directories(target);
        } else if (std::filesystem::is_regular_file(status)) {
            std::filesystem::create_directories(target.parent_path());
            std::filesystem::copy_file(entries->path(), target);
            std::filesystem::permissions(target, status.permissions());
        } else {
            throw std::filesystem::filesystem_error(
                "unsupported package entry", entries->path(),
                std::make_error_code(std::errc::invalid_argument));
        }
    }
}

auto unique_name(std::string_view operation, std::string_view id)
    -> std::string {
    static std::atomic_uint64_t sequence = 0;
    return std::format("{}-{}-{}-{}", operation, id,
        std::chrono::steady_clock::now().time_since_epoch().count(),
        ++sequence);
}

} // namespace

export namespace star::extension {

struct PackageRecord {
    Kind kind = Kind::tool;
    std::string id;
    std::string version;
    std::filesystem::path root;
};

class Repository {
public:
    explicit Repository(std::filesystem::path root)
        : root_(std::filesystem::absolute(std::move(root)).lexically_normal()) {}

    auto root() const -> const std::filesystem::path& { return root_; }

    auto kind_root(Kind kind) const -> std::filesystem::path {
        return root_ / (kind == Kind::tool ? "tools" : "fields");
    }

    auto extension_roots() const -> std::vector<std::filesystem::path> {
        return {kind_root(Kind::tool), kind_root(Kind::field)};
    }

    auto install(const std::filesystem::path& source, Kind expected_kind)
        -> runtime::Result<PackageRecord> {
        try {
            const auto absolute_source = std::filesystem::absolute(source)
                                             .lexically_normal();
            auto safe_tree = validate_package_tree(absolute_source);
            if (!safe_tree) return std::unexpected(safe_tree.error());

            const auto source_root = std::filesystem::canonical(absolute_source);
            const auto repository_root = std::filesystem::weakly_canonical(root_);
            if (source_root == repository_root ||
                is_inside(repository_root, source_root)) {
                return failure<PackageRecord>(ErrorCode::invalid_input,
                    "Star repository cannot be inside the extension source");
            }

            auto manifest = parse_manifest(source_root / "star.toml");
            if (!manifest) return std::unexpected(manifest.error());
            if (manifest->kind != expected_kind) {
                return failure<PackageRecord>(ErrorCode::invalid_input,
                    "cannot install " + std::string(kind_name(manifest->kind)) +
                    " package with star " + std::string(kind_name(expected_kind)) +
                    " add");
            }

            const auto packages = kind_root(expected_kind);
            const auto destination = packages / manifest->id;
            if (std::filesystem::exists(destination)) {
                return failure<PackageRecord>(ErrorCode::invalid_input,
                    "extension already installed: " + manifest->id,
                    "remove the installed package before adding another version");
            }

            const auto staging_root = root_ / ".staging";
            std::filesystem::create_directories(staging_root);
            std::filesystem::create_directories(packages);
            const auto staging = staging_root /
                unique_name("install", manifest->id);

            try {
                copy_package_tree(source_root, staging);
                auto copied = parse_manifest(staging / "star.toml");
                if (!copied) {
                    std::error_code cleanup_error;
                    std::filesystem::remove_all(staging, cleanup_error);
                    return std::unexpected(copied.error());
                }
                if (copied->id != manifest->id ||
                    copied->version != manifest->version ||
                    copied->kind != manifest->kind) {
                    std::error_code cleanup_error;
                    std::filesystem::remove_all(staging, cleanup_error);
                    return failure<PackageRecord>(ErrorCode::invalid_input,
                        "extension changed while it was being installed");
                }
                std::filesystem::rename(staging, destination);
            } catch (...) {
                std::error_code cleanup_error;
                std::filesystem::remove_all(staging, cleanup_error);
                throw;
            }

            return PackageRecord{
                .kind = manifest->kind,
                .id = manifest->id,
                .version = manifest->version,
                .root = destination,
            };
        } catch (const std::filesystem::filesystem_error& error) {
            return failure<PackageRecord>(ErrorCode::internal,
                "cannot install extension package: " +
                std::string(error.what()));
        } catch (const std::exception& error) {
            return failure<PackageRecord>(ErrorCode::internal,
                "cannot install extension package: " +
                std::string(error.what()));
        }
    }

    auto uninstall(Kind kind, std::string_view id)
        -> runtime::Result<PackageRecord> {
        if (!valid_id(id)) {
            return failure<PackageRecord>(ErrorCode::invalid_input,
                "invalid extension id: " + std::string(id));
        }

        try {
            const auto packages = kind_root(kind);
            const auto target = packages / id;
            const auto status = std::filesystem::symlink_status(target);
            if (!std::filesystem::exists(status)) {
                return failure<PackageRecord>(ErrorCode::not_found,
                    "extension is not installed: " + std::string(id));
            }
            if (std::filesystem::is_symlink(status) ||
                !std::filesystem::is_directory(status)) {
                return failure<PackageRecord>(ErrorCode::invalid_input,
                    "installed extension path is not a managed directory: " +
                    target.string());
            }

            auto manifest = parse_manifest(target / "star.toml");
            if (!manifest) return std::unexpected(manifest.error());
            if (manifest->kind != kind || manifest->id != id ||
                manifest->root.parent_path() != packages) {
                return failure<PackageRecord>(ErrorCode::invalid_input,
                    "installed extension identity does not match its repository path");
            }

            const PackageRecord record{
                .kind = manifest->kind,
                .id = manifest->id,
                .version = manifest->version,
                .root = target,
            };
            const auto trash_root = root_ / ".trash";
            std::filesystem::create_directories(trash_root);
            const auto trash = trash_root / unique_name("remove", id);
            std::filesystem::rename(target, trash);
            std::error_code cleanup_error;
            std::filesystem::remove_all(trash, cleanup_error);
            return record;
        } catch (const std::filesystem::filesystem_error& error) {
            return failure<PackageRecord>(ErrorCode::internal,
                "cannot remove extension package: " +
                std::string(error.what()));
        } catch (const std::exception& error) {
            return failure<PackageRecord>(ErrorCode::internal,
                "cannot remove extension package: " +
                std::string(error.what()));
        }
    }

private:
    std::filesystem::path root_;
};

} // namespace star::extension
