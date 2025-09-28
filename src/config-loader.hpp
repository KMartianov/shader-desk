// config-loader.hpp
#pragma once
#include <nlohmann/json.hpp>
#include <string>

nlohmann::json load_config();
std::string get_config_path();