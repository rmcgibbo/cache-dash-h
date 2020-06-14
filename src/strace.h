#pragma once
#include <functional>
#include <linux/limits.h>
#include <string>
#include <sys/types.h>
#include <utility>
#include <vector>

namespace cache_dash_h {

std::pair<std::string, int>
exec_and_record_opened_files(std::vector<std::string>& cmd,
                             std::function<void(std::string const&)> open_callback);

}; // namespace cache_dash_h