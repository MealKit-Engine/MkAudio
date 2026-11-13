#include "App.h"
#include "Strings.h"

#include <mkcontract/User.h>

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cstring>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <functional>
#include <sstream>

namespace mkaudio::tool {
namespace {

constexpr const char* kEventsFile = "sounds.events"; // relative to assets
constexpr const char* kOldTable = "sounds.table";    // MealKit engine 0.5 format (loaded and migrated)
constexpr size_t kUndoDepth = 200;
// Glyphs from engine docs/TOOL_DESIGN.md section 2 (Segoe Fluent Icons / MDL2 Assets)
constexpr const char* kIconAdd = "\xEE\x9C\x90";    // U+E710
constexpr const char* kIconRemove = "\xEE\x9C\xB8"; // U+E738 remove from a list
constexpr const char* kIconCopy = "\xEE\xA3\x88";   // U+E8C8
constexpr const char* kIconUp = "\xEE\x9D\x8A";     // U+E74A move up
constexpr const char* kIconPlay = "\xEE\x9D\xA8";   // U+E768
constexpr const char* kIconStop = "\xEE\x9C\x9A";   // U+E71A
constexpr const char* kIconLaunch = "\xEE\xA2\xA7"; // U+E8A7 open in its program
// Panel tabs (engine docs/TOOL_DESIGN.md section 2 "Panels")
constexpr const char* kIconAudio = "\xEE\xA3\x96";     // U+E8D6 Events
constexpr const char* kIconInspector = "\xEE\xA7\xA9"; // U+E9E9 Event
constexpr const char* kIconLayers = "\xEE\xA0\x9E";    // U+E81E MapLayers: Buses
constexpr const char* kIconPreview = "\xEE\x9F\xB4";   // U+E7F4
constexpr const char* kIconMusic = "\xEE\xB1\x8F";     // U+EC4F MusicNote
constexpr const char* kIconVolume = "\xEE\x9D\xA7";    // U+E767 Mix
constexpr const char* kIconConnect = "\xEE\x9C\x83";   // U+E703 Live
constexpr const char* kIconLanguage = "\xEF\x8A\xB7";  // U+F2B7 LocaleLanguage
const char* const kStandardBuses[] = {"master", "music", "sfx", "ui", "voice"}; // buses the runtime creates from the start

// Checkbox and radio colors (engine docs/TOOL_DESIGN.md 3.1, 3.2): a gray 4 box on a gray 2 panel is about 1.25:1, so an unchecked
// box gets a gray 9 outline (gray 11 hovered) and a checked one the accent fill with a gray 12 mark. Same as MkRender tool/App.cpp
const ImVec4 kControlOutline(0x8A / 255.0f, 0x8A / 255.0f, 0x8A / 255.0f, 1.0f);      // gray 9
const ImVec4 kControlOutlineHover(0xAF / 255.0f, 0xAF / 255.0f, 0xAF / 255.0f, 1.0f); // gray 11
const ImVec4 kControlMark(0xEB / 255.0f, 0xEB / 255.0f, 0xEB / 255.0f, 1.0f);         // gray 12
const ImVec4 kAccent(0x3B / 255.0f, 0x63 / 255.0f, 0xFB / 255.0f, 1.0f);
const ImVec4 kAccentHover(0x4B / 255.0f, 0x75 / 255.0f, 0xFF / 255.0f, 1.0f);
const ImVec4 kAccentDown(0x27 / 255.0f, 0x4D / 255.0f, 0xEA / 255.0f, 1.0f);

// Pushes the fill and mark of a checkbox or radio button in the state it is drawn in; popControlColors after
void pushControlColors(bool on) {
    const ImVec4 clear(0.0f, 0.0f, 0.0f, 0.0f);
    const ImGuiStyle& s = ImGui::GetStyle();
    ImGui::PushStyleColor(ImGuiCol_FrameBg, on ? kAccent : clear);
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, on ? kAccentHover : s.Colors[ImGuiCol_FrameBg]);
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, on ? kAccentDown : s.Colors[ImGuiCol_FrameBgHovered]);
    ImGui::PushStyleColor(ImGuiCol_CheckMark, kControlMark);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, s.FrameRounding * 0.5f); // a 4 px box, not the round 8 px of fields
}

void popControlColors() {
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(4);
}

// Every checkbox goes through here, never ImGui::Checkbox (engine docs/TOOL_DESIGN.md 3.2)
bool checkbox(const char* label, bool* v) {
    const bool on = *v;
    pushControlColors(on);
    const bool changed = ImGui::Checkbox(label, v);
    const float rounding = ImGui::GetStyle().FrameRounding;
    popControlColors();
    if (!on) {
        // The box is the square at the start of the item (ImGui::Checkbox check_bb)
        const ImVec2 p = ImGui::GetItemRectMin();
        const float size = ImGui::GetFrameHeight();
        const float width = std::max(1.0f, ImGui::GetStyle().FontScaleDpi);
        const float inset = width * 0.5f;
        ImGui::GetWindowDrawList()->AddRect({p.x + inset, p.y + inset}, {p.x + size - inset, p.y + size - inset},
                                            ImGui::GetColorU32(ImGui::IsItemHovered() ? kControlOutlineHover : kControlOutline), rounding, 0, width);
    }
    return changed;
}

bool radioButton(const char* label, bool active) {
    pushControlColors(active);
    const bool pressed = ImGui::RadioButton(label, active);
    popControlColors();
    if (!active) {
        // Same circle as ImGui::RadioButton: radius (square - 1) / 2 around the square's center
        const ImVec2 p = ImGui::GetItemRectMin();
        const float size = ImGui::GetFrameHeight();
        const float width = std::max(1.0f, ImGui::GetStyle().FontScaleDpi);
        const ImVec2 center(std::floor(p.x + size * 0.5f + 0.5f), std::floor(p.y + size * 0.5f + 0.5f));
        ImGui::GetWindowDrawList()->AddCircle(center, (size - 1.0f) * 0.5f - width * 0.5f,
                                              ImGui::GetColorU32(ImGui::IsItemHovered() ? kControlOutlineHover : kControlOutline), 0, width);
    }
    return pressed;
}

std::filesystem::path pathOf(const std::string& utf8) { return std::filesystem::path(std::u8string(utf8.begin(), utf8.end())); }

std::string utf8Of(const std::filesystem::path& p) {
    const std::u8string s = p.u8string();
    return std::string(s.begin(), s.end());
}

bool readFile(const std::filesystem::path& p, std::string& out) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return false;
    std::ostringstream text;
    text << in.rdbuf();
    out = text.str();
    return true;
}

// One curve: a straight line from value atMin to atMax over parameter min (left) to max (right). Drag the endpoints up/down to change it. t = preview position (0..1)
bool curveEditor(const char* id, float& atMin, float& atMax, float lo, float hi, float t, const char* format) {
    bool changed = false;
    const ImVec2 size(ImGui::GetContentRegionAvail().x, 70.0f * ImGui::GetStyle().FontScaleDpi);
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    const ImVec2 p1(p0.x + size.x, p0.y + size.y);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p0, p1, ImGui::GetColorU32(ImGuiCol_FrameBg), 4.0f);
    const float pad = 8.0f;
    auto yOf = [&](float v) { return p1.y - pad - (std::clamp(v, lo, hi) - lo) / (hi - lo) * (size.y - 2 * pad); };
    const ImVec2 a(p0.x + pad, yOf(atMin)), b(p1.x - pad, yOf(atMax));
    dl->AddLine(a, b, ImGui::GetColorU32(ImGuiCol_PlotLines), 2.0f);
    const float tx = a.x + (b.x - a.x) * t;
    dl->AddLine(ImVec2(tx, p0.y + 2), ImVec2(tx, p1.y - 2), ImGui::GetColorU32(ImGuiCol_TextDisabled));
    for (int i = 0; i < 2; ++i) {
        float& v = i == 0 ? atMin : atMax;
        const ImVec2 c = i == 0 ? a : b;
        ImGui::SetCursorScreenPos(ImVec2(c.x - 7, c.y - 7));
        ImGui::PushID(id);
        ImGui::InvisibleButton(i == 0 ? "##min" : "##max", ImVec2(14, 14));
        const bool active = ImGui::IsItemActive(), hovered = ImGui::IsItemHovered();
        ImGui::PopID();
        if (active) {
            const float delta = -ImGui::GetIO().MouseDelta.y / (size.y - 2 * pad) * (hi - lo);
            v = std::clamp(v + delta, lo, hi);
            changed = true;
        }
        dl->AddCircleFilled(c, active || hovered ? 6.0f : 4.5f, ImGui::GetColorU32(active ? ImGuiCol_SliderGrabActive : ImGuiCol_SliderGrab));
        if (hovered || active) ImGui::SetTooltip(format, static_cast<double>(v));
    }
    ImGui::SetCursorScreenPos(ImVec2(p0.x, p1.y + 4));
    ImGui::Dummy(ImVec2(0, 0));
    return changed;
}

