#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace scrt::viz {

/// One file attached to a request: a reference photo, or a specification document.
///
/// The bytes are carried raw and base64-encoded at send time. `media_type` is what the API is
/// told the file is - `image/png`, `image/jpeg`, `application/pdf` - and it decides which content
/// block the attachment becomes, so it has to be right rather than merely plausible.
struct Attachment {
    std::string          name;        ///< File name, for the user interface only.
    std::string          media_type;  ///< IANA media type, e.g. "image/png".
    std::vector<std::uint8_t> bytes;  ///< File contents, unencoded.
};

/// What a request asked for. Everything except `api_key` and `user_text` has a usable default.
struct ClaudeRequest {
    std::string             api_key;
    std::string             model = "claude-opus-5";
    std::string             system_prompt;
    std::string             user_text;
    std::vector<Attachment> attachments;
    int                     max_tokens = 16000;
    /// "low" | "medium" | "high" | "xhigh" | "max". Sent as output_config.effort.
    std::string             effort = "high";
};

/// What came back. `ok` is false for a transport failure, a non-200 status, or a refusal; in
/// every one of those cases `error` says which, in words meant for the user rather than a log.
struct ClaudeResponse {
    bool        ok = false;
    std::string text;         ///< Concatenated text blocks of the reply.
    std::string error;        ///< Human-readable failure, empty when ok.
    std::string stop_reason;  ///< "end_turn", "max_tokens", "refusal", ...
    int         http_status = 0;
    int         input_tokens = 0;
    int         output_tokens = 0;
};

/// Sends one non-streaming request to the Claude Messages API and waits for the reply.
///
/// BLOCKS for as long as the model takes, which on a hard prompt is minutes - call it on a worker
/// thread, never from the frame loop.
///
/// Windows-only: it speaks HTTPS through WinHTTP, which ships with the OS, rather than pulling in
/// a curl or cpr dependency for a single request shape. On any other platform it returns an
/// `ok == false` response saying so.
ClaudeResponse send_claude_request(const ClaudeRequest& req);

/// Base64, standard alphabet with padding. Exposed because the request builder is tested through
/// it and because attachments are encoded nowhere else.
std::string base64_encode(const std::vector<std::uint8_t>& bytes);

/// Guesses an IANA media type from a file extension; empty when the type is not one the API
/// accepts, which is the caller's cue to refuse the attachment rather than send it mislabelled.
std::string media_type_for_extension(const std::string& extension);

} // namespace scrt::viz
