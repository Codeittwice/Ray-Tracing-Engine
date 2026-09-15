#include "scrt/viz/Panels.hpp"
#include "scrt/viz/AppConfig.hpp"
#include "scrt/viz/ClaudeClient.hpp"
#include "scrt/viz/FileDialog.hpp"
#include "scrt/viz/Fonts.hpp"
#include "scrt/viz/Icons.hpp"

#include "scrt/io/SceneDocument.hpp"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <exception>
#include <fstream>
#include <future>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "imgui.h"

namespace scrt::viz {

namespace {

// ---- the brief the model works from ---------------------------------------------------------
//
// This prompt is the whole contract. The scene loader parses generated documents in STRICT mode,
// which rejects any key it does not recognise, so an invented field is a hard failure rather than
// a silently ignored line - that is deliberate: it turns "the assistant made something subtly
// wrong" into "the assistant's answer did not load", which the user can see and act on.
//
// Keep this in step with io::SceneDocument::parse_document. If a surface type or a key is added
// there and not here, the assistant will never use it; if one is removed there and left here, it
// will produce documents that no longer load.

const char* const kSystemPrompt = R"PROMPT(
You design solar cooker scenes for a Monte Carlo ray tracer. You reply with one JSON document and
nothing else: no prose, no explanation, no markdown fence.

COORDINATES AND UNITS
- Right-handed, Z up. The ground is the z = 0 plane; the sun is overhead at +Z.
- Every length is in metres, every angle in degrees, irradiance in W/m^2.

DOCUMENT SHAPE
{
  "scene": {
    "name": "<short human name>",
    "sun": {
      "direction": [0.0, 0.0, -1.0],          // direction light TRAVELS; straight down is [0,0,-1]
                                               // or give "azimuth_deg" and "elevation_deg" instead
                                               // (elevation 90 = overhead, azimuth 0 = from +X)
      "dni_wm2": 1000.0,                       // 1000 is clear midday sun
      "sunshape": {"type": "pillbox", "half_angle_mrad": 4.65}
    },
    "aperture": {                              // the window rays are fired through; it must cover
      "type": "disk",                          // every reflector seen from the sun's direction
      "center": [0.0, 0.0, 1.0],
      "normal": [0.0, 0.0, 1.0],
      "radius": 0.8
    },
    "materials": [
      {"id": "foil_mirror", "type": "real_mirror", "reflectance": 0.85, "slope_error_mrad": 4.0},
      {"id": "pot", "type": "absorber"}
    ],
    "elements": [
      {
        "name": "back_panel",
        "material": "foil_mirror",
        "surface": {"type": "plane", "half_width": 0.35, "half_height": 0.25},
        "transform": {"rotation_euler_deg": [67.5, 0.0, 0.0], "translation": [0.0, 0.35, 0.35]}
      }
    ],
    "receiver": {
      "surface": {"type": "plane", "half_width": 0.1, "half_height": 0.1},
      "grid": {"nx": 32, "ny": 32},
      "transform": {"translation": [0.0, 0.0, 0.0]}
    }
  },
  "trace": {
    "n_primary_rays": 100000, "max_bounces": 3,
    "record_paths": false, "max_paths_to_record": 200, "rng_seed": 13
  }
}

SURFACE TYPES - use ONLY these, with exactly these keys
- plane                   half_width, half_height                        (a rectangle in its local XY)
- sphere                  radius
- paraboloid              focal_length_m, aperture_radius_m              (a dish; focus at +Z focal_length_m)
- cylindrical_paraboloid  focal_length_m, aperture_half_width_m, aperture_half_length_m  (a trough)
- fresnel_zone_lens       focal_length_m, inner_radius_m, pitch_m, n_zones, n_lens
- quadric                 coeffs {A..J}, aperture_box {min:[x,y,z], max:[x,y,z]}
- disk                    radius, hole_radius (optional; 0 = solid)  (a round plate; with a hole, an iris)
- thick_lens              radius1, radius2, center_thickness_m, diameter_m  (a real lens body; signed radii,
                          0 = flat; plano-convex is radius1 > 0, radius2 = 0; bind a dielectric material)
- slit_plate              half_width, half_height, slit_width, slit_count, slit_pitch (needed if count > 1)
Do NOT use a "mesh" surface: it needs a file on disk that you cannot supply.

MATERIAL TYPES - use ONLY these, with exactly these keys and no others
- perfect_mirror          (no keys at all)
- real_mirror             reflectance, slope_error_mrad
- absorber                (no keys at all - it absorbs everything that reaches it)
- dielectric              n, absorption_per_m, sellmeier (optional: "bk7", "fused_silica", "n_sf11",
                          "pmma" or "polycarbonate" for dispersion)  (glass; use only if genuinely needed)
- thin_dielectric_pane    n, thickness_m, absorption_per_m
- beam_splitter           reflectance, absorptance    (a designed split at every angle; the rest passes straight through)

TRANSFORMS
"transform" takes any of: "translation" [x,y,z], "rotation_euler_deg" [rx,ry,rz] (applied X then
Y then Z), "scale" [sx,sy,sz]. A plane's local normal is +Z before rotation.

RULES THAT MAKE A SCENE ACTUALLY WORK
1. Every "material" on an element must match an "id" in "materials".
2. The receiver is the pot. Put it where the light is focused - at a dish's focal point, along a
   trough's focal line, or on the ground under a panel cooker.
3. The aperture must sit ABOVE everything and be wide enough to cover the whole reflector array
   seen from above, or rays will miss the cooker entirely and the result will read as near zero.
4. Aim reflectors at the receiver. A flat panel tilted by angle t from horizontal sends overhead
   sun toward a point roughly 2t from vertical - work the geometry out, do not guess.
5. Prefer few, well-placed elements over many. Four panels that aim correctly beat twelve that
   do not.
6. Keep it physical: reflectance 0.8-0.95 for foil and mirror, slope_error_mrad 2-10 for real
   materials. The pot is an "absorber" and takes no parameters.
7. Add no key that is not listed above. The document is parsed strictly and an extra key is a
   hard failure, not a warning.

Reply with the JSON document alone.
)PROMPT";

// ---- state ----------------------------------------------------------------------------------

/// Everything the assistant overlay remembers between frames.
struct AiState {
    AppConfig cfg = load_config();
    bool      cfg_loaded = false;

    char        key_buf[256] = {};
    char        prompt_buf[4096] =
        "A panel cooker for one pot, about 60 cm across, made from cardboard and kitchen foil.";
    int         model_choice = 0;
    std::string save_note;

    std::vector<Attachment> attachments;
    std::string             attach_error;

    /// The in-flight request. Valid only while `running` is true.
    std::future<ClaudeResponse> pending;
    bool                        running = false;
    std::chrono::steady_clock::time_point started;

    std::string error;        ///< Last failure, shown in red. Empty when there is none.
    std::string raw_json;     ///< Last reply, after fence-stripping. Empty until one arrives.
    std::string summary;      ///< Human description of a document that parsed.
    bool        parsed = false;
    int         in_tokens = 0, out_tokens = 0;
};

AiState g_ai;

/// Models offered, most capable first. Opus is the default because this is a design task the
/// user runs a handful of times a session, not a hot loop - and a scene that does not aim its
/// mirrors correctly costs more of their time than the token difference costs them in money.
const char* const kModelIds[]    = {"claude-opus-5", "claude-sonnet-5", "claude-haiku-4-5"};
const char* const kModelLabels[] = {"Claude Opus 5 (best designs)",
                                    "Claude Sonnet 5 (faster, cheaper)",
                                    "Claude Haiku 4.5 (fastest, cheapest)"};

/// Strips a ```json ... ``` fence if the model wrapped its answer in one.
///
/// The prompt asks for bare JSON and usually gets it. This is here because the failure mode
/// otherwise is a parse error on line 1 that tells the user nothing about what went wrong.
std::string strip_fence(std::string s) {
    const auto first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return s;
    if (s.compare(first, 3, "```") != 0) return s;

    const auto nl = s.find('\n', first);
    if (nl == std::string::npos) return s;
    const auto close = s.rfind("```");
    if (close == std::string::npos || close <= nl) return s.substr(nl + 1);
    return s.substr(nl + 1, close - nl - 1);
}

/// Describes a parsed document in one paragraph, so the user can sanity-check before loading.
std::string describe(const io::SceneDocument& doc) {
    char buf[512];
    char sun_line[64];
    if (const auto* sun = io::first_sun(doc))
        std::snprintf(sun_line, sizeof(sun_line), "Sun %.0f W/m².", sun->dni_wm2);
    else
        std::snprintf(sun_line, sizeof(sun_line), "%zu source%s, no sun.",
                      doc.sources.size(), doc.sources.size() == 1 ? "" : "s");
    std::snprintf(buf, sizeof(buf),
                  "%s\n%zu element%s, %zu material%s. %s",
                  doc.name.empty() ? "(unnamed)" : doc.name.c_str(),
                  doc.elements.size(), doc.elements.size() == 1 ? "" : "s",
                  doc.materials.size(), doc.materials.size() == 1 ? "" : "s",
                  sun_line);
    return buf;
}

/// Validates the reply and fills in summary / error.
void accept_reply(const std::string& text) {
    g_ai.raw_json = strip_fence(text);
    g_ai.parsed   = false;
    g_ai.summary.clear();
    try {
        const auto j = nlohmann::json::parse(g_ai.raw_json);
        // Strict: an invented key is rejected here rather than quietly ignored, which is the
        // only reason a wrong-but-plausible scene cannot reach the viewport looking correct.
        const auto doc = io::parse_document(j, /*strict=*/true);
        g_ai.summary   = describe(doc);
        g_ai.parsed    = true;
        g_ai.error.clear();
    } catch (const std::exception& e) {
        g_ai.error = std::string("The reply did not load as a scene: ") + e.what();
    }
}

/// Starts a request on a worker thread. The GUI keeps drawing while it runs.
void start_request(const std::string& user_text) {
    ClaudeRequest req;
    req.api_key       = g_ai.cfg.api_key;
    req.model         = kModelIds[g_ai.model_choice];
    req.system_prompt = kSystemPrompt;
    req.user_text     = user_text;
    req.attachments   = g_ai.attachments;

    g_ai.error.clear();
    g_ai.running = true;
    g_ai.started = std::chrono::steady_clock::now();
    g_ai.pending = std::async(std::launch::async,
                              [req]() { return send_claude_request(req); });
}

/// Picks up a finished request. Must run before anything reads the result this frame.
void poll_request() {
    if (!g_ai.running) return;
    if (g_ai.pending.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;

    g_ai.running = false;
    const ClaudeResponse r = g_ai.pending.get();
    g_ai.in_tokens  = r.input_tokens;
    g_ai.out_tokens = r.output_tokens;
    if (!r.ok) {
        g_ai.error = r.error;
        // A truncated reply is still worth showing: the user can see how far it got.
        if (!r.text.empty()) g_ai.raw_json = strip_fence(r.text);
        return;
    }
    accept_reply(r.text);
}

/// Writes the generated document somewhere durable and returns the path, or an empty path.
///
/// A real file rather than an in-memory hand-off: the Viewer loads scenes by path, which is the
/// one route that already defers the load to the end of the frame and so cannot free the Scene
/// out from under the panels still being drawn. It also leaves the user something to keep.
std::filesystem::path write_generated(std::string& error_out) {
    try {
        const auto dir = config_path().parent_path() / "generated";
        std::filesystem::create_directories(dir);

        const auto  now = std::time(nullptr);
        std::tm     tm{};
#ifdef _WIN32
        localtime_s(&tm, &now);
#else
        localtime_r(&now, &tm);
#endif
        char stamp[32];
        std::strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", &tm);

        const auto    path = dir / (std::string("assistant-") + stamp + ".json");
        std::ofstream out(path, std::ios::trunc);
        if (!out) { error_out = "Could not write " + path.string(); return {}; }
        out << g_ai.raw_json;
        if (!out.good()) { error_out = "Could not write " + path.string(); return {}; }
        return path;
    } catch (const std::exception& e) {
        error_out = std::string("Could not save the generated scene: ") + e.what();
        return {};
    }
}

void add_attachment() {
    std::filesystem::path picked;
    std::string           err;
    if (!open_file_dialog({{"Reference photo or document", "png,jpg,jpeg,gif,webp,pdf"}}, {},
                          picked, err)) {
        if (!err.empty()) g_ai.attach_error = err;
        return;
    }
    const std::string media = media_type_for_extension(picked.extension().string());
    if (media.empty()) {
        g_ai.attach_error = "Only PNG, JPEG, GIF, WebP and PDF can be attached.";
        return;
    }

    std::ifstream in(picked, std::ios::binary);
    if (!in) { g_ai.attach_error = "Could not read " + picked.filename().string(); return; }
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)),
                                    std::istreambuf_iterator<char>());

    // The API caps a whole request at 32 MB, and base64 inflates by a third. Refusing here with
    // a number beats a 400 from the server three seconds after the user presses Generate.
    constexpr std::size_t kMaxBytes = 6u * 1024u * 1024u;
    if (bytes.size() > kMaxBytes) {
        g_ai.attach_error = picked.filename().string() + " is larger than 6 MB. Shrink it first.";
        return;
    }

    g_ai.attach_error.clear();
    g_ai.attachments.push_back({picked.filename().string(), media, std::move(bytes)});
}

} // namespace

// ---- draw_ai_overlay -------------------------------------------------------------------------

void draw_ai_overlay(PanelContext& ctx) {
    poll_request();   // always, so a reply lands even if the user closed the overlay

    if (!ctx.ai_overlay_open || !*ctx.ai_overlay_open) return;

    if (!g_ai.cfg_loaded) {
        g_ai.cfg_loaded = true;
        std::snprintf(g_ai.key_buf, sizeof(g_ai.key_buf), "%s", g_ai.cfg.api_key.c_str());
        for (int i = 0; i < 3; ++i)
            if (g_ai.cfg.model == kModelIds[i]) g_ai.model_choice = i;
    }

    const float  k = ui_scale();
    const ImVec2 centre(ImGui::GetIO().DisplaySize.x * 0.5f, ImGui::GetIO().DisplaySize.y * 0.5f);
    ImGui::SetNextWindowPos(centre, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(720.0f * k, 0.0f), ImGuiCond_Always);

    ImGui::OpenPopup("###ai_overlay");
    if (!ImGui::BeginPopupModal(ICON_FA_WAND_MAGIC_SPARKLES "  Scene assistant###ai_overlay",
                                ctx.ai_overlay_open,
                                ImGuiWindowFlags_NoSavedSettings |
                                    ImGuiWindowFlags_AlwaysAutoResize |
                                    ImGuiWindowFlags_NoMove)) {
        return;
    }

    // ---- the key --
    if (g_ai.cfg.api_key.empty() || ImGui::CollapsingHeader(ICON_FA_USER "  API key")) {
        ImGui::TextWrapped(
            "The assistant runs on your own Anthropic API key and bills your account. Create one "
            "at console.anthropic.com, then paste it here.");
        ImGui::SetNextItemWidth(-120.0f * k);
        ImGui::InputText("##key", g_ai.key_buf, sizeof(g_ai.key_buf),
                         ImGuiInputTextFlags_Password);
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_FLOPPY_DISK "  Save key", ImVec2(-1, 0))) {
            g_ai.cfg.api_key = g_ai.key_buf;
            g_ai.cfg.model   = kModelIds[g_ai.model_choice];
            g_ai.save_note   = save_config(g_ai.cfg)
                                   ? "Saved to " + config_path().string()
                                   : std::string("Could not write ") + config_path().string();
        }
        ImGui::TextDisabled("Stored as plain text in your Windows user profile, not encrypted.");
        if (!g_ai.save_note.empty()) ImGui::TextDisabled("%s", g_ai.save_note.c_str());
        ImGui::Spacing();
    }

    const bool have_key = !g_ai.cfg.api_key.empty();

    // ---- the brief --
    ImGui::TextDisabled("Describe the cooker you want");
    ImGui::InputTextMultiline("##brief", g_ai.prompt_buf, sizeof(g_ai.prompt_buf),
                              ImVec2(-1, ImGui::GetTextLineHeight() * 4.5f));
    ImGui::TextDisabled("Say what it is made of, roughly how big, and what it has to heat.");

    ImGui::Spacing();

    // ---- attachments --
    ImGui::BeginDisabled(!file_dialogs_available());
    if (ImGui::Button(ICON_FA_FILE_IMPORT "  Attach a photo or spec")) add_attachment();
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextDisabled("PNG, JPEG, WebP or PDF. Up to 6 MB each.");

    for (std::size_t i = 0; i < g_ai.attachments.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        ImGui::Text(ICON_FA_FILE "  %s", g_ai.attachments[i].name.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton(ICON_FA_TRASH)) {
            g_ai.attachments.erase(g_ai.attachments.begin() + static_cast<long>(i));
            ImGui::PopID();
            break;
        }
        ImGui::PopID();
    }
    if (!g_ai.attach_error.empty())
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.40f, 1.0f), "%s", g_ai.attach_error.c_str());

    ImGui::Spacing();
    ImGui::SetNextItemWidth(-1);
    ImGui::Combo("##model", &g_ai.model_choice, kModelLabels, 3);

    ImGui::Spacing();

    // ---- generate --
    ImGui::BeginDisabled(g_ai.running || !have_key);
    if (ImGui::Button(ICON_FA_WAND_MAGIC_SPARKLES "  Design it", ImVec2(-1, 0)))
        start_request(g_ai.prompt_buf);
    ImGui::EndDisabled();
    if (!have_key && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Paste an API key above first.");

    if (g_ai.running) {
        const auto secs = std::chrono::duration_cast<std::chrono::seconds>(
                              std::chrono::steady_clock::now() - g_ai.started).count();
        ImGui::Text("Thinking... %llds", static_cast<long long>(secs));
        ImGui::TextDisabled("A careful design takes a minute or two. The app stays usable.");
    }

    if (!g_ai.error.empty()) {
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.45f, 0.40f, 1.0f));
        ImGui::TextWrapped(ICON_FA_TRIANGLE_EXCLAMATION "  %s", g_ai.error.c_str());
        ImGui::PopStyleColor();
        if (!g_ai.raw_json.empty()) {
            ImGui::TextDisabled("Ask again and mention what was wrong - the reply is below.");
        }
    }

    // ---- the result --
    if (!g_ai.raw_json.empty()) {
        ImGui::Spacing();
        ImGui::Separator();
        if (g_ai.parsed) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_CheckMark]);
            ImGui::TextWrapped(ICON_FA_CHECK "  %s", g_ai.summary.c_str());
            ImGui::PopStyleColor();
        }
        if (g_ai.out_tokens > 0)
            ImGui::TextDisabled("%d tokens in, %d out.", g_ai.in_tokens, g_ai.out_tokens);

        if (ImGui::CollapsingHeader(ICON_FA_FILE "  The scene file it wrote")) {
            ImGui::InputTextMultiline("##raw", g_ai.raw_json.data(), g_ai.raw_json.size() + 1,
                                      ImVec2(-1, ImGui::GetTextLineHeight() * 12.0f),
                                      ImGuiInputTextFlags_ReadOnly);
        }

        ImGui::BeginDisabled(!g_ai.parsed || !ctx.load_scene);
        if (ImGui::Button(ICON_FA_PLAY "  Open it", ImVec2(-1, 0))) {
            std::string err;
            const auto  path = write_generated(err);
            if (path.empty()) {
                g_ai.error = err;
            } else {
                // Deferred to the end of the frame by the Viewer; see PanelContext::load_scene.
                ctx.load_scene(path);
                *ctx.ai_overlay_open = false;
            }
        }
        ImGui::EndDisabled();
        if (g_ai.parsed && ImGui::IsItemHovered())
            ImGui::SetTooltip("Saves it under your user profile and loads it, replacing the\n"
                              "scene that is open now.");
    }

    ImGui::Spacing();
    if (ImGui::Button(ICON_FA_XMARK "  Close", ImVec2(-1, 0))) *ctx.ai_overlay_open = false;

    ImGui::EndPopup();
}

} // namespace scrt::viz