// Distance attenuation graph: 1 up to min, 0 at max (same linear model as the runtime). d = preview distance
void attenuationPlot(float minDistance, float maxDistance, float d) {
    const ImVec2 size(ImGui::GetContentRegionAvail().x, 60.0f * ImGui::GetStyle().FontScaleDpi);
    const ImVec2 p0 = ImGui::GetCursorScreenPos(), p1(p0.x + size.x, p0.y + size.y);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p0, p1, ImGui::GetColorU32(ImGuiCol_FrameBg), 4.0f);
    const float range = std::max(maxDistance * 1.2f, 1.0f), pad = 6.0f;
    auto xOf = [&](float v) { return p0.x + pad + v / range * (size.x - 2 * pad); };
    const float top = p0.y + pad, bottom = p1.y - pad;
    const ImU32 color = ImGui::GetColorU32(ImGuiCol_PlotLines);
    dl->AddLine(ImVec2(xOf(0), top), ImVec2(xOf(minDistance), top), color, 2.0f);
    dl->AddLine(ImVec2(xOf(minDistance), top), ImVec2(xOf(maxDistance), bottom), color, 2.0f);
    dl->AddLine(ImVec2(xOf(maxDistance), bottom), ImVec2(xOf(range), bottom), color, 2.0f);
    dl->AddLine(ImVec2(xOf(std::min(d, range)), p0.y + 2), ImVec2(xOf(std::min(d, range)), p1.y - 2), ImGui::GetColorU32(ImGuiCol_TextDisabled));
    ImGui::Dummy(size);
}

bool knownBus(const EventFile& f, const std::string& name) {
    for (const char* b : kStandardBuses)
        if (name == b) return true;
    for (const BusDef& b : f.buses)
        if (b.name == name) return true;
    return false;
}

} // namespace

// ---------------------------------------------------------------- Start/end

bool App::init(const std::string& projectDir) {
    Desc d;
    d.log = [this](LogLevel, const std::string& text) {
        m_status = trf("Audio: %s", text.c_str());
        m_statusTime = m_time;
    };
    loadLanguage();
    if (!languageOverride.empty()) setLanguage(languageOverride);
    m_audio = m_mixer.init(d);
    if (!m_audio) m_status = tr("No audio output device: preview is off");
    if (!projectDir.empty()) openProject(projectDir);
    return true;
}

void App::shutdown() { m_mixer.shutdown(); }

std::string App::title() const {
    if (m_project.empty()) return "MkAudio";
    const bool dirty = writeEvents(m_file) != m_savedText;
    return std::string(dirty ? "* " : "") + utf8Of(pathOf(m_project).filename()) + " - MkAudio";
}

void App::setPendingProject(const std::string& dir) {
    std::lock_guard<std::mutex> lock(m_pendingMutex);
    m_pendingProject = dir;
}

void App::select(const std::string& event) {
    for (size_t i = 0; i < m_file.events.size(); ++i)
        if (m_file.events[i].name == event) m_selected = static_cast<int>(i);
}

bool App::openProject(const std::string& dir) {
    const std::filesystem::path root = pathOf(dir);
    std::error_code ec;
    if (!std::filesystem::is_directory(root / "assets", ec)) {
        m_status = trf("Not a MealKit project (no assets folder): %s", dir.c_str());
        m_statusTime = m_time;
        return false;
    }
    m_project = utf8Of(std::filesystem::absolute(root, ec));
    m_selected = -1;
    m_undo.clear();
    m_redo.clear();
    load();
    scanClips();
    refreshLive(true);
    return true;
}

std::string App::assetsDir() const { return utf8Of(pathOf(m_project) / "assets"); }
std::string App::eventsPath() const { return utf8Of(pathOf(m_project) / "assets" / kEventsFile); }

std::string App::resolveClip(const std::string& name) const {
    std::error_code ec;
    const std::filesystem::path p = pathOf(m_project) / "assets" / pathOf(name);
    return std::filesystem::is_regular_file(p, ec) ? utf8Of(p) : std::string();
}

void App::scanClips() {
    m_clipFiles.clear();
    std::error_code ec;
    const std::filesystem::path root = pathOf(assetsDir());
    for (auto it = std::filesystem::recursive_directory_iterator(root, ec); !ec && it != std::filesystem::recursive_directory_iterator();
         it.increment(ec)) {
        std::string ext = utf8Of(it->path().extension());
        std::transform(ext.begin(), ext.end(), ext.begin(), [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); });
        if (ext != ".wav" && ext != ".ogg") continue;
        std::string rel = utf8Of(std::filesystem::relative(it->path(), root, ec));
        std::replace(rel.begin(), rel.end(), '\\', '/');
        m_clipFiles.push_back(rel);
    }
    std::sort(m_clipFiles.begin(), m_clipFiles.end());
}

bool App::load() {
    m_messages.clear();
    m_externalChange = false;
    std::error_code ec;
    const std::filesystem::path path = pathOf(eventsPath());
    if (!std::filesystem::exists(path, ec)) {
        m_file = {};
        m_savedText = writeEvents(m_file);
        m_fileTime = {};
        m_status = tr("No assets/sounds.events yet: add events and save to create it");
    } else {
        loadEventFile(eventsPath(), m_file, &m_messages);
        m_savedText = writeEvents(m_file);
        m_fileTime = std::filesystem::last_write_time(path, ec);
        m_status = trf("Loaded assets/sounds.events: %d event(s)", static_cast<int>(m_file.events.size()));
    }
    m_statusTime = m_time;
    m_lastSnapshot = m_savedText;
    if (m_selected >= static_cast<int>(m_file.events.size())) m_selected = -1;
    if (m_selected < 0 && !m_file.events.empty()) m_selected = 0;
    applyToMixer();
    return true;
}

bool App::save() {
    if (m_project.empty()) return false;
    if (!saveEventFile(eventsPath(), m_file)) {
        m_status = trf("Cannot write %s", eventsPath().c_str());
        m_statusTime = m_time;
        return false;
    }
    std::error_code ec;
    m_savedText = writeEvents(m_file);
    m_fileTime = std::filesystem::last_write_time(pathOf(eventsPath()), ec);
    m_externalChange = false;
    m_status = tr("Saved assets/sounds.events");
    m_statusTime = m_time;
    reloadInGames(); // running games/editors also reread by file time, but are notified so changes are heard right away
    return true;
}

// MealKit 0.5's assets/sounds.table ("row <name>" + the same keys) → events
void App::importTable() {
    std::string text;
    if (!readFile(pathOf(m_project) / "assets" / kOldTable, text)) return;
    std::string converted;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.rfind("@format", 0) == 0 || line.rfind("#", 0) == 0) continue;
        if (line.rfind("row ", 0) == 0) line = "event " + line.substr(4);
        converted += line + "\n";
    }
    EventFile imported;
    m_messages.clear();
    parseEvents(converted, imported, &m_messages);
    m_file.events = imported.events;
    m_selected = m_file.events.empty() ? -1 : 0;
    m_status = trf("Imported %d event(s) from assets/sounds.table: save to write sounds.events", static_cast<int>(m_file.events.size()));
    m_statusTime = m_time;
}

void App::checkExternalChange() {
    if (m_project.empty() || m_time < m_fileCheck) return;
    m_fileCheck = m_time + 0.5;
    std::error_code ec;
    const auto t = std::filesystem::last_write_time(pathOf(eventsPath()), ec);
    if (ec || t == m_fileTime) return;
    if (writeEvents(m_file) == m_savedText) load(); // if nothing was edited, just reread
    else m_externalChange = true;
}

void App::applyToMixer() {
    const std::string text = writeEvents(m_file);
    if (!m_audio || text == m_appliedText) return;
    m_appliedText = text;
    EventFile resolved = m_file;
    resolveClips(resolved, [this](const std::string& name) { return resolveClip(name); });
    m_mixer.setBuses(resolved.buses);
    m_mixer.setEvents(resolved.events);
    m_mixer.setSnapshots(resolved.snapshots);
    m_mixer.setMusic(resolved.music);
}

void App::snapshot() {
    if (ImGui::IsAnyItemActive()) return; // only once while dragging (on release)
    const std::string text = writeEvents(m_file);
    if (text == m_lastSnapshot) return;
    m_undo.push_back(m_lastSnapshot);
    if (m_undo.size() > kUndoDepth) m_undo.pop_front();
    m_redo.clear();
    m_lastSnapshot = text;
}

void App::undo(bool redo) {
    auto& from = redo ? m_redo : m_undo;
    auto& to = redo ? m_undo : m_redo;
    if (from.empty()) return;
    to.push_back(m_lastSnapshot);
    m_lastSnapshot = from.back();
    from.pop_back();
    parseEvents(m_lastSnapshot, m_file);
    if (m_selected >= static_cast<int>(m_file.events.size())) m_selected = static_cast<int>(m_file.events.size()) - 1;
    if (m_musicSelected >= static_cast<int>(m_file.music.size())) m_musicSelected = static_cast<int>(m_file.music.size()) - 1;
    m_musicRenaming = m_snapRenaming = -1;
}

// ---------------------------------------------------------------- Every frame

bool App::iconButton(const char* glyph, const char* name, bool small) {
    const std::string label = std::string(iconFont ? glyph : name) + "##" + name;
    // Flat like the editor's icon buttons: clear until hovered
    if (iconFont) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetStyleColorVec4(ImGuiCol_FrameBgHovered));
    }
    const bool pressed = small ? ImGui::SmallButton(label.c_str()) : iconFont ? ImGui::Button(label.c_str(), {ImGui::GetFrameHeight(), 0}) : ImGui::Button(label.c_str());
    if (iconFont) ImGui::PopStyleColor(2);
    if (iconFont && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s", name);
    return pressed;
}

void App::frame(double dt) {
    m_time += dt;
    {
        std::string pending;
        {
            std::lock_guard<std::mutex> lock(m_pendingMutex);
            pending.swap(m_pendingProject);
        }
        if (!pending.empty()) openProject(pending);
    }
    if (m_audio) m_mixer.update(dt);
    checkExternalChange();
    refreshLive(false);
    if (languageOverride.empty()) followLanguage(); // MealKit Manager or another Mk tool changed the suite language

    const ImGuiIO& io = ImGui::GetIO();
    if (!m_project.empty() && io.KeyCtrl && !io.WantTextInput) {
        if (ImGui::IsKeyPressed(ImGuiKey_S, false)) save();
        if (ImGui::IsKeyPressed(ImGuiKey_Z, false)) undo(io.KeyShift);
        if (ImGui::IsKeyPressed(ImGuiKey_Y, false)) undo(true);
    }

    drawMenu();
    const ImGuiID dock = ImGui::DockSpaceOverViewport(0, nullptr, ImGuiDockNodeFlags_PassthruCentralNode);
    if (m_project.empty()) {
        drawStart();
    } else {
        if (!m_layoutDone) layoutDocks(dock);
        drawEventList();
        drawEventEditor();
        drawBuses();
        drawMusic();
        drawMix();
        drawPreview();
        drawLive();
        applyToMixer();
        snapshot();
    }
    drawQuitDialog();
}

void App::layoutDocks(unsigned dockspace) {
    m_layoutDone = true;
    if (ImGui::DockBuilderGetNode(dockspace) && ImGui::DockBuilderGetNode(dockspace)->IsSplitNode() && ImGui::FindWindowSettingsByID(ImHashStr("###Events")))
        return; // imgui.ini has a layout (one saved before the ### panel ids is laid out again)
    ImGui::DockBuilderRemoveNode(dockspace);
    ImGui::DockBuilderAddNode(dockspace, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspace, ImGui::GetMainViewport()->WorkSize);
    ImGuiID left = 0, center = 0, right = 0, rightBottom = 0;
    ImGui::DockBuilderSplitNode(dockspace, ImGuiDir_Left, 0.22f, &left, &center);
    ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.36f, &right, &center);
    ImGui::DockBuilderSplitNode(right, ImGuiDir_Down, 0.45f, &rightBottom, &right);
    ImGui::DockBuilderDockWindow("###Events", left);
    ImGui::DockBuilderDockWindow("###Event", center);
    ImGui::DockBuilderDockWindow("###Music", center);
    ImGui::DockBuilderDockWindow("###Mix", center);
    ImGui::DockBuilderDockWindow("###Preview", right);
    ImGui::DockBuilderDockWindow("###Buses", rightBottom);
    ImGui::DockBuilderDockWindow("###Live", rightBottom);
    ImGui::DockBuilderFinish(dockspace);
}

void App::drawMenu() {
    if (!ImGui::BeginMainMenuBar()) return;
    if (ImGui::BeginMenu(tr("File"))) {
        if (ImGui::MenuItem(tr("Open Project Folder..."))) {
            SDL_ShowOpenFolderDialog(
                [](void* user, const char* const* list, int) {
                    if (list && list[0]) static_cast<App*>(user)->setPendingProject(list[0]);
                },
                this, static_cast<SDL_Window*>(window), m_project.empty() ? nullptr : m_project.c_str(), false);
        }
        if (ImGui::MenuItem(tr("Save"), "Ctrl+S", false, !m_project.empty())) save();
        if (ImGui::MenuItem(tr("Reload from Disk"), nullptr, false, !m_project.empty())) load();
        std::error_code ec;
        const bool hasTable = !m_project.empty() && std::filesystem::exists(pathOf(m_project) / "assets" / kOldTable, ec);
        if (ImGui::MenuItem(tr("Import assets/sounds.table"), nullptr, false, hasTable)) importTable();
        ImGui::Separator();
        if (ImGui::MenuItem(tr("Quit"))) requestQuit();
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu(tr("Edit"), !m_project.empty())) {
        if (ImGui::MenuItem(tr("Undo"), "Ctrl+Z", false, !m_undo.empty())) undo(false);
        if (ImGui::MenuItem(tr("Redo"), "Ctrl+Y", false, !m_redo.empty())) undo(true);
        ImGui::Separator();
        if (ImGui::MenuItem(tr("Rescan Clips"))) scanClips();
        ImGui::EndMenu();
    }
    if (!m_status.empty() && m_time - m_statusTime < 8.0) {
        ImGui::SameLine(ImGui::GetWindowWidth() * 0.45f);
        ImGui::TextDisabled("%s", m_status.c_str());
    }
    drawLanguageButton();
    ImGui::EndMainMenuBar();
}

void App::drawLanguageButton() {
    // The name in English too while another language is on, so someone who can't read it still knows the button
    const std::string name = std::strcmp(language(), "en") == 0 ? std::string("Language") : std::string(tr("Language")) + " (Language)";
    ImGui::SameLine();
    ImGui::SetCursorPosX(ImGui::GetWindowContentRegionMax().x - ImGui::GetFrameHeight()); // the right end, inside the padding
    if (iconButton(kIconLanguage, name.c_str())) ImGui::OpenPopup("##language");
    if (ImGui::BeginPopup("##language")) {
        for (const Language& l : kLanguages) {
            if (ImGui::MenuItem(l.name, nullptr, std::strcmp(language(), l.code) == 0)) {
                languageOverride.clear();
                if (!saveLanguage(l.code)) {
                    setLanguage(l.code);
                    m_status = trf("Cannot save the language to %s", mkcontract::userPath(mkcontract::kEditorSettings).c_str());
                    m_statusTime = m_time;
                }
            }
        }
        ImGui::EndPopup();
    }
}

std::string App::panelTitle(const char* glyph, const char* name) const {
    return (iconFont ? std::string(glyph) + "  " : std::string()) + tr(name) + "###" + name;
}

void App::drawStart() {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + vp->WorkSize.y * 0.4f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::Begin("##start", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);
    ImGui::TextUnformatted(tr("MkAudio"));
    ImGui::TextDisabled(tr("Sound events for a MealKit project (assets/sounds.events)"));
    ImGui::Spacing();
    if (ImGui::Button(tr("Open Project Folder..."), ImVec2(260, 0)))
        SDL_ShowOpenFolderDialog(
            [](void* user, const char* const* list, int) {
                if (list && list[0]) static_cast<App*>(user)->setPendingProject(list[0]);
            },
            this, static_cast<SDL_Window*>(window), nullptr, false);
    ImGui::TextDisabled(tr("The MealKit editor opens this tool from Tools > MkAudio."));
    if (!m_status.empty()) ImGui::TextDisabled("%s", m_status.c_str());
    ImGui::End();
}

// ---------------------------------------------------------------- Event list

void App::drawEventList() {
    ImGui::Begin(panelTitle(kIconAudio, "Events").c_str());
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##filter", tr("Filter"), m_filter, sizeof(m_filter));
    if (iconButton(kIconAdd, tr("New event"))) {
        EventDef e;
        int n = 1;
        do e.name = "event" + std::to_string(n++);
        while (std::any_of(m_file.events.begin(), m_file.events.end(), [&](const EventDef& o) { return o.name == e.name; }));
        m_file.events.push_back(e);
        m_selected = static_cast<int>(m_file.events.size()) - 1;
    }
    ImGui::SameLine();
    const bool has = m_selected >= 0 && m_selected < static_cast<int>(m_file.events.size());
    ImGui::BeginDisabled(!has);
    if (iconButton(kIconCopy, tr("Duplicate event"))) {
        EventDef e = m_file.events[static_cast<size_t>(m_selected)];
        e.name += "_copy";
        m_file.events.insert(m_file.events.begin() + m_selected + 1, e);
        ++m_selected;
    }
    ImGui::SameLine();
    if (iconButton(kIconRemove, tr("Delete event"))) {
        m_file.events.erase(m_file.events.begin() + m_selected);
        m_selected = std::min(m_selected, static_cast<int>(m_file.events.size()) - 1);
    }
    ImGui::EndDisabled();
    ImGui::Separator();
    if (m_externalChange) {
        ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f), tr("sounds.events changed on disk"));
        if (ImGui::SmallButton(tr("Reload (lose my changes)"))) load();
        ImGui::Separator();
    }
    ImGui::BeginChild("##list");
    for (int i = 0; i < static_cast<int>(m_file.events.size()); ++i) {
        const EventDef& e = m_file.events[static_cast<size_t>(i)];
        if (m_filter[0] && e.name.find(m_filter) == std::string::npos) continue;
        bool missing = false;
        for (const ClipRef& c : e.clips) missing = missing || resolveClip(c.name).empty();
        if (missing || e.clips.empty()) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.55f, 0.45f, 1.0f));
        if (ImGui::Selectable(e.name.c_str(), i == m_selected)) m_selected = i;
        if (missing || e.clips.empty()) ImGui::PopStyleColor();
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
            m_selected = m_previewFor = i;
            m_param = e.paramMin;
            m_preview = m_mixer.play(e.name, {0, 0, m_distance}, {{e.param.c_str(), m_param}});
        }
    }
    ImGui::EndChild();
    ImGui::End();
}

// ---------------------------------------------------------------- Event editing

void App::drawEventEditor() {
    ImGui::Begin(panelTitle(kIconInspector, "Event").c_str());
    if (m_selected < 0 || m_selected >= static_cast<int>(m_file.events.size())) {
        ImGui::TextDisabled(tr("Select or add an event."));
        if (!m_messages.empty()) {
            ImGui::SeparatorText(tr("While reading sounds.events"));
            for (const ParseMessage& m : m_messages) ImGui::TextDisabled(tr("line %u: %s%s"), m.line, m.error ? tr("error: ") : "", m.text.c_str());
        }
        ImGui::End();
        return;
    }
    EventDef& e = m_file.events[static_cast<size_t>(m_selected)];

    // Name (game code: ctx.audio.play("name", ...))
    if (m_renaming != m_selected) {
        std::snprintf(m_rename, sizeof(m_rename), "%s", e.name.c_str());
        m_renaming = m_selected;
    }
    ImGui::SetNextItemWidth(260);
    if (ImGui::InputText(tr("Name"), m_rename, sizeof(m_rename), ImGuiInputTextFlags_EnterReturnsTrue) || ImGui::IsItemDeactivatedAfterEdit()) {
        const std::string name = m_rename;
        const bool taken = std::any_of(m_file.events.begin(), m_file.events.end(), [&](const EventDef& o) { return &o != &e && o.name == name; });
        if (validName(name) && !taken) e.name = name;
        else {
            m_status = taken ? trf("Name already used: %s", name.c_str()) : std::string(tr("Names use letters, digits, _ - . only"));
            m_statusTime = m_time;
            std::snprintf(m_rename, sizeof(m_rename), "%s", e.name.c_str());
        }
    }
    ImGui::SameLine();
    ImGui::TextDisabled(tr("game code: play(\"%s\")"), e.name.c_str());

    // ---- Clips
    ImGui::SeparatorText(tr("Clips"));
    int container = static_cast<int>(e.container);
    ImGui::SetNextItemWidth(160);
    const char* containers[] = {tr("random"), tr("switch"), tr("blend")};
    if (ImGui::Combo(tr("Container"), &container, containers, 3)) e.container = static_cast<Container>(container);
    ImGui::SameLine();
    ImGui::TextDisabled(e.container == Container::Random   ? tr("one clip at random, never the same twice")
                        : e.container == Container::Switch ? tr("the parameter picks clip 0, 1, 2...")
                                                           : tr("all clips crossfaded along the parameter"));
    int removeAt = -1, moveUp = -1;
    for (size_t i = 0; i < e.clips.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        const bool missing = resolveClip(e.clips[i].name).empty();
        if (iconButton(kIconRemove, tr("Remove"), true)) removeAt = static_cast<int>(i);
        ImGui::SameLine();
        ImGui::BeginDisabled(i == 0);
        if (iconButton(kIconUp, tr("Move up"), true)) moveUp = static_cast<int>(i);
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (missing) ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.45f, 1.0f), tr("%zu  %s (missing)"), i, e.clips[i].name.c_str());
        else ImGui::Text(tr("%zu  %s"), i, e.clips[i].name.c_str());
        ImGui::PopID();
    }
    if (removeAt >= 0) e.clips.erase(e.clips.begin() + removeAt);
    if (moveUp > 0) std::swap(e.clips[static_cast<size_t>(moveUp)], e.clips[static_cast<size_t>(moveUp) - 1]);
    ImGui::SetNextItemWidth(320);
    if (ImGui::BeginCombo("##addclip", tr("+ Add clip from assets/"), ImGuiComboFlags_HeightLarge)) {
        static char clipFilter[64] = {};
        ImGui::InputTextWithHint("##cf", tr("Filter"), clipFilter, sizeof(clipFilter));
        for (const std::string& f : m_clipFiles) {
            if (clipFilter[0] && f.find(clipFilter) == std::string::npos) continue;
            if (ImGui::Selectable(f.c_str())) e.clips.push_back({f, {}});
        }
        if (m_clipFiles.empty()) ImGui::TextDisabled(tr("No .wav or .ogg files under assets/"));
        ImGui::EndCombo();
    }

    // ---- Volume/pitch
    ImGui::SeparatorText(tr("Volume and pitch"));
    ImGui::SetNextItemWidth(200);
    ImGui::SliderFloat(tr("Volume"), &e.volume, 0.0f, 2.0f, "%.2f");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(140);
    ImGui::SliderFloat((std::string(tr("+/- random")) + "##v").c_str(), &e.volumeRandom, 0.0f, 0.5f, "%.2f");
    ImGui::SetNextItemWidth(200);
    ImGui::SliderFloat(tr("Pitch"), &e.pitch, 0.25f, 4.0f, "%.2f", ImGuiSliderFlags_Logarithmic);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(140);
    ImGui::SliderFloat((std::string(tr("+/- random")) + "##p").c_str(), &e.pitchRandom, 0.0f, 0.5f, "%.2f");

    // ---- Parameters and curves
    ImGui::SeparatorText(tr("Parameter"));
    char param[64];
    std::snprintf(param, sizeof(param), "%s", e.param.c_str());
    ImGui::SetNextItemWidth(160);
    if (ImGui::InputText((std::string(tr("Name")) + "##param").c_str(), param, sizeof(param))) e.param = param;
    ImGui::SameLine();
    ImGui::SetNextItemWidth(160);
    ImGui::DragFloatRange2(tr("Range"), &e.paramMin, &e.paramMax, 0.05f);
    const float lo = std::min(e.paramMin, e.paramMax), hi = std::max(e.paramMin, e.paramMax);
    const float t = (e.param.empty() || hi == lo) ? 0.0f : std::clamp((m_param - e.paramMin) / (e.paramMax - e.paramMin), 0.0f, 1.0f);
    if (e.param.empty()) {
        ImGui::TextDisabled(tr("No parameter: the curves below are not used. Game code passes it as {{\"name\", value}}."));
    } else {
        ImGui::Text(tr("Volume curve (%.2f -> %.2f)"), e.volumeAtMin, e.volumeAtMax);
        curveEditor("vol", e.volumeAtMin, e.volumeAtMax, 0.0f, 2.0f, t, "%.2f");
        ImGui::Text(tr("Pitch curve (%.2f -> %.2f)"), e.pitchAtMin, e.pitchAtMax);
        curveEditor("pitch", e.pitchAtMin, e.pitchAtMax, 0.25f, 2.0f, t, "%.2f");
        ImGui::Text(tr("Low-pass curve (%.0f -> %.0f Hz, 0 = off)"), e.lowpassAtMin, e.lowpassAtMax);
        curveEditor("lpf", e.lowpassAtMin, e.lowpassAtMax, 0.0f, 20000.0f, t, "%.0f Hz");
    }

    // ---- 3D
    ImGui::SeparatorText("3D");
    checkbox(tr("Positioned in the world"), &e.spatial);
    if (e.spatial) {
        ImGui::SetNextItemWidth(260);
        ImGui::DragFloatRange2(tr("Distance (full, silent)"), &e.minDistance, &e.maxDistance, 0.1f, 0.0f, 1000.0f, "%.1f m", "%.1f m");
        attenuationPlot(e.minDistance, e.maxDistance, m_distance);
        ImGui::SetNextItemWidth(200);
        ImGui::SliderFloat(tr("Behind a wall: volume"), &e.occlusionVolume, 0.0f, 1.0f, "%.2f");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(160);
        ImGui::SliderFloat(tr("low-pass"), &e.occlusionLowpass, 0.0f, 20000.0f, e.occlusionLowpass > 0.0f ? tr("%.0f Hz") : tr("off"), ImGuiSliderFlags_Logarithmic);
        ImGui::TextDisabled(tr("The game checks a line from the listener to the sound; fully blocked uses these values."));
    }

    // ---- Playback
    ImGui::SeparatorText(tr("Playback"));
    ImGui::SetNextItemWidth(160);
    if (ImGui::BeginCombo(tr("Bus"), e.bus.c_str())) {
        for (const char* b : kStandardBuses)
            if (std::string(b) != "master" && ImGui::Selectable(b, e.bus == b)) e.bus = b;
        for (const BusDef& b : m_file.buses)
            if (ImGui::Selectable(b.name.c_str(), e.bus == b.name)) e.bus = b.name;
        ImGui::EndCombo();
    }
    if (!knownBus(m_file, e.bus)) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f), tr("not defined (made under master)"));
    }
    checkbox(tr("Loop"), &e.loop);
    ImGui::SameLine();
    checkbox(tr("Stream from disk (music)"), &e.stream);
    int priority = static_cast<int>(e.priority), maxInstances = static_cast<int>(e.maxInstances);
    ImGui::SetNextItemWidth(160);
    if (ImGui::SliderInt(tr("Priority"), &priority, 0, 100)) e.priority = static_cast<uint32_t>(priority);
    ImGui::SetNextItemWidth(160);
    if (ImGui::SliderInt(tr("Max at once"), &maxInstances, 1, 32)) e.maxInstances = static_cast<uint32_t>(maxInstances);
    ImGui::SameLine();
    int steal = static_cast<int>(e.steal);
    ImGui::SetNextItemWidth(120);
    const char* steals[] = {tr("stop oldest"), tr("stop quietest"), tr("don't play")};
    if (ImGui::Combo(tr("When full"), &steal, steals, 3)) e.steal = static_cast<Steal>(steal);
    ImGui::SetNextItemWidth(160);
    ImGui::DragFloat(tr("Cooldown"), &e.cooldown, 0.01f, 0.0f, 60.0f, "%.2f s");

    // ---- Subtitles
    ImGui::SeparatorText(tr("Subtitle"));
    char subtitle[96];
    std::snprintf(subtitle, sizeof(subtitle), "%s", e.subtitle.c_str());
    ImGui::SetNextItemWidth(220);
    if (ImGui::InputTextWithHint(tr("String key"), tr("e.g. sub.door"), subtitle, sizeof(subtitle))) e.subtitle = subtitle;
    if (!e.subtitle.empty()) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(120);
        ImGui::DragFloat(tr("Shown for"), &e.subtitleSeconds, 0.05f, 0.2f, 30.0f, "%.1f s");
    }

    if (!m_messages.empty()) {
        ImGui::SeparatorText(tr("While reading sounds.events"));
        for (const ParseMessage& m : m_messages) ImGui::TextDisabled(tr("line %u: %s%s"), m.line, m.error ? tr("error: ") : "", m.text.c_str());
    }
    ImGui::End();
}

namespace {

// Name field that only takes valid, unused names (renaming happens when editing ends)
bool nameField(const char* label, std::string& name, char* buf, size_t size, int& editing, int index, const std::function<bool(const std::string&)>& taken,
               std::string& status) {
    if (editing != index) {
        std::snprintf(buf, size, "%s", name.c_str());
        editing = index;
    }
    ImGui::SetNextItemWidth(220);
    if (!(ImGui::InputText(label, buf, size, ImGuiInputTextFlags_EnterReturnsTrue) || ImGui::IsItemDeactivatedAfterEdit())) return false;
    const std::string next = buf;
    if (next == name) return false;
    if (validName(next) && !taken(next)) {
        name = next;
        return true;
    }
    status = taken(next) ? trf("Name already used: %s", next.c_str()) : std::string(tr("Names use letters, digits, _ - . only"));
    std::snprintf(buf, size, "%s", name.c_str());
    return false;
}

bool busCombo(const char* label, std::string& bus, const EventFile& file, bool allowNone) {
    bool changed = false;
    if (ImGui::BeginCombo(label, bus.empty() ? tr("(none)") : bus.c_str())) {
        if (allowNone && ImGui::Selectable(tr("(none)"), bus.empty())) {
            bus.clear();
            changed = true;
        }
        for (const char* b : kStandardBuses)
            if (std::string(b) != "master" && ImGui::Selectable(b, bus == b)) {
                bus = b;
                changed = true;
            }
        for (const BusDef& b : file.buses)
            if (!std::any_of(std::begin(kStandardBuses), std::end(kStandardBuses), [&](const char* s) { return b.name == s; }) &&
                ImGui::Selectable(b.name.c_str(), bus == b.name)) {
                bus = b.name;
                changed = true;
            }
        ImGui::EndCombo();
    }
    return changed;
}

// Windows added after a layout was saved (imgui.ini) open as tabs next to the Event window
void dockWithEvent() {
    if (const ImGuiWindow* event = ImGui::FindWindowByName("Event"); event && event->DockId) ImGui::SetNextWindowDockID(event->DockId, ImGuiCond_FirstUseEver);
}

} // namespace

// ---------------------------------------------------------------- Buses

void App::drawBuses() {
    ImGui::Begin(panelTitle(kIconLayers, "Buses").c_str());
    std::vector<BusInfo> live;
    if (m_audio) m_mixer.buses(live);
    auto peakOf = [&](const std::string& name) {
        for (const BusInfo& b : live)
            if (b.name == name) return b.peak;
        return 0.0f;
    };
    int removeAt = -1;
    auto infoOf = [&](const std::string& name) {
        for (const BusInfo& b : live)
            if (b.name == name) return b;
        return BusInfo{};
    };
    if (ImGui::BeginTable("##buses", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn(tr("Bus"));
        ImGui::TableSetupColumn(tr("Parent"));
        ImGui::TableSetupColumn(tr("Mix volume"));
        ImGui::TableSetupColumn(tr("Level"));
        ImGui::TableSetupColumn(tr("Effects"));
        ImGui::TableHeadersRow();
        auto meter = [&](const std::string& name) {
            const float p = peakOf(name);
            char overlay[16];
            std::snprintf(overlay, sizeof(overlay), "%.2f", p);
            ImGui::ProgressBar(std::min(p, 1.0f), ImVec2(-FLT_MIN, 0), overlay);
        };
        for (const char* b : kStandardBuses) {
            if (std::any_of(m_file.buses.begin(), m_file.buses.end(), [&](const BusDef& d) { return d.name == b; })) continue;
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(b);
            ImGui::TableNextColumn();
            ImGui::TextDisabled(std::string(b) == "master" ? "-" : "master");
            ImGui::TableNextColumn();
            ImGui::TextDisabled("1.00");
            ImGui::TableNextColumn();
            meter(b);
            ImGui::TableNextColumn();
            ImGui::TextDisabled("-");
        }
        for (size_t i = 0; i < m_file.buses.size(); ++i) {
            BusDef& b = m_file.buses[i];
            ImGui::PushID(static_cast<int>(i));
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            if (iconButton(kIconRemove, tr("Remove"), true)) removeAt = static_cast<int>(i);
            ImGui::SameLine();
            ImGui::TextUnformatted(b.name.c_str());
            ImGui::TableNextColumn();
            if (b.name != "master") {
                ImGui::SetNextItemWidth(-FLT_MIN);
                if (ImGui::BeginCombo("##parent", b.parent.c_str())) {
                    for (const char* s : kStandardBuses)
                        if (b.name != s && ImGui::Selectable(s, b.parent == s)) b.parent = s;
                    for (const BusDef& o : m_file.buses)
                        if (o.name != b.name && !std::any_of(std::begin(kStandardBuses), std::end(kStandardBuses), [&](const char* s) { return o.name == s; }) &&
                            ImGui::Selectable(o.name.c_str(), b.parent == o.name))
                            b.parent = o.name;
                    ImGui::EndCombo();
                }
            } else {
                ImGui::TextDisabled("-");
            }
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::SliderFloat("##mix", &b.volume, 0.0f, 1.5f, "%.2f");
            ImGui::TableNextColumn();
            meter(b.name);
            ImGui::TableNextColumn();
            std::string summary = b.reverb ? "reverb" : "";
            if (!b.duckBy.empty()) summary += (summary.empty() ? "" : ", ") + std::string("ducked by ") + b.duckBy;
            const BusInfo now = infoOf(b.name);
            if (now.duck < 0.999f) summary += " (now " + std::to_string(static_cast<int>(now.duck * 100.0f + 0.5f)) + "%)";
            if (ImGui::SmallButton(summary.empty() ? tr("none...") : (summary + "...").c_str())) ImGui::OpenPopup("fx");
            if (ImGui::BeginPopup("fx")) {
                checkbox(tr("Reverb (amount from reverb zones)"), &b.reverb);
                ImGui::SetNextItemWidth(140);
                busCombo(tr("Ducked by"), b.duckBy, m_file, true);
                if (b.duckBy == b.name) b.duckBy.clear();
                if (!b.duckBy.empty()) {
                    ImGui::SetNextItemWidth(160);
                    ImGui::SliderFloat(tr("Down to"), &b.duckVolume, 0.0f, 1.0f, "%.2f");
                    ImGui::SetNextItemWidth(160);
                    ImGui::DragFloat(tr("Attack"), &b.duckAttack, 0.005f, 0.0f, 5.0f, "%.2f s");
                    ImGui::SetNextItemWidth(160);
                    ImGui::DragFloat(tr("Release"), &b.duckRelease, 0.005f, 0.0f, 10.0f, "%.2f s");
                }
                ImGui::EndPopup();
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (removeAt >= 0) m_file.buses.erase(m_file.buses.begin() + removeAt);
    static char name[48] = {};
    ImGui::SetNextItemWidth(160);
    ImGui::InputTextWithHint("##newbus", tr("bus name"), name, sizeof(name));
    ImGui::SameLine();
    const bool exists = std::any_of(m_file.buses.begin(), m_file.buses.end(), [&](const BusDef& b) { return b.name == name; });
    ImGui::BeginDisabled(!validName(name) || exists);
    if (iconButton(kIconAdd, tr("Add bus"))) {
        BusDef b;
        b.name = name;
        m_file.buses.push_back(b);
        name[0] = 0;
    }
    ImGui::EndDisabled();
    ImGui::TextDisabled(tr("Standard buses can be added by name to change their mix volume or parent."));
    ImGui::End();
}

// ---------------------------------------------------------------- Preview

void App::drawPreview() {
    ImGui::Begin(panelTitle(kIconPreview, "Preview").c_str());
    if (!m_audio) {
        ImGui::TextDisabled(tr("No audio output device."));
        ImGui::End();
        return;
    }
    const EventDef* e = (m_selected >= 0 && m_selected < static_cast<int>(m_file.events.size())) ? &m_file.events[static_cast<size_t>(m_selected)] : nullptr;
    if (e && m_previewFor != m_selected) {
        m_previewFor = m_selected;
        m_param = e->paramMin;
    }
    ImGui::BeginDisabled(!e);
    if (iconButton(kIconPlay, tr("Play  (or double-click an event)")) && e) {
        m_preview = m_mixer.play(e->name, {0, 0, m_distance}, {{e->param.c_str(), m_param}});
        m_mixer.setOcclusion(m_preview, m_occlusion);
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (iconButton(kIconStop, tr("Stop"))) m_mixer.stop(m_preview, 0.1f);
    ImGui::SameLine();
    if (ImGui::Button(tr("Stop all"))) m_mixer.stopAll(); // no glyph of its own: words (engine docs/TOOL_DESIGN.md section 1)
    if (e && !e->param.empty()) {
        const float lo = std::min(e->paramMin, e->paramMax), hi = std::max(e->paramMin, e->paramMax);
        ImGui::SetNextItemWidth(-120);
        if (ImGui::SliderFloat(e->param.c_str(), &m_param, lo, hi)) m_mixer.setParam(m_preview, e->param, m_param);
    }
    ImGui::SetNextItemWidth(-120);
    if (ImGui::SliderFloat(tr("Distance"), &m_distance, 0.0f, 100.0f, "%.1f m")) m_mixer.setPosition(m_preview, {0, 0, m_distance});
    ImGui::SetNextItemWidth(-120);
    if (ImGui::SliderFloat(tr("Occlusion"), &m_occlusion, 0.0f, 1.0f, "%.2f")) m_mixer.setOcclusion(m_preview, m_occlusion);

    const Stats& st = m_mixer.stats();
    ImGui::SeparatorText(tr("Voices"));
    ImGui::TextDisabled(tr("%u mixed, %u virtual, %.3f ms"), st.realVoices, st.virtualVoices, st.updateMs);
    std::vector<VoiceInfo> voices;
    m_mixer.voices(voices);
    for (const VoiceInfo& v : voices)
        ImGui::Text("%s%s  %.2f  %.1f m", v.event.c_str(), v.virtualVoice ? tr(" (virtual)") : v.loop ? tr(" (loop)") : "", v.audibility, v.distance);
    ImGui::SeparatorText(tr("Culled (newest first)"));
    const auto& culls = m_mixer.culls();
    int shown = 0;
    for (auto it = culls.rbegin(); it != culls.rend() && shown < 12; ++it, ++shown) ImGui::TextDisabled("%s  %s", it->event.c_str(), it->reason);
    ImGui::End();
}

// ---------------------------------------------------------------- Music

void App::drawMusic() {
    dockWithEvent();
    ImGui::Begin(panelTitle(kIconMusic, "Music").c_str());
    if (iconButton(kIconAdd, tr("New music track"))) {
        MusicDef d;
        int n = 1;
        do d.name = "track" + std::to_string(n++);
        while (std::any_of(m_file.music.begin(), m_file.music.end(), [&](const MusicDef& o) { return o.name == d.name; }));
        m_file.music.push_back(d);
        m_musicSelected = static_cast<int>(m_file.music.size()) - 1;
    }
    ImGui::SameLine();
    const bool has = m_musicSelected >= 0 && m_musicSelected < static_cast<int>(m_file.music.size());
    ImGui::BeginDisabled(!has);
    if (iconButton(kIconRemove, tr("Delete music track"))) {
        m_file.music.erase(m_file.music.begin() + m_musicSelected);
        m_musicSelected = std::min(m_musicSelected, static_cast<int>(m_file.music.size()) - 1);
    }
    ImGui::EndDisabled();
    // Track list on the left, the selected track on the right
    ImGui::BeginChild("##tracks", ImVec2(160, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX);
    for (int i = 0; i < static_cast<int>(m_file.music.size()); ++i)
        if (ImGui::Selectable(m_file.music[static_cast<size_t>(i)].name.c_str(), i == m_musicSelected)) m_musicSelected = i;
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("##track");
    if (m_musicSelected < 0 || m_musicSelected >= static_cast<int>(m_file.music.size())) {
        ImGui::TextDisabled(tr("Add a track. Game code: ctx.audio.playMusic(\"name\")."));
        ImGui::EndChild();
        ImGui::End();
        return;
    }
    MusicDef& d = m_file.music[static_cast<size_t>(m_musicSelected)];
    if (nameField((std::string(tr("Name")) + "##music").c_str(), d.name, m_musicRename, sizeof(m_musicRename), m_musicRenaming, m_musicSelected,
                  [&](const std::string& n) { return std::any_of(m_file.music.begin(), m_file.music.end(), [&](const MusicDef& o) { return &o != &d && o.name == n; }); },
                  m_status))
        m_statusTime = m_time;
    ImGui::SameLine();
    ImGui::TextDisabled(tr("game code: playMusic(\"%s\")"), d.name.c_str());

    ImGui::SeparatorText(tr("Stems (same length, played together)"));
    int removeAt = -1;
    for (size_t i = 0; i < d.stems.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        if (iconButton(kIconRemove, tr("Remove"), true)) removeAt = static_cast<int>(i);
        ImGui::SameLine();
        const bool missing = resolveClip(d.stems[i].name).empty();
        if (missing) ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.45f, 1.0f), tr("%zu  %s (missing)"), i, d.stems[i].name.c_str());
        else ImGui::Text(tr("%zu  %s%s"), i, d.stems[i].name.c_str(), i == 0 ? tr("  (always on)") : "");
        ImGui::PopID();
    }
    if (removeAt >= 0) d.stems.erase(d.stems.begin() + removeAt);
    ImGui::SetNextItemWidth(320);
    if (ImGui::BeginCombo("##addstem", tr("+ Add stem from assets/"), ImGuiComboFlags_HeightLarge)) {
        static char stemFilter[64] = {};
        ImGui::InputTextWithHint("##sf", tr("Filter"), stemFilter, sizeof(stemFilter));
        for (const std::string& f : m_clipFiles) {
            if (stemFilter[0] && f.find(stemFilter) == std::string::npos) continue;
            if (ImGui::Selectable(f.c_str())) d.stems.push_back({f, {}});
        }
        ImGui::EndCombo();
    }

    ImGui::SeparatorText(tr("Timing"));
    ImGui::SetNextItemWidth(120);
    ImGui::DragFloat(tr("BPM"), &d.bpm, 0.5f, 20.0f, 400.0f, "%.1f");
    d.bpm = std::max(d.bpm, 1.0f);
    ImGui::SameLine();
    int beats = static_cast<int>(d.beatsPerBar);
    ImGui::SetNextItemWidth(100);
    if (ImGui::SliderInt(tr("Beats per bar"), &beats, 1, 12)) d.beatsPerBar = static_cast<uint32_t>(beats);
    ImGui::SetNextItemWidth(120);
    ImGui::DragFloat(tr("Pickup before beat 1"), &d.offset, 0.005f, 0.0f, 10.0f, "%.3f s");
    int transition = static_cast<int>(d.transition);
    ImGui::SetNextItemWidth(120);
    const char* transitions[] = {tr("once (right away)"), tr("next beat"), tr("next bar")};
    if (ImGui::Combo(tr("Switch to this at"), &transition, transitions, 3)) d.transition = static_cast<Quantize>(transition);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(100);
    ImGui::DragFloat(tr("Crossfade"), &d.crossfade, 0.005f, 0.0f, 4.0f, "%.2f s");

    ImGui::SeparatorText(tr("Stem parameter"));
    char param[64];
    std::snprintf(param, sizeof(param), "%s", d.stemParam.c_str());
    ImGui::SetNextItemWidth(160);
    if (ImGui::InputTextWithHint((std::string(tr("Name")) + "##stemparam").c_str(), tr("e.g. tension"), param, sizeof(param))) d.stemParam = param;
    ImGui::SameLine();
    ImGui::SetNextItemWidth(160);
    ImGui::DragFloatRange2((std::string(tr("Range")) + "##stem").c_str(), &d.paramMin, &d.paramMax, 0.05f);
    ImGui::SetNextItemWidth(120);
    ImGui::DragFloat(tr("Stem fade"), &d.stemFade, 0.01f, 0.0f, 10.0f, "%.2f s");
    if (d.stemParam.empty()) ImGui::TextDisabled(tr("No parameter: every stem plays. With one, stems after the first come in one by one as it rises."));

    ImGui::SeparatorText(tr("Output"));
    ImGui::SetNextItemWidth(200);
    ImGui::SliderFloat((std::string(tr("Volume")) + "##music").c_str(), &d.volume, 0.0f, 2.0f, "%.2f");
    ImGui::SetNextItemWidth(160);
    busCombo((std::string(tr("Bus")) + "##music").c_str(), d.bus, m_file, false);
    ImGui::SameLine();
    checkbox((std::string(tr("Loop")) + "##music").c_str(), &d.loop);

    if (m_audio) {
        ImGui::SeparatorText(tr("Preview"));
        if (iconButton(kIconPlay, tr("Play track"))) m_mixer.playMusic(d.name);
        ImGui::SameLine();
        if (iconButton(kIconStop, tr("Stop music"))) m_mixer.stopMusic(0.5f);
        const MusicInfo info = m_mixer.music();
        ImGui::SameLine();
        if (info.track.empty()) ImGui::TextDisabled(tr("not playing"));
        else ImGui::Text(tr("%s  bar %u  beat %.1f%s%s"), info.track.c_str(), info.bar + 1, info.beat + 1.0f, info.next.empty() ? "" : "  -> ", info.next.c_str());
        if (!d.stemParam.empty()) {
            ImGui::SetNextItemWidth(-160);
            if (ImGui::SliderFloat(d.stemParam.c_str(), &m_stemParam, std::min(d.paramMin, d.paramMax), std::max(d.paramMin, d.paramMax)))
                m_mixer.setGlobalParam(d.stemParam, m_stemParam);
        }
        const EventDef* e = (m_selected >= 0 && m_selected < static_cast<int>(m_file.events.size())) ? &m_file.events[static_cast<size_t>(m_selected)] : nullptr;
        ImGui::BeginDisabled(!e);
        if (ImGui::Button(e ? (tr("Stinger: ") + e->name + tr(" on the next beat")).c_str() : tr("Stinger (select an event)"))) m_mixer.stinger(e->name, Quantize::Beat);
        ImGui::SameLine();
        if (ImGui::Button(tr("next bar")) && e) m_mixer.stinger(e->name, Quantize::Bar);
        ImGui::EndDisabled();
    }
    ImGui::EndChild();
    ImGui::End();
}

// ---------------------------------------------------------------- Mix: snapshots, reverb preview

void App::drawMix() {
    dockWithEvent();
    ImGui::Begin(panelTitle(kIconVolume, "Mix").c_str());
    ImGui::SeparatorText(tr("Snapshots"));
    ImGui::TextDisabled(tr("Bus values applied while a snapshot is on (game code: ctx.audio.setSnapshot(\"pause\", true))."));
    int removeAt = -1;
    std::vector<SnapshotInfo> live;
    if (m_audio) m_mixer.snapshots(live);
    for (size_t i = 0; i < m_file.snapshots.size(); ++i) {
        SnapshotDef& s = m_file.snapshots[i];
        ImGui::PushID(static_cast<int>(i));
        float weight = 0.0f;
        for (const SnapshotInfo& l : live)
            if (l.name == s.name) weight = l.weight;
        const bool open = ImGui::TreeNodeEx("##snap", ImGuiTreeNodeFlags_DefaultOpen, "%s", s.name.c_str());
        ImGui::SameLine(220);
        if (m_audio) {
            bool on = m_mixer.snapshot(s.name);
            if (checkbox(tr("preview"), &on)) m_mixer.setSnapshot(s.name, on);
            ImGui::SameLine();
            ImGui::ProgressBar(weight, ImVec2(80, 0), "");
            ImGui::SameLine();
        }
        if (iconButton(kIconRemove, tr("Delete snapshot"), true)) removeAt = static_cast<int>(i);
        if (open) {
            const int index = static_cast<int>(i);
            if (nameField((std::string(tr("Name")) + "##snap").c_str(), s.name, m_snapRename, sizeof(m_snapRename), m_snapRenaming, index,
                          [&](const std::string& n) {
                              return std::any_of(m_file.snapshots.begin(), m_file.snapshots.end(), [&](const SnapshotDef& o) { return &o != &s && o.name == n; });
                          },
                          m_status))
                m_statusTime = m_time;
            ImGui::SetNextItemWidth(120);
            ImGui::DragFloat(tr("Fade"), &s.fade, 0.01f, 0.0f, 10.0f, "%.2f s");
            int removeBus = -1;
            for (size_t k = 0; k < s.buses.size(); ++k) {
                SnapshotBus& sb = s.buses[k];
                ImGui::PushID(static_cast<int>(k));
                if (iconButton(kIconRemove, tr("Remove bus value"), true)) removeBus = static_cast<int>(k);
                ImGui::SameLine();
                ImGui::SetNextItemWidth(110);
                busCombo("##bus", sb.bus, m_file, false);
                ImGui::SameLine();
                ImGui::SetNextItemWidth(140);
                ImGui::SliderFloat(tr("volume"), &sb.volume, 0.0f, 1.5f, "%.2f");
                ImGui::SameLine();
                ImGui::SetNextItemWidth(160);
                ImGui::SliderFloat(tr("lowpass"), &sb.lowpass, 0.0f, 20000.0f, sb.lowpass > 0.0f ? tr("%.0f Hz") : tr("off"), ImGuiSliderFlags_Logarithmic);
                ImGui::PopID();
            }
            if (removeBus >= 0) s.buses.erase(s.buses.begin() + removeBus);
            if (iconButton(kIconAdd, tr("Add bus value"), true)) s.buses.push_back({"music", 1.0f, 0.0f});
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
    if (removeAt >= 0) m_file.snapshots.erase(m_file.snapshots.begin() + removeAt);
    if (iconButton(kIconAdd, tr("New snapshot"))) {
        SnapshotDef s;
        int n = 1;
        do s.name = "snapshot" + std::to_string(n++);
        while (std::any_of(m_file.snapshots.begin(), m_file.snapshots.end(), [&](const SnapshotDef& o) { return o.name == s.name; }));
        m_file.snapshots.push_back(s);
    }

    if (m_audio) {
        ImGui::SeparatorText(tr("Reverb preview"));
        ImGui::TextDisabled(tr("Reverb values come from reverb zones in the scene. Try them here on buses with reverb on (Buses window)."));
        bool changed = false;
        ImGui::SetNextItemWidth(200);
        changed |= ImGui::SliderFloat(tr("Room size"), &m_reverb.roomSize, 0.0f, 1.0f, "%.2f");
        ImGui::SetNextItemWidth(200);
        changed |= ImGui::SliderFloat(tr("Damping"), &m_reverb.damping, 0.0f, 1.0f, "%.2f");
        ImGui::SetNextItemWidth(200);
        changed |= ImGui::SliderFloat(tr("Wet"), &m_reverb.wet, 0.0f, 1.0f, "%.2f");
        ImGui::SetNextItemWidth(200);
        changed |= ImGui::SliderFloat(tr("Width"), &m_reverb.width, 0.0f, 1.0f, "%.2f");
        if (changed) m_mixer.setReverb(m_reverb, 0.05f);
    }
    ImGui::End();
}

// ---------------------------------------------------------------- Live connection

void App::refreshLive(bool force) {
    if (m_project.empty()) return;
    if (force || m_time >= m_liveCheck) {
        m_liveCheck = m_time + 1.0;
        std::vector<Instance> found = findInstances(m_project);
        std::vector<bool> on(found.size(), true);
        for (size_t i = 0; i < found.size(); ++i)
            for (size_t j = 0; j < m_live.size(); ++j)
                if (m_live[j].pid == found[i].pid && j < m_liveOn.size()) on[i] = m_liveOn[j];
        m_live = std::move(found);
        m_liveOn = std::move(on);
    }
    if (m_time < m_stateCheck) return;
    m_stateCheck = m_time + 0.5;
    m_liveState.clear();
    for (size_t i = 0; i < m_live.size(); ++i) {
        if (!m_liveOn[i]) continue;
        const HttpResult r = request(m_live[i], "GET", "audio/state", {}, 200);
        if (r.status == 200) {
            m_liveState = r.body;
            m_liveJson = {};
            if (!Json::parse(m_liveState, m_liveJson)) m_liveState.clear();
        }
        break; // state is watched only for the first selected one
    }
}

void App::playInGame(const EventDef& e) {
    std::string body = "event=" + urlEncode(e.name);
    if (!e.param.empty()) body += "&p." + urlEncode(e.param) + "=" + std::to_string(m_param);
    int sent = 0;
    for (size_t i = 0; i < m_live.size(); ++i) {
        if (!m_liveOn[i]) continue;
        if (request(m_live[i], "POST", "audio/play", body).status == 200) ++sent;
    }
    m_liveError = sent ? std::string() : std::string(tr("No running game answered (is it a development build of this project?)"));
}

void App::reloadInGames() {
    for (size_t i = 0; i < m_live.size(); ++i)
        if (m_liveOn[i]) request(m_live[i], "POST", "audio/reload");
}

void App::drawLive() {
    ImGui::Begin(panelTitle(kIconConnect, "Live").c_str());
    if (m_live.empty()) {
        ImGui::TextDisabled(tr("No running game or editor for this project."));
        ImGui::TextWrapped(tr("Run a development build of the game (or the MealKit editor) for this project: it appears here. Saving updates it at once."));
        ImGui::End();
        return;
    }
    for (size_t i = 0; i < m_live.size(); ++i) {
        const Instance& in = m_live[i];
        bool on = m_liveOn[i];
        const std::string label = trf("%s (pid %u, engine %s)", in.kind.c_str(), static_cast<unsigned>(in.pid), in.engine.c_str()) + "##live" + std::to_string(i);
        if (checkbox(label.c_str(), &on)) m_liveOn[i] = on;
        if (in.protocol != kLiveProtocol) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.45f, 1.0f), tr("protocol %u (tool %u)"), in.protocol, kLiveProtocol);
        }
    }
    const EventDef* e = (m_selected >= 0 && m_selected < static_cast<int>(m_file.events.size())) ? &m_file.events[static_cast<size_t>(m_selected)] : nullptr;
    const bool dirty = writeEvents(m_file) != m_savedText;
    ImGui::BeginDisabled(!e);
    if (iconButton(kIconLaunch, tr("Play in game")) && e) {
        if (dirty) save(); // the game hears the saved file
        playInGame(*e);
    }
    ImGui::EndDisabled();
    if (dirty) {
        ImGui::SameLine();
        ImGui::TextDisabled(tr("(saves first)"));
    }
    if (!m_liveError.empty()) ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.45f, 1.0f), "%s", m_liveError.c_str());

    if (!m_liveState.empty()) {
        const Json& j = m_liveJson;
        ImGui::SeparatorText(tr("Game audio"));
        ImGui::TextDisabled(tr("%d event(s), %d mixed, %d virtual"), static_cast<int>(j["events"].num()), static_cast<int>(j["stats"]["realVoices"].num()),
                            static_cast<int>(j["stats"]["virtualVoices"].num()));
        for (const Json& v : j["voices"].array)
            ImGui::Text("%s%s  %.2f  %.1f m", v["event"].str().c_str(), v["virtual"].flag() ? tr(" (virtual)") : "", v["audibility"].num(), v["distance"].num());
        ImGui::SeparatorText(tr("Game buses"));
        for (const Json& b : j["buses"].array) {
            char overlay[48];
            std::snprintf(overlay, sizeof(overlay), "%s %.2f", b["name"].str().c_str(), b["peak"].num());
            ImGui::ProgressBar(static_cast<float>(std::min(b["peak"].num(), 1.0)), ImVec2(-FLT_MIN, 0), overlay);
        }
    }
    ImGui::End();
}

void App::drawQuitDialog() {
    if (!m_quitAsked) return;
    const bool dirty = !m_project.empty() && writeEvents(m_file) != m_savedText;
    if (!dirty) {
        m_quitConfirmed = true;
        return;
    }
    ImGui::OpenPopup("###unsaved");
    if (ImGui::BeginPopupModal((std::string(tr("Unsaved changes")) + "###unsaved").c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted(tr("Save the changes to assets/sounds.events?"));
        if (ImGui::Button(tr("Save"))) {
            if (save()) m_quitConfirmed = true;
        }
        ImGui::SameLine();
        if (ImGui::Button(tr("Don't save"))) m_quitConfirmed = true;
        ImGui::SameLine();
        if (ImGui::Button(tr("Cancel"))) {
            m_quitAsked = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

} // namespace mkaudio::tool
