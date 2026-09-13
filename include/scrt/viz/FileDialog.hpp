#pragma once
#include <filesystem>
#include <string>
#include <vector>

namespace scrt::viz {

/// One entry of a file-dialog type filter: a human label and a comma-separated extension list.
///
/// The extensions carry no dot and no wildcard - `{"3D model", "stl,obj,ply"}` - which is what
/// nativefiledialog-extended expects.
struct FileFilter {
    const char* label;
    const char* extensions;
};

/// Opens a native "open file" dialog; false on cancel, and on an error it also fills `error_out`.
///
/// A cancel leaves `error_out` untouched, so a panel can show its previous message rather than
/// clearing itself every time the user changes their mind.
bool open_file_dialog(const std::vector<FileFilter>& filters,
                      const std::filesystem::path& start_dir,
                      std::filesystem::path& out, std::string& error_out);

/// Opens a native "save as" dialog seeded with `start_dir`/`default_name`; false on cancel.
bool save_file_dialog(const std::vector<FileFilter>& filters,
                      const std::filesystem::path& start_dir, const std::string& default_name,
                      std::filesystem::path& out, std::string& error_out);

/// True when the native dialogs are usable at all.
///
/// False means the COM session behind them could not be initialised, in which case every panel
/// that offers a Browse button must keep its typed-path field reachable rather than becoming a
/// dead end.
bool file_dialogs_available();

} // namespace scrt::viz
