#pragma once

namespace island {
enum class FocusKind {Unknown,Browsing,Editing};
struct FocusMetadata {
    bool known=false,password=false,edit=false,textEdit=false;
    bool value=false,valueReadOnly=false,valueKnown=false;
    bool text=false,textReadOnly=false,textKnown=false;
};
inline FocusKind classifyFocus(const FocusMetadata& focus) {
    if(focus.password||focus.edit||focus.textEdit)return FocusKind::Editing;
    if(!focus.known)return FocusKind::Unknown;
    if(focus.value&&(!focus.valueKnown||!focus.valueReadOnly))return FocusKind::Editing;
    if(focus.text&&(!focus.textKnown||!focus.textReadOnly))return FocusKind::Editing;
    return FocusKind::Browsing;
}
// A focus change discards the preceding decision, not the last physical key.
struct FocusApproval {
    unsigned long long foreground=0,focus=0,epoch=0,checked=0;
    FocusKind kind=FocusKind::Unknown;
    bool allows(unsigned long long currentForeground,unsigned long long currentFocus,unsigned long long currentEpoch,unsigned long long now) const {
        return kind==FocusKind::Browsing&&foreground!=0&&foreground==currentForeground&&focus==currentFocus&&epoch==currentEpoch&&now>=checked&&now-checked<500;
    }
};
}
