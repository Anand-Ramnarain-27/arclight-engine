#pragma once

#include <string>
#include <string_view>

namespace arc
{
    /// The path of a file in the Assets folder, or an empty string if it is not found. Looks in
    /// Assets/ under the working directory first (Game/ when run from Visual Studio or Xcode), then
    /// in Assets/ next to the executable (the release package). UTF-8.
    [[nodiscard]] std::string findAssetPath(std::string_view relativePath);
}
