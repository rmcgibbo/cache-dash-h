#pragma once
#include <functional> // for function
#include <string>     // for string
#include <utility>    // for pair
#include <vector>     // for vector

namespace cache_dash_h {

std::pair<std::string, int>
exec_and_record_opened_files(std::vector<std::string>& cmd,
                             std::function<void(std::string const&)> open_callback);

}; // namespace cache_dash_h