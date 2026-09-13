#include "scrt/viz/FileDialog.hpp"

#include <nfd.h>

namespace scrt::viz {

namespace {

/// Owns the process-wide NFD session: NFD_Init() on first use, NFD_Quit() at process teardown.
///
/// NFD_Init/NFD_Quit are COM (CoInitializeEx/CoUninitialize) on Windows and must be paired on
/// one thread. Making this a function-local static means init happens exactly once, lazily, on
/// the GUI thread that first opens a dialog - never per click, and never at all in a headless
/// run that draws no panels - and quit happens once during static destruction on that same
/// thread. CoInitializeEx is per-thread reference counted, so coexisting with GLFW's own COM use
/// is fine; if it fails anyway (RPC_E_CHANGED_MODE), ok stays false and callers fall back to
/// their typed-path field rather than losing the ability to open or save anything.
struct NfdSession {
    NfdSession() : ok(NFD_Init() == NFD_OKAY) {}
    ~NfdSession() { if (ok) NFD_Quit(); }
    NfdSession(const NfdSession&)            = delete;
    NfdSession& operator=(const NfdSession&) = delete;
    bool ok;  ///< True when NFD_Init() succeeded and dialogs may be opened.
};

/// The lazily created NFD session; the first caller initialises it, later callers reuse it.
const NfdSession& nfd_session() {
    static NfdSession session;
    return session;
}

/// Translates our filter list into NFD's, keeping the caller's strings alive for the call.
std::vector<nfdu8filteritem_t> to_nfd(const std::vector<FileFilter>& filters) {
    std::vector<nfdu8filteritem_t> out;
    out.reserve(filters.size());
    for (const auto& f : filters) out.push_back({f.label, f.extensions});
    return out;
}

/// Reports an NFD failure, and reports nothing at all for a cancel.
bool finish(nfdresult_t r, nfdu8char_t* picked, std::filesystem::path& out,
            std::string& error_out) {
    if (r == NFD_OKAY) {
        out = std::filesystem::path(std::string(picked));
        NFD_FreePathU8(picked);
        return true;
    }
    if (r == NFD_ERROR) {
        const char* msg = NFD_GetError();
        error_out       = std::string("File dialog failed: ") + (msg ? msg : "unknown error");
        NFD_ClearError();
    }
    return false;  // NFD_CANCEL, or the error already reported above.
}

} // namespace

bool file_dialogs_available() { return nfd_session().ok; }

bool open_file_dialog(const std::vector<FileFilter>& filters,
                      const std::filesystem::path& start_dir,
                      std::filesystem::path& out, std::string& error_out) {
    if (!nfd_session().ok) {
        error_out = "Could not initialise the native file dialog; type a path instead.";
        return false;
    }
    const auto        nf      = to_nfd(filters);
    const std::string dir_str = start_dir.string();
    nfdu8char_t*      picked  = nullptr;
    const nfdresult_t r =
        NFD_OpenDialogU8(&picked, nf.empty() ? nullptr : nf.data(),
                         static_cast<nfdfiltersize_t>(nf.size()),
                         dir_str.empty() ? nullptr : dir_str.c_str());
    return finish(r, picked, out, error_out);
}

bool save_file_dialog(const std::vector<FileFilter>& filters,
                      const std::filesystem::path& start_dir, const std::string& default_name,
                      std::filesystem::path& out, std::string& error_out) {
    if (!nfd_session().ok) {
        error_out = "Could not initialise the native file dialog; type a path instead.";
        return false;
    }
    const auto        nf      = to_nfd(filters);
    const std::string dir_str = start_dir.string();
    nfdu8char_t*      picked  = nullptr;
    const nfdresult_t r =
        NFD_SaveDialogU8(&picked, nf.empty() ? nullptr : nf.data(),
                         static_cast<nfdfiltersize_t>(nf.size()),
                         dir_str.empty() ? nullptr : dir_str.c_str(),
                         default_name.empty() ? nullptr : default_name.c_str());
    return finish(r, picked, out, error_out);
}

} // namespace scrt::viz
