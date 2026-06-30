#include "filesystem.hpp"
#include <cstdlib>
#include <filesystem>
#include <unistd.h>
#include <pwd.h>

namespace flint::fs {

namespace {
std::string get_home_dir() {
    if (const char* home = std::getenv("HOME")) {
        return home;
    }
    // Fallback if HOME env var is not set
    struct passwd* pw = getpwuid(getuid());
    if (pw && pw->pw_dir) {
        return pw->pw_dir;
    }
    return "/tmp";
}

std::string get_env_or_fallback(const char* envVar, std::string_view fallbackSuffix) {
    if (const char* val = std::getenv(envVar)) {
        return val;
    }
    return get_home_dir() + std::string(fallbackSuffix);
}
} // namespace

std::string get_config_dir() {
    return get_env_or_fallback("XDG_CONFIG_HOME", "/.config") + "/flint";
}

std::string get_cache_dir() {
    return get_env_or_fallback("XDG_CACHE_HOME", "/.cache") + "/flint";
}

std::string get_data_dir() {
    return get_env_or_fallback("XDG_DATA_HOME", "/.local/share") + "/flint";
}

std::string get_minecraft_dir() {
    return get_data_dir() + "/minecraft";
}

std::string get_instances_dir() {
    return get_data_dir() + "/instances";
}

void ensure_directories_exist() {
    std::filesystem::create_directories(get_config_dir());
    std::filesystem::create_directories(get_cache_dir());
    std::filesystem::create_directories(get_data_dir());
    std::filesystem::create_directories(get_minecraft_dir());
    std::filesystem::create_directories(get_instances_dir());
}

} // namespace flint::fs
