#pragma once
#include <algorithm>
#include <cwctype>
#include <initializer_list>
#include <string>
#include <string_view>

namespace island {
enum class MediaKind {Unknown,Music,Video,Image};
struct MediaCandidate {
    std::wstring source,title,artist,album,subtitle;
    MediaKind kind=MediaKind::Unknown;
    bool playing=false,paused=false;
};
inline std::wstring folded(std::wstring value) {
    std::transform(value.begin(),value.end(),value.begin(),[](wchar_t c){return static_cast<wchar_t>(std::towlower(c));});return value;
}
inline bool containsAny(const std::wstring& value,std::initializer_list<std::wstring_view> names) {
    for(auto name:names)if(value.find(name)!=std::wstring::npos)return true;return false;
}
inline bool isMusic(const MediaCandidate& candidate) {
    if((!candidate.playing&&!candidate.paused)||candidate.title.find_first_not_of(L" \t\r\n")==std::wstring::npos)return false;
    auto source=folded(candidate.source);
    if(containsAny(source,{L"discord",L"tiktok",L"tik-tok"}))return false;
    if(candidate.kind==MediaKind::Video||candidate.kind==MediaKind::Image)return false;
    bool browser=containsAny(source,{L"chrome",L"msedge",L"firefox",L"brave",L"opera",L"vivaldi",L"browser",L"arc.exe"});
    auto service=folded(candidate.album+L"\n"+candidate.subtitle);
    if(browser&&containsAny(service+L"\n"+folded(candidate.artist)+L"\n"+folded(candidate.title),{L"tiktok",L"tik-tok",L"discord"}))return false;
    if(candidate.kind==MediaKind::Music)return true;
    // Untyped browser videos must not take over a paused or playing music app.
    // Accept them only when the session identifies a music provider.
    return containsAny(source,{L"spotify",L"applemusic",L"apple.music",L"itunes",L"musicbee",L"foobar",L"tidal",L"deezer",L"qobuz",L"amazonmusic",L"winamp",L"aimp",L"zunemusic"})||
        containsAny(service,{L"spotify",L"apple music",L"youtube music",L"music.youtube.com",L"soundcloud",L"bandcamp",L"tidal",L"deezer",L"qobuz",L"amazon music"});
}
}
