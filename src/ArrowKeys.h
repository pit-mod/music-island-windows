#pragma once
#include <array>

namespace island {
// Values match Windows virtual keys; no platform APIs are used by this policy.
enum class ArrowAction {None,Previous,Next,Playback,Expand};
inline ArrowAction arrowAction(unsigned key) {
    switch(key){case 0x25:return ArrowAction::Previous;case 0x27:return ArrowAction::Next;case 0x26:return ArrowAction::Playback;case 0x28:return ArrowAction::Expand;default:return ArrowAction::None;}
}
struct ArrowResult {bool consume=false,trigger=false;};
class ArrowKeys {
    std::array<bool,4> held{},owned{};
public:
    ArrowResult event(unsigned key,bool down,bool allowed,bool injected=false) {
        if(injected||key<0x25||key>0x28)return {};
        auto index=key-0x25;
        if(!down){bool consumed=owned[index];held[index]=owned[index]=false;return {consumed,false};}
        if(held[index])return {owned[index],false};
        held[index]=true;owned[index]=allowed;return {allowed,allowed};
    }
};
}
