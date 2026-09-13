#include "scrt/viz/ClaudeClient.hpp"

#include <algorithm>
#include <cctype>

#include <nlohmann/json.hpp>

#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>
#endif

using nlohmann::json;

namespace scrt::viz {

namespace {

constexpr char kAlphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

#ifdef _WIN32

/// Widens ASCII to UTF-16 for the WinHTTP header API.
///
/// ASCII only, deliberately: everything passed through here is a header this file builds - an
/// API key, a version string, a content type - and none of it is user text.
std::wstring widen_ascii(const std::string& s) {
    return std::wstring(s.begin(), s.end());
}

/// Closes a WinHTTP handle on scope exit; there are three of them and four ways out.
struct Handle {
    HINTERNET h = nullptr;
    ~Handle() { if (h) WinHttpCloseHandle(h); }
    explicit operator bool() const { return h != nullptr; }
};

/// Turns GetLastError() into something a user can act on rather than a hex code.
std::string last_error_text(const char* stage) {
    const DWORD code = ::GetLastError();
    std::string what;
    switch (code) {
        case ERROR_WINHTTP_CANNOT_CONNECT:
        case ERROR_WINHTTP_NAME_NOT_RESOLVED:
            what = "could not reach api.anthropic.com - check the network connection";
            break;
        case ERROR_WINHTTP_TIMEOUT:
            what = "the request timed out";
            break;
        case ERROR_WINHTTP_SECURE_FAILURE:
            what = "the HTTPS connection could not be verified";
            break;
        default:
            what = "Windows error " + std::to_string(code);
            break;
    }
    return std::string(stage) + ": " + what;
}

#endif // _WIN32

/// Builds the Messages API request body.
///
/// Attachments go BEFORE the text block: the API's own guidance for documents and images is that
/// the model reads them better in that order, and it costs nothing to honour.
std::string build_body(const ClaudeRequest& req) {
    json content = json::array();
    for (const auto& a : req.attachments) {
        if (a.bytes.empty() || a.media_type.empty()) continue;
        const bool is_pdf = (a.media_type == "application/pdf");
        content.push_back({
            {"type", is_pdf ? "document" : "image"},
            {"source", {{"type", "base64"},
                        {"media_type", a.media_type},
                        {"data", base64_encode(a.bytes)}}},
        });
    }
    content.push_back({{"type", "text"}, {"text", req.user_text}});

    json body = {
        {"model", req.model},
        {"max_tokens", req.max_tokens},
        {"messages", json::array({json{{"role", "user"}, {"content", content}}})},
    };
    if (!req.system_prompt.empty()) body["system"] = req.system_prompt;
    if (!req.effort.empty()) body["output_config"] = {{"effort", req.effort}};
    return body.dump();
}

/// Pulls the assistant's words out of a successful response.
///
/// The content array holds more than text - with adaptive thinking on there are thinking blocks
/// too - so it is filtered by type rather than indexed at [0].
std::string collect_text(const json& msg) {
    std::string out;
    if (!msg.contains("content") || !msg["content"].is_array()) return out;
    for (const auto& block : msg["content"]) {
        if (block.value("type", "") == "text") out += block.value("text", "");
    }
    return out;
}

/// Turns an API error body into a sentence. The API's own message is usually the clearest thing
/// available, so it is preferred over anything invented here.
std::string describe_error(int status, const std::string& body) {
    std::string detail;
    try {
        const json j = json::parse(body);
        if (j.contains("error") && j["error"].contains("message"))
            detail = j["error"]["message"].get<std::string>();
    } catch (...) {
        detail = body.substr(0, 300);
    }

    switch (status) {
        case 401:
            return "The API key was rejected. Check that it is a current key from "
                   "console.anthropic.com.";
        case 403:
            return "That key is not allowed to use this model. " + detail;
        case 429:
            return "Rate limited - too many requests, or the account is out of credit. " + detail;
        case 400:
            return "The request was rejected: " + detail;
        default:
            break;
    }
    if (status >= 500) return "The API had a server error (" + std::to_string(status) +
                              "). Trying again usually works. " + detail;
    return "HTTP " + std::to_string(status) + ": " + detail;
}

} // namespace

// ---- base64 ---------------------------------------------------------------------------------

std::string base64_encode(const std::vector<std::uint8_t>& bytes) {
    std::string out;
    out.reserve(((bytes.size() + 2) / 3) * 4);
    std::size_t i = 0;
    for (; i + 2 < bytes.size(); i += 3) {
        const std::uint32_t v = (static_cast<std::uint32_t>(bytes[i]) << 16) |
                                (static_cast<std::uint32_t>(bytes[i + 1]) << 8) |
                                static_cast<std::uint32_t>(bytes[i + 2]);
        out += kAlphabet[(v >> 18) & 0x3F];
        out += kAlphabet[(v >> 12) & 0x3F];
        out += kAlphabet[(v >> 6) & 0x3F];
        out += kAlphabet[v & 0x3F];
    }
    if (i < bytes.size()) {
        std::uint32_t v = static_cast<std::uint32_t>(bytes[i]) << 16;
        const bool    two = (i + 1 < bytes.size());
        if (two) v |= static_cast<std::uint32_t>(bytes[i + 1]) << 8;
        out += kAlphabet[(v >> 18) & 0x3F];
        out += kAlphabet[(v >> 12) & 0x3F];
        out += two ? kAlphabet[(v >> 6) & 0x3F] : '=';
        out += '=';
    }
    return out;
}

std::string media_type_for_extension(const std::string& extension) {
    std::string e = extension;
    if (!e.empty() && e.front() == '.') e.erase(e.begin());
    std::transform(e.begin(), e.end(), e.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    if (e == "png")                  return "image/png";
    if (e == "jpg" || e == "jpeg")   return "image/jpeg";
    if (e == "gif")                  return "image/gif";
    if (e == "webp")                 return "image/webp";
    if (e == "pdf")                  return "application/pdf";
    return {};   // anything else is refused rather than sent under a guessed type
}

// ---- send_claude_request --------------------------------------------------------------------

#ifndef _WIN32

ClaudeResponse send_claude_request(const ClaudeRequest&) {
    ClaudeResponse r;
    r.error = "The scene assistant needs Windows: its HTTPS client is WinHTTP.";
    return r;
}

#else

ClaudeResponse send_claude_request(const ClaudeRequest& req) {
    ClaudeResponse out;
    if (req.api_key.empty()) {
        out.error = "No API key set.";
        return out;
    }

    Handle session{WinHttpOpen(L"solar-cooker-rt/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                               WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0)};
    if (!session) { out.error = last_error_text("Could not start an HTTPS session"); return out; }

    // Generous, and deliberately so: a hard prompt at high effort can think for minutes, and a
    // timeout here reads to the user as "the assistant is broken" rather than "it is still busy".
    WinHttpSetTimeouts(session.h, 15000, 30000, 30000, 600000);

    Handle conn{WinHttpConnect(session.h, L"api.anthropic.com", INTERNET_DEFAULT_HTTPS_PORT, 0)};
    if (!conn) { out.error = last_error_text("Could not connect"); return out; }

    Handle request{WinHttpOpenRequest(conn.h, L"POST", L"/v1/messages", nullptr,
                                      WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                      WINHTTP_FLAG_SECURE)};
    if (!request) { out.error = last_error_text("Could not open the request"); return out; }

    const std::wstring headers = L"Content-Type: application/json\r\n"
                                 L"anthropic-version: 2023-06-01\r\n"
                                 L"x-api-key: " + widen_ascii(req.api_key) + L"\r\n";

    const std::string body = build_body(req);
    if (!WinHttpSendRequest(request.h, headers.c_str(), static_cast<DWORD>(headers.size()),
                            const_cast<char*>(body.data()), static_cast<DWORD>(body.size()),
                            static_cast<DWORD>(body.size()), 0)) {
        out.error = last_error_text("Could not send the request");
        return out;
    }
    if (!WinHttpReceiveResponse(request.h, nullptr)) {
        out.error = last_error_text("No reply from the API");
        return out;
    }

    DWORD status = 0, status_size = sizeof(status);
    WinHttpQueryHeaders(request.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &status, &status_size, WINHTTP_NO_HEADER_INDEX);
    out.http_status = static_cast<int>(status);

    std::string payload;
    for (;;) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request.h, &available)) {
            out.error = last_error_text("The reply was cut short");
            return out;
        }
        if (available == 0) break;
        const std::size_t offset = payload.size();
        payload.resize(offset + available);
        DWORD read = 0;
        if (!WinHttpReadData(request.h, payload.data() + offset, available, &read)) {
            out.error = last_error_text("The reply could not be read");
            return out;
        }
        payload.resize(offset + read);
    }

    if (status != 200) {
        out.error = describe_error(out.http_status, payload);
        return out;
    }

    json msg;
    try {
        msg = json::parse(payload);
    } catch (const std::exception& e) {
        out.error = std::string("The API's reply was not valid JSON: ") + e.what();
        return out;
    }

    out.stop_reason   = msg.value("stop_reason", "");
    out.input_tokens  = msg.contains("usage") ? msg["usage"].value("input_tokens", 0) : 0;
    out.output_tokens = msg.contains("usage") ? msg["usage"].value("output_tokens", 0) : 0;
    out.text          = collect_text(msg);

    // A refusal is HTTP 200 with an empty-ish body, so the status check above does not catch it.
    if (out.stop_reason == "refusal") {
        out.error = "The model declined this request.";
        return out;
    }
    if (out.stop_reason == "max_tokens") {
        // Reported rather than swallowed: a scene cut off mid-JSON will fail to parse, and
        // "invalid JSON" would send the user looking in the wrong place.
        out.error = "The reply hit the length limit and is incomplete. Ask for a simpler design.";
        return out;
    }
    if (out.text.empty()) {
        out.error = "The model replied with nothing.";
        return out;
    }

    out.ok = true;
    return out;
}

#endif // _WIN32

} // namespace scrt::viz
