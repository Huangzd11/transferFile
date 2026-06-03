#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace transfer {

std::string base64Encode(const std::vector<uint8_t>& data);
bool base64Decode(const std::string& encoded, std::vector<uint8_t>& out);

}  // namespace transfer
