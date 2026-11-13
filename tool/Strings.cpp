#include "Strings.h"

#include <mkcontract/User.h>

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string_view>
#include <unordered_map>

namespace mkaudio::tool {
namespace {

struct Pair {
    const char* en;
    const char* ko;
};

// When new text is shown on screen, add its Korean here: mkaudio_strings_test fails on tr("...") text without a line.
// Korean follows engine docs/TOOL_DESIGN.md section 4: short, plain words, sentences in 해요체, buttons and menu items a verb noun
constexpr Pair kKorean[] = {
    {"Audio: %s", "오디오: %s"},
    {"No audio output device: preview is off", "오디오 출력 장치가 없어서 미리 듣기를 쓸 수 없어요"},
    {"Not a MealKit project (no assets folder): %s", "MealKit 프로젝트가 아니에요 (assets 폴더 없음): %s"},
    {"No assets/sounds.events yet: add events and save to create it", "assets/sounds.events가 아직 없어요. 이벤트를 추가하고 저장하면 만들어져요"},
    {"Loaded assets/sounds.events: %d event(s)", "assets/sounds.events를 불러왔어요: 이벤트 %d개"},
    {"Cannot write %s", "%s 파일을 쓸 수 없어요"},
    {"Saved assets/sounds.events", "assets/sounds.events를 저장했어요"},
    {"Imported %d event(s) from assets/sounds.table: save to write sounds.events", "assets/sounds.table에서 이벤트 %d개를 가져왔어요. 저장하면 sounds.events에 써요"},
    {"File", "파일"},
    {"Open Project Folder...", "프로젝트 폴더 열기..."},
    {"Save", "저장"},
    {"Reload from Disk", "디스크에서 다시 불러오기"},
    {"Import assets/sounds.table", "assets/sounds.table 가져오기"},
    {"Quit", "끝내기"},
    {"Edit", "편집"},
    {"Undo", "실행 취소"},
    {"Redo", "다시 실행"},
    {"Rescan Clips", "클립 다시 찾기"},
    {"Language", "언어"},
    {"Cannot save the language to %s", "%s에 언어를 저장할 수 없어요"},
    {"MkAudio", "MkAudio"},
    {"Sound events for a MealKit project (assets/sounds.events)", "MealKit 프로젝트의 사운드 이벤트 (assets/sounds.events)"},
    {"The MealKit editor opens this tool from Tools > MkAudio.", "MealKit 에디터의 도구 > MkAudio에서도 이 도구를 열 수 있어요."},
    {"Filter", "필터"},
    {"New event", "새 이벤트"},
    {"Duplicate event", "이벤트 복제"},
    {"Delete event", "이벤트 삭제"},
    {"sounds.events changed on disk", "디스크의 sounds.events가 바뀌었어요"},
    {"Reload (lose my changes)", "다시 불러오기 (내 변경 버리기)"},
    {"Select or add an event.", "이벤트를 고르거나 추가하세요."},
    {"While reading sounds.events", "sounds.events를 읽는 중에"},
    {"line %u: %s%s", "%u번째 줄: %s%s"},
    {"error: ", "오류:"},
    {"Name", "이름"},
    {"Name already used: %s", "이미 쓰는 이름이에요: %s"},
    {"Names use letters, digits, _ - . only", "이름에는 영문자, 숫자, _ - . 만 쓸 수 있어요"},
    {"game code: play(\"%s\")", "게임 코드: play(\"%s\")"},
    {"Clips", "클립"},
    {"random", "랜덤"},
    {"switch", "전환"},
    {"blend", "블렌드"},
    {"Container", "묶음 방식"},
    {"one clip at random, never the same twice", "클립 하나를 랜덤으로, 같은 것을 연달아 고르지 않아요"},
    {"the parameter picks clip 0, 1, 2...", "파라미터가 클립 0, 1, 2...를 골라요"},
    {"all clips crossfaded along the parameter", "파라미터에 따라 모든 클립을 섞어요"},
    {"Remove", "빼기"},
    {"Move up", "위로 옮기기"},
    {"%zu  %s (missing)", "%zu  %s (없음)"},
    {"%zu  %s", "%zu  %s"},
    {"+ Add clip from assets/", "+ assets/에서 클립 추가"},
    {"No .wav or .ogg files under assets/", "assets/ 아래에 .wav나 .ogg 파일이 없어요"},
    {"Volume and pitch", "볼륨과 피치"},
    {"Volume", "볼륨"},
    {"+/- random", "+/- 랜덤"},
    {"Pitch", "피치"},
    {"Parameter", "파라미터"},
    {"Range", "범위"},
    {"No parameter: the curves below are not used. Game code passes it as {{\"name\", value}}.", "파라미터가 없어서 아래 곡선은 쓰지 않아요. 게임 코드에서 {{\"name\", value}}로 넘겨요."},
    {"Volume curve (%.2f -> %.2f)", "볼륨 곡선 (%.2f -> %.2f)"},
    {"Pitch curve (%.2f -> %.2f)", "피치 곡선 (%.2f -> %.2f)"},
    {"Low-pass curve (%.0f -> %.0f Hz, 0 = off)", "로우패스 곡선 (%.0f -> %.0f Hz, 0 = 끔)"},
    {"Positioned in the world", "월드에 위치함"},
    {"Distance (full, silent)", "거리 (최대, 무음)"},
    {"Behind a wall: volume", "벽 뒤: 볼륨"},
    {"low-pass", "로우패스"},
    {"%.0f Hz", "%.0f Hz"},
    {"off", "끔"},
    {"The game checks a line from the listener to the sound; fully blocked uses these values.", "게임이 듣는 위치에서 소리까지 선을 확인해요. 완전히 막히면 이 값을 써요."},
    {"Playback", "재생"},
    {"Bus", "버스"},
    {"not defined (made under master)", "정의 안 됨 (master 아래에 만들어져요)"},
    {"Loop", "반복"},
    {"Stream from disk (music)", "디스크에서 스트리밍 (음악)"},
    {"Priority", "우선순위"},
    {"Max at once", "동시에 최대"},
    {"stop oldest", "가장 오래된 것 멈추기"},
    {"stop quietest", "가장 작은 것 멈추기"},
    {"don't play", "재생 안 함"},
    {"When full", "가득 찼을 때"},
    {"Cooldown", "재사용 대기"},
    {"Subtitle", "자막"},
    {"String key", "문자열 키"},
    {"e.g. sub.door", "예: sub.door"},
    {"Shown for", "표시 시간"},
    {"(none)", "(없음)"},
    {"Parent", "부모"},
    {"Mix volume", "믹스 볼륨"},
    {"Level", "레벨"},
    {"Effects", "효과"},
    {"none...", "없음..."},
    {"Reverb (amount from reverb zones)", "리버브 (양은 리버브 영역에서)"},
    {"Ducked by", "덕킹 기준"},
    {"Down to", "낮출 값"},
    {"Attack", "어택"},
    {"Release", "릴리스"},
    {"bus name", "버스 이름"},
    {"Add bus", "버스 추가"},
    {"Standard buses can be added by name to change their mix volume or parent.", "기본 버스도 이름으로 추가해서 믹스 볼륨이나 부모를 바꿀 수 있어요."},
    {"No audio output device.", "오디오 출력 장치가 없어요."},
    {"Play  (or double-click an event)", "재생  (또는 이벤트 더블클릭)"},
    {"Stop", "정지"},
    {"Stop all", "모두 정지"},
    {"Distance", "거리"},
    {"Occlusion", "가림"},
    {"Voices", "보이스"},
    {"%u mixed, %u virtual, %.3f ms", "믹스 %u개, 가상 %u개, %.3f ms"},
    {" (virtual)", " (가상)"},
    {" (loop)", " (반복)"},
    {"Culled (newest first)", "제외됨 (최근 것부터)"},
    {"New music track", "새 음악 트랙"},
    {"Delete music track", "음악 트랙 삭제"},
    {"Add a track. Game code: ctx.audio.playMusic(\"name\").", "트랙을 추가하세요. 게임 코드: ctx.audio.playMusic(\"name\")."},
    {"game code: playMusic(\"%s\")", "게임 코드: playMusic(\"%s\")"},
    {"Stems (same length, played together)", "스템 (길이가 같고 함께 재생)"},
    {"%zu  %s%s", "%zu  %s%s"},
    {"  (always on)", "  (항상 켜짐)"},
    {"+ Add stem from assets/", "+ assets/에서 스템 추가"},
    {"Timing", "타이밍"},
    {"BPM", "BPM"},
    {"Beats per bar", "마디당 박자"},
    {"Pickup before beat 1", "첫 박 앞 여린박"},
    {"once (right away)", "즉시"},
    {"next beat", "다음 박자"},
    {"next bar", "다음 마디"},
    {"Switch to this at", "전환 시점"},
    {"Crossfade", "크로스페이드"},
    {"Stem parameter", "스템 파라미터"},
    {"e.g. tension", "예: tension"},
    {"Stem fade", "스템 페이드"},
    {"No parameter: every stem plays. With one, stems after the first come in one by one as it rises.", "파라미터가 없으면 모든 스템을 재생해요. 있으면 값이 오를수록 두 번째 스템부터 하나씩 들어와요."},
    {"Output", "출력"},
    {"Preview", "미리보기"},
    {"Play track", "트랙 재생"},
    {"Stop music", "음악 정지"},
    {"not playing", "재생 안 함"},
    {"%s  bar %u  beat %.1f%s%s", "%s  %u마디  %.1f박%s%s"},
    {"Stinger: ", "스팅어:"},
    {" on the next beat", " (다음 박자에)"},
    {"Stinger (select an event)", "스팅어 (이벤트를 고르세요)"},
    {"Snapshots", "스냅샷"},
    {"Bus values applied while a snapshot is on (game code: ctx.audio.setSnapshot(\"pause\", true)).", "스냅샷이 켜져 있는 동안 적용할 버스 값이에요 (게임 코드: ctx.audio.setSnapshot(\"pause\", true))."},
    {"preview", "미리 듣기"},
    {"Delete snapshot", "스냅샷 삭제"},
    {"Fade", "페이드"},
    {"Remove bus value", "버스 값 빼기"},
    {"volume", "볼륨"},
    {"lowpass", "로우패스"},
    {"Add bus value", "버스 값 추가"},
    {"New snapshot", "새 스냅샷"},
    {"Reverb preview", "리버브 미리 듣기"},
    {"Reverb values come from reverb zones in the scene. Try them here on buses with reverb on (Buses window).", "리버브 값은 씬의 리버브 영역에서 와요. 리버브를 켠 버스(버스 창)에서 여기서 들어 볼 수 있어요."},
    {"Room size", "방 크기"},
    {"Damping", "감쇠"},
    {"Wet", "웻"},
    {"Width", "폭"},
    {"No running game answered (is it a development build of this project?)", "실행 중인 게임이 응답하지 않아요 (이 프로젝트의 개발 빌드인가요?)"},
    {"No running game or editor for this project.", "이 프로젝트를 실행 중인 게임이나 에디터가 없어요."},
    {"Run a development build of the game (or the MealKit editor) for this project: it appears here. Saving updates it at once.", "이 프로젝트의 개발 빌드 게임(또는 MealKit 에디터)을 실행하면 여기에 나와요. 저장하면 바로 반영돼요."},
    {"%s (pid %u, engine %s)", "%s (pid %u, 엔진 %s)"},
    {"protocol %u (tool %u)", "프로토콜 %u (도구 %u)"},
    {"Play in game", "게임에서 재생"},
    {"(saves first)", "(먼저 저장해요)"},
    {"Game audio", "게임 오디오"},
    {"%d event(s), %d mixed, %d virtual", "이벤트 %d개, 믹스 %d개, 가상 %d개"},
    {"Game buses", "게임 버스"},
    {"Unsaved changes", "저장하지 않은 변경"},
    {"Save the changes to assets/sounds.events?", "assets/sounds.events에 변경을 저장할까요?"},
    {"Don't save", "저장 안 함"},
    {"Cancel", "취소"},
    {"Events", "이벤트 목록"},
    {"Event", "이벤트"},
    {"Buses", "버스"},
    {"Music", "음악"},
    {"Mix", "믹스"},
    {"Live", "라이브"},
};

bool g_korean = true;

const std::unordered_map<std::string_view, const char*>& koreanMap() {
    static const std::unordered_map<std::string_view, const char*> map = [] {
        std::unordered_map<std::string_view, const char*> m;
        for (const Pair& p : kKorean) m.emplace(p.en, p.ko);
        return m;
    }();
    return map;
}

std::filesystem::file_time_type g_settingsTime{};
std::chrono::steady_clock::time_point g_nextCheck{};

std::filesystem::file_time_type settingsTime() {
    std::error_code ec;
    const std::string path = mkcontract::userPath(mkcontract::kEditorSettings);
    if (path.empty()) return {};
    const auto t = std::filesystem::last_write_time(std::filesystem::path(std::u8string(path.begin(), path.end())), ec);
    return ec ? std::filesystem::file_time_type{} : t;
}

} // namespace

void setLanguage(const std::string& code) { g_korean = code != "en"; }

const char* language() { return g_korean ? "ko" : "en"; }

const char* tr(const char* english) {
    if (!g_korean) return english;
    const auto& map = koreanMap();
    const auto it = map.find(english);
    return it == map.end() ? english : it->second;
}

std::string trf(const char* english, ...) {
    char buffer[2048];
    va_list args;
    va_start(args, english);
    std::vsnprintf(buffer, sizeof(buffer), tr(english), args);
    va_end(args);
    return buffer;
}

bool hasTranslation(const char* english) { return koreanMap().count(english) != 0; }

void loadLanguage() {
    const std::string code = mkcontract::editorSetting("language");
    setLanguage(code.empty() ? "ko" : code);
    g_settingsTime = settingsTime();
}

bool saveLanguage(const std::string& code) {
    if (!mkcontract::setEditorSetting("language", code)) return false;
    setLanguage(code);
    g_settingsTime = settingsTime();
    return true;
}

bool followLanguage() {
    const auto now = std::chrono::steady_clock::now();
    if (now < g_nextCheck) return false;
    g_nextCheck = now + std::chrono::seconds(1);
    const auto t = settingsTime();
    if (t == g_settingsTime) return false;
    const std::string before = language();
    loadLanguage();
    return before != language();
}

} // namespace mkaudio::tool
