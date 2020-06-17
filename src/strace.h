#pragma once
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace cache_dash_h {

std::pair<std::string, int>
exec_and_record_opened_files(std::vector<std::string>& cmd,
                             std::function<void(std::string const&)> open_callback);

}; // namespace cache_dash_h