// MkAudio executable: SDL3 window + SDL_Renderer + Dear ImGui (docking). The actual screen is App.
//   MkAudio [--project <MealKit project folder>] [--select <event>] [--frames N --capture <png>]
//   MkAudio --golden --project <folder> [--update] [--event <name>]   golden render tests of the project's events without a window
//     (<project>/tests/audio/<event>.golden, mkaudio/Golden.h): "GOLDEN ok|new|updated|FAIL <event> ...", exit code 1 on a failure
#include "App.h"
#include "mkaudio/Golden.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <dwmapi.h>
#endif

namespace {

std::filesystem::path pathOf(const std::string& utf8) { return std::filesystem::path(std::u8string(utf8.begin(), utf8.end())); }
std::string utf8Of(const std::filesystem::path& p) {
    const std::u8string s = p.u8string();
    return std::string(s.begin(), s.end());
}

// Golden render tests: a missing golden file is written (new), --update rewrites all, otherwise each render is compared with its file
int runGolden(const std::string& project, bool update, const std::string& only) {
    const std::filesystem::path assets = pathOf(project) / "assets";
    mkaudio::EventFile file;
    std::vector<mkaudio::ParseMessage> messages;
    if (!mkaudio::loadEventFile(utf8Of(assets / "sounds.events"), file, &messages)) {
        std::printf("GOLDEN FAIL cannot read %s\n", utf8Of(assets / "sounds.events").c_str());
        return 1;
    }
    mkaudio::resolveClips(file, [&](const std::string& name) {
        std::error_code ec;
        const std::filesystem::path p = assets / pathOf(name);
        return std::filesystem::is_regular_file(p, ec) ? utf8Of(p) : std::string();
    });
    const std::filesystem::path dir = pathOf(project) / "tests" / "audio";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    int failed = 0, count = 0;
    for (const mkaudio::EventDef& e : file.events) {
        if (!only.empty() && e.name != only) continue;
        ++count;
        mkaudio::Envelope actual;
        std::string error;
        if (!mkaudio::renderEnvelope(file, e.name, actual, error)) {
            std::printf("GOLDEN FAIL %s: %s\n", e.name.c_str(), error.c_str());
            ++failed;
            continue;
        }
        const std::filesystem::path golden = dir / (e.name + ".golden");
        std::ifstream in(golden, std::ios::binary);
        std::stringstream text;
        text << in.rdbuf();
        const bool exists = static_cast<bool>(in);
        in.close();
        if (update || !exists) {
            std::ofstream(golden, std::ios::binary) << mkaudio::writeEnvelope(actual);
            std::printf("GOLDEN %s %s (%.2f s)\n", exists ? "updated" : "new", e.name.c_str(),
                        static_cast<double>(actual.rms.size()) * actual.blockFrames / actual.sampleRate);
            continue;
        }
        mkaudio::Envelope expected;
        if (!mkaudio::parseEnvelope(text.str(), expected, error)) {
            std::printf("GOLDEN FAIL %s: %s: %s\n", e.name.c_str(), utf8Of(golden).c_str(), error.c_str());
            ++failed;
            continue;
        }
        const mkaudio::GoldenDiff diff = mkaudio::compareEnvelopes(expected, actual);
        if (diff.same) std::printf("GOLDEN ok %s\n", e.name.c_str());
        else {
            std::printf("GOLDEN FAIL %s: %s (rewrite with --update if intended)\n", e.name.c_str(), diff.reason.c_str());
            ++failed;
        }
    }
    if (!only.empty() && count == 0) {
        std::printf("GOLDEN FAIL no event named %s\n", only.c_str());
        ++failed;
    }
    std::printf("GOLDEN %s: %d event(s), %d failed\n", failed ? "FAIL" : "PASS", count, failed);
    std::fflush(stdout);
    return failed ? 1 : 0;
}

// Segoe UI + Malgun Gothic (Korean names) + the icon font (engine docs/TOOL_DESIGN.md section 2), all at one size.
// Returns whether the icon font loaded: without it icon buttons show their names (App::iconButton)
bool loadFont(float size) {
    ImGuiIO& io = ImGui::GetIO();
    const char* path = "C:\\Windows\\Fonts\\segoeui.ttf";
    if (!SDL_GetPathInfo(path, nullptr) || !io.Fonts->AddFontFromFileTTF(path, size)) {
        ImFontConfig config;
        config.SizePixels = size;
        io.Fonts->AddFontDefaultVector(&config);
    }
    ImFontConfig merge;
    merge.MergeMode = true;
    const char* korean = "C:\\Windows\\Fonts\\malgun.ttf";
    if (SDL_GetPathInfo(korean, nullptr)) io.Fonts->AddFontFromFileTTF(korean, size, &merge);
    // Segoe Fluent Icons on Windows 11, Segoe MDL2 Assets on 10: the editor's code points
    for (const char* icons : {"C:\\Windows\\Fonts\\SegoeIcons.ttf", "C:\\Windows\\Fonts\\segmdl2.ttf"}) {
        ImFontConfig config;
        config.MergeMode = true;
        config.GlyphOffset = ImVec2(0.0f, 3.0f);
        if (SDL_GetPathInfo(icons, nullptr) && io.Fonts->AddFontFromFileTTF(icons, size, &config)) return true;
    }
    return false;
}

// The MealKit editor's look (module-debug src/mealkit/debug/ImGuiLayer.cpp applyStyle), copied since MkAudio doesn't link the engine
// (engine docs/TOOL_DESIGN.md section 3). Keep it the same as the original.
void applyEditorStyle(float scale) {
    ImGui::StyleColorsDark();
    ImGuiStyle& s = ImGui::GetStyle();
    // Spectrum 2 shape (engine docs/TOOL_DESIGN.md 3.1): 8 px fields, 10 px panels and popups.
    // Panels are separated by space and a darker gutter, not by lines
    s.WindowRounding = 10.0f;
    s.ChildRounding = 10.0f;
    s.PopupRounding = 10.0f;
    s.FrameRounding = 8.0f;
    s.GrabRounding = 4.0f;
    s.TabRounding = 8.0f;
    s.ScrollbarRounding = 6.0f;
    s.ScrollbarSize = 10.0f;
    s.WindowBorderSize = 0.0f; // layers are told apart by color, not outlines
    s.ChildBorderSize = 0.0f;
    s.FrameBorderSize = 0.0f;
    s.PopupBorderSize = 1.0f;
    s.TabBarBorderSize = 0.0f;
    s.TabBarOverlineSize = 0.0f;
    s.DockingSeparatorSize = 4.0f;
    s.DockingNodeHasCloseButton = false; // each tab has its own close button
    s.TabCloseButtonMinWidthSelected = 0.0f; // tabs show it only while hovered
    s.TabCloseButtonMinWidthUnselected = 0.0f;
    s.WindowPadding = ImVec2(10.0f, 10.0f);
    s.FramePadding = ImVec2(7.0f, 4.0f);
    s.ItemSpacing = ImVec2(8.0f, 6.0f);
    s.ItemInnerSpacing = ImVec2(6.0f, 4.0f);
    s.CellPadding = ImVec2(6.0f, 3.0f);
    s.IndentSpacing = 16.0f;
    s.WindowMenuButtonPosition = ImGuiDir_None; // no triangle on every dock tab bar: tabs are all visible and can be dragged
    // Tokens of engine docs/TOOL_DESIGN.md 3.1: Spectrum 2 dark gray-25..gray-900 (gray 1..12) and blue. Pick a step by its job, never a color by eye
    const auto rgb = [](unsigned v, float a = 1.0f) {
        return ImVec4(((v >> 16) & 0xFF) / 255.0f, ((v >> 8) & 0xFF) / 255.0f, (v & 0xFF) / 255.0f, a);
    };
    const auto gray = [&](int step) {
        static constexpr unsigned kGray[13] = {0, 0x111111, 0x1B1B1B, 0x222222, 0x2C2C2C, 0x323232, 0x393939,
                                          0x444444, 0x6D6D6D, 0x8A8A8A, 0x909090, 0xAFAFAF, 0xEBEBEB};
        return rgb(kGray[step]);
    };
    const ImVec4 clear(0.0f, 0.0f, 0.0f, 0.0f);
    const ImVec4 focus = rgb(0x5D89FF);  // keyboard focus, hovered splitters
    const ImVec4 accent = rgb(0x3B63FB); // selection, check marks, active grabs
    ImVec4* c = s.Colors;
    c[ImGuiCol_Text] = gray(12);
    c[ImGuiCol_TextDisabled] = gray(11); // secondary text: paths, dates, hints
    c[ImGuiCol_TextSelectedBg] = rgb(0x3B63FB, 0.45f);
    c[ImGuiCol_WindowBg] = gray(2);
    c[ImGuiCol_ChildBg] = clear;
    c[ImGuiCol_PopupBg] = gray(3);
    c[ImGuiCol_Border] = gray(6);
    c[ImGuiCol_BorderShadow] = clear;
    c[ImGuiCol_MenuBarBg] = gray(1);
    c[ImGuiCol_TitleBg] = gray(1);
    c[ImGuiCol_TitleBgActive] = gray(1);
    c[ImGuiCol_TitleBgCollapsed] = gray(1);
    // Hover is one neutral step up, pressed and selected one more; the accent never fills a hovered control
    c[ImGuiCol_FrameBg] = gray(4); // fields and buttons share a step, so a combo and its arrow read as one control
    c[ImGuiCol_FrameBgHovered] = gray(5);
    c[ImGuiCol_FrameBgActive] = gray(6);
    c[ImGuiCol_Header] = gray(5);
    c[ImGuiCol_HeaderHovered] = gray(4);
    c[ImGuiCol_HeaderActive] = gray(6);
    c[ImGuiCol_Button] = gray(4);
    c[ImGuiCol_ButtonHovered] = gray(6);
    c[ImGuiCol_ButtonActive] = gray(7);
    c[ImGuiCol_CheckMark] = accent;
    c[ImGuiCol_SliderGrab] = gray(9);
    c[ImGuiCol_SliderGrabActive] = accent;
    c[ImGuiCol_Separator] = gray(5);
    c[ImGuiCol_SeparatorHovered] = focus;
    c[ImGuiCol_SeparatorActive] = accent;
    c[ImGuiCol_ResizeGrip] = clear;
    c[ImGuiCol_ResizeGripHovered] = focus;
    c[ImGuiCol_ResizeGripActive] = accent;
    c[ImGuiCol_ScrollbarBg] = clear;
    c[ImGuiCol_ScrollbarGrab] = gray(6);
    c[ImGuiCol_ScrollbarGrabHovered] = gray(7);
    c[ImGuiCol_ScrollbarGrabActive] = gray(8);
    // The selected tab joins its panel; the others sit on the strip
    c[ImGuiCol_Tab] = clear;
    c[ImGuiCol_TabHovered] = gray(4);
    c[ImGuiCol_TabSelected] = gray(2);
    c[ImGuiCol_TabSelectedOverline] = clear;
    c[ImGuiCol_TabDimmed] = clear;
    c[ImGuiCol_TabDimmedSelected] = gray(2);
    c[ImGuiCol_TabDimmedSelectedOverline] = clear;
    c[ImGuiCol_TableHeaderBg] = clear; // no header band: quiet header text over a line
    c[ImGuiCol_TableBorderStrong] = gray(6);
    c[ImGuiCol_TableBorderLight] = gray(5);
    c[ImGuiCol_TableRowBgAlt] = clear; // no zebra rows
    c[ImGuiCol_DockingPreview] = rgb(0x3B63FB, 0.5f);
    c[ImGuiCol_DockingEmptyBg] = clear;
    c[ImGuiCol_NavCursor] = focus;
    s.ScaleAllSizes(scale);
    s.FontScaleDpi = scale;
}

// Dark title bar in gray 1 #111111 (editor kEditorCaptionColor, docs/TOOL_DESIGN.md 3.1). Ignored where Windows doesn't support it
void applyEditorCaption(SDL_Window* window) {
#if defined(_WIN32)
    HWND hwnd = static_cast<HWND>(SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
    if (!hwnd) return;
    const BOOL dark = TRUE;
    DwmSetWindowAttribute(hwnd, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark, sizeof(dark));
    const COLORREF caption = RGB(0x11, 0x11, 0x11);
    DwmSetWindowAttribute(hwnd, 35 /* DWMWA_CAPTION_COLOR */, &caption, sizeof(caption));
#else
    (void)window;
#endif
}

} // namespace

int main(int argc, char** argv) {
    std::string project, capture, select, onlyEvent;
    bool golden = false, update = false;
    long frames = 0; // --frames N: quit after N frames (--capture saves the last screen as PNG, for docs/tests)
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--project") == 0 && i + 1 < argc) project = argv[++i];
        else if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc) frames = std::strtol(argv[++i], nullptr, 10);
        else if (std::strcmp(argv[i], "--capture") == 0 && i + 1 < argc) capture = argv[++i];
        else if (std::strcmp(argv[i], "--select") == 0 && i + 1 < argc) select = argv[++i];
        else if (std::strcmp(argv[i], "--golden") == 0) golden = true;
        else if (std::strcmp(argv[i], "--update") == 0) update = true;
        else if (std::strcmp(argv[i], "--event") == 0 && i + 1 < argc) onlyEvent = argv[++i];
        else if (std::strcmp(argv[i], "--version") == 0) {
            SDL_Log("MkAudio %s", MKAUDIO_VERSION);
            return 0;
        }
    }

    if (golden) {
        if (project.empty()) {
            std::printf("--golden needs --project <folder>\n");
            return 1;
        }
        return runGolden(project, update, onlyEvent);
    }
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return 1;
    }
    const float scale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
    SDL_Window* window = SDL_CreateWindow("MkAudio", static_cast<int>(1400 * scale), static_cast<int>(860 * scale),
                                          SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_HIDDEN);
    SDL_Renderer* renderer = window ? SDL_CreateRenderer(window, nullptr) : nullptr;
    if (!renderer) {
        SDL_Log("Cannot create the window: %s", SDL_GetError());
        return 1;
    }
    SDL_SetRenderVSync(renderer, 1);
    SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    applyEditorCaption(window);
    SDL_ShowWindow(window);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_DockingEnable;
    // Window layout goes in the user folder (not left in the project)
    static std::string ini;
    if (char* pref = SDL_GetPrefPath("MealKit", "MealKit Audio") /* kept: settings folder from before the MkAudio rename */) {
        ini = std::string(pref) + "imgui.ini";
        SDL_free(pref);
        io.IniFilename = ini.c_str();
    }
    applyEditorStyle(scale);
    const bool iconFont = loadFont(16.0f);
    ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer3_Init(renderer);

    mkaudio::tool::App app;
    app.window = window;
    app.iconFont = iconFont;
    app.init(project);
    if (!select.empty()) app.select(select);

    Uint64 last = SDL_GetPerformanceCounter();
    std::string shownTitle;
    long frame = 0;
    for (bool running = true; running;) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL3_ProcessEvent(&event);
            if (event.type == SDL_EVENT_QUIT) app.requestQuit();
            if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && event.window.windowID == SDL_GetWindowID(window)) app.requestQuit();
        }
        if (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED) {
            SDL_Delay(10);
            continue;
        }
        const Uint64 now = SDL_GetPerformanceCounter();
        const double dt = static_cast<double>(now - last) / static_cast<double>(SDL_GetPerformanceFrequency());
        last = now;

        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        app.frame(dt);
        ImGui::Render();
        if (app.canQuit()) running = false;

        const std::string title = app.title();
        if (title != shownTitle) {
            SDL_SetWindowTitle(window, title.c_str());
            shownTitle = title;
        }
        SDL_SetRenderScale(renderer, io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y);
        SDL_SetRenderDrawColorFloat(renderer, 0x11 / 255.0f, 0x11 / 255.0f, 0x11 / 255.0f, 1.0f); // gray 1
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
        if (frames > 0 && ++frame >= frames) {
            if (!capture.empty()) {
                SDL_Surface* shot = SDL_RenderReadPixels(renderer, nullptr);
                if (!shot || !SDL_SavePNG(shot, capture.c_str())) SDL_Log("Cannot save %s: %s", capture.c_str(), SDL_GetError());
                SDL_DestroySurface(shot);
            }
            running = false;
        }
        SDL_RenderPresent(renderer);
    }

    app.shutdown();
    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
