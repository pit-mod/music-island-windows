#include "Media.h"
#include "Audio.h"
#include "MediaPolicy.h"
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Media.Control.h>
#include <winrt/Windows.Storage.Streams.h>
#include <wrl/client.h>
#include <cmath>
#include <algorithm>
#include <chrono>

namespace island {
using namespace winrt;
using namespace winrt::Windows::Media::Control;
using namespace winrt::Windows::Storage::Streams;
using Microsoft::WRL::ComPtr;
double clockSeconds() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
template<class T> auto wait(T const& operation) {
    if(operation.wait_for(std::chrono::seconds(3))!=winrt::Windows::Foundation::AsyncStatus::Completed) {
        operation.Cancel();throw hresult_error(HRESULT_FROM_WIN32(ERROR_TIMEOUT));
    }
    return operation.GetResults();
}
static std::wstring trackId(GlobalSystemMediaTransportControlsSessionMediaProperties const& m) {
    std::wstring value=m.Title().c_str();value.push_back(0);value+=m.Artist();value.push_back(0);value+=m.AlbumTitle();value.push_back(0);value+=std::to_wstring(m.TrackNumber());return value;
}
MediaService::~MediaService() {{std::lock_guard lock(mutex);stopping=true;}wake.notify_all();if(worker.joinable())worker.join();}
std::shared_ptr<const Snapshot> MediaService::snapshot() {std::lock_guard lock(mutex);return latest;}
bool MediaService::command(Action action,const Snapshot& target,double position) {
    if(!target.available)return false;
    std::lock_guard lock(mutex);if(stopping||commands.size()>=8)return false;
    commands.push_back({action,target.session,target.track,position});wake.notify_all();return true;
}
void MediaService::run() {
    init_apartment(apartment_type::multi_threaded);
    GlobalSystemMediaTransportControlsSessionManager manager{nullptr};
    GlobalSystemMediaTransportControlsSession selected{nullptr};
    struct Entry {GlobalSystemMediaTransportControlsSession session{nullptr};std::wstring token;};
    std::vector<Entry> entries;
    AudioVisualizer audio;
    Snapshot state;
    double nextPoll=0,nextArt=0;unsigned long long token=0;
    while(true) {
        std::deque<Command> pending;
        {std::unique_lock lock(mutex);wake.wait_for(lock,std::chrono::milliseconds(25),[&]{return stopping||!commands.empty();});if(stopping)break;pending.swap(commands);}
        double now=clockSeconds();
        try {
            if(!manager)manager=wait(GlobalSystemMediaTransportControlsSessionManager::RequestAsync());
            for(auto& command:pending) {
                auto entry=std::find_if(entries.begin(),entries.end(),[&](auto& e){return e.token==command.session;});
                bool accepted=false;
                if(entry!=entries.end()) {
                    auto properties=wait(entry->session.TryGetMediaPropertiesAsync());
                    if(trackId(properties)==command.track) {
                        auto c=entry->session.GetPlaybackInfo().Controls();
                        switch(command.action) {
                        case Action::Play:if(c.IsPlayEnabled())accepted=wait(entry->session.TryPlayAsync());break;
                        case Action::Pause:if(c.IsPauseEnabled())accepted=wait(entry->session.TryPauseAsync());break;
                        case Action::Previous:if(c.IsPreviousEnabled())accepted=wait(entry->session.TrySkipPreviousAsync());break;
                        case Action::Next:if(c.IsNextEnabled())accepted=wait(entry->session.TrySkipNextAsync());break;
                        case Action::Seek:if(c.IsPlaybackPositionEnabled()&&std::isfinite(command.position)) {
                            auto timeline=entry->session.GetTimelineProperties();double start=std::chrono::duration<double>(timeline.StartTime()).count(),end=std::chrono::duration<double>(timeline.EndTime()).count();
                            double low=std::max(start,std::chrono::duration<double>(timeline.MinSeekTime()).count());double high=end;
                            if(timeline.MaxSeekTime()>timeline.MinSeekTime())high=std::min(high,std::chrono::duration<double>(timeline.MaxSeekTime()).count());
                            double value=std::clamp(start+command.position,low,std::max(low,high));accepted=wait(entry->session.TryChangePlaybackPositionAsync(static_cast<int64_t>(value*10000000)));
                        }break;
                        }
                    }
                }
                state.error=accepted?L"":L"The player did not accept that control";nextPoll=0;
            }
            if(now>=nextPoll) {
                nextPoll=now+.35;
                auto sessions=manager.GetSessions();
                std::erase_if(entries,[&](auto& e){return std::none_of(sessions.begin(),sessions.end(),[&](auto const& s){return s==e.session;});});
                for(auto const& s:sessions)if(std::none_of(entries.begin(),entries.end(),[&](auto& e){return e.session==s;}))entries.push_back({s,std::to_wstring(++token)});
                auto current=manager.GetCurrentSession();int best=-1;Entry* choice=nullptr;
                GlobalSystemMediaTransportControlsSessionMediaProperties metadata{nullptr};
                for(auto& e:entries) {
                    try {
                        auto source=e.session.SourceAppUserModelId();if(containsAny(folded(source.c_str()),{L"discord",L"tiktok",L"tik-tok"}))continue;
                        auto info=e.session.GetPlaybackInfo();auto status=info.PlaybackStatus();
                        if(status!=GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing&&status!=GlobalSystemMediaTransportControlsSessionPlaybackStatus::Paused)continue;
                        auto properties=wait(e.session.TryGetMediaPropertiesAsync());
                        auto type=properties.PlaybackType();if(!type)type=info.PlaybackType();
                        MediaCandidate candidate{source.c_str(),properties.Title().c_str(),properties.Artist().c_str(),properties.AlbumTitle().c_str(),properties.Subtitle().c_str()};
                        candidate.kind=type?static_cast<MediaKind>(type.Value()):MediaKind::Unknown;
                        candidate.playing=status==GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing;candidate.paused=status==GlobalSystemMediaTransportControlsSessionPlaybackStatus::Paused;
                        if(!isMusic(candidate))continue;
                        int score=candidate.playing?100:50;if(e.session==current)score+=20;if(e.session==selected)score+=5;
                        if(score>best){best=score;choice=&e;metadata=properties;}
                    }catch(hresult_error const&) { /* An inaccessible session cannot mask another music player. */ }
                }
                if(!choice) {state=Snapshot{};selected=nullptr;}
                else {
                    selected=choice->session;auto id=trackId(metadata);
                    bool replaced=state.session!=choice->token||state.track!=id;
                    if(replaced){state.artwork.reset();state.artHash=0;state.error.clear();nextArt=0;}
                    state.available=true;state.session=choice->token;state.track=id;state.source=selected.SourceAppUserModelId();state.title=metadata.Title();state.artist=metadata.Artist();
                    auto info=selected.GetPlaybackInfo();auto controls=info.Controls();auto timeline=selected.GetTimelineProperties();
                    state.playing=info.PlaybackStatus()==GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing;
                    state.play=controls.IsPlayEnabled();state.pause=controls.IsPauseEnabled();state.previous=controls.IsPreviousEnabled();state.next=controls.IsNextEnabled();
                    auto rate=info.PlaybackRate();state.rate=rate?rate.Value():1;if(!std::isfinite(state.rate)||state.rate<0)state.rate=1;
                    state.duration=std::max(0.0,std::chrono::duration<double>(timeline.EndTime()-timeline.StartTime()).count());
                    state.position=std::max(0.0,std::chrono::duration<double>(timeline.Position()-timeline.StartTime()).count());
                    double age=std::chrono::duration<double>(winrt::clock::now()-timeline.LastUpdatedTime()).count();
                    if(state.playing&&age>=0&&age<86400)state.position+=age*state.rate;
                    if(state.duration>0)state.position=std::min(state.position,state.duration);state.seek=controls.IsPlaybackPositionEnabled()&&state.duration>0;state.received=clockSeconds();
                    if(now>=nextArt&&metadata.Thumbnail()) {
                        nextArt=now+(state.artwork?10:1);
                        try {
                            auto stream=wait(metadata.Thumbnail().OpenReadAsync());
                            if(stream.Size()>0&&stream.Size()<=4*1024*1024) {
                                DataReader reader{stream};uint32_t size=static_cast<uint32_t>(stream.Size());wait(reader.LoadAsync(size));auto bytes=std::make_shared<std::vector<unsigned char>>(size);reader.ReadBytes(*bytes);
                                unsigned long long hash=1469598103934665603ULL;for(auto c:*bytes){hash^=c;hash*=1099511628211ULL;}
                                state.artHash=hash;state.artwork=bytes;
                            }
                        }catch(...) {nextArt=now+1;}
                    }
                }
            }

        }catch(hresult_error const&) {
            state.error=L"Waiting for Windows media sessions";state.peak=0;manager=nullptr;nextPoll=now+1;
            // Avoid retrying a failed WinRT request on every meter tick.
            std::unique_lock lock(mutex);wake.wait_for(lock,std::chrono::seconds(1),[&]{return stopping;});
        }catch(...) {state.error=L"Waiting for the media player";state.peak=0;}
        audio.select(state.source,state.available&&state.playing&&clockSeconds()-state.received<2);
        auto levels=audio.read();state.peaks=levels.peaks;state.audioGain=levels.outputGain;state.audioRawPeak=levels.rawPeak;state.audioSessionVolume=levels.sessionVolume;state.audioEndpointDb=levels.endpointDb;state.audioCapturedPeak=levels.capturedPeak;state.audioOutputMeasured=levels.outputMeasured;state.audioLoopbackError=levels.loopbackError;state.audioReceived=levels.received;state.peak=levels.peak;state.audioAvailable=levels.available;state.audioPid=levels.process;state.audioSamples=levels.samples;state.audioMatches=levels.matches;state.audioSource=levels.source;
        ++state.revision;
        {std::lock_guard lock(mutex);latest=std::make_shared<Snapshot>(state);}
        PostMessageW(window,MediaMessage,0,0);
    }
    uninit_apartment();
}
}
