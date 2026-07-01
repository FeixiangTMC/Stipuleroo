#pragma once

#include "ll/api/event/ListenerBase.h"
#include "ll/api/mod/NativeMod.h"

namespace Stipuleroo {

class Entry {
public:
    static Entry& getInstance();

    Entry() : mSelf(*ll::mod::NativeMod::current()) {}

    [[nodiscard]] ll::mod::NativeMod& getSelf() const { return mSelf; }

    bool load();
    bool enable();
    bool disable();
    bool unload();

private:
    ll::mod::NativeMod&        mSelf;
    ll::event::ListenerPtr mCommandRegisterListener;
    ll::event::ListenerPtr mDieListener;
    ll::event::ListenerPtr mDisconnectListener;
};

} // namespace Stipuleroo
