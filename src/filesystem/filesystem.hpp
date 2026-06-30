#pragma once

#include <string>

namespace flint::fs {

std::string get_config_dir();
std::string get_cache_dir();
std::string get_data_dir();

std::string get_minecraft_dir();
std::string get_instances_dir();

void ensure_directories_exist();

} // namespace flint::fs
