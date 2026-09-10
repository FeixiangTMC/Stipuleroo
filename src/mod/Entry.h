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
    ll::event::ListenerPtr mJoinLevelListener; // ClientJoinLevelEvent: 状态复位
    ll::event::ListenerPtr mClientCmdListener; // ClientCommandRegisterEvent: (重新)注册命令
    ll::event::ListenerPtr mTickListener;      // ClientLevelTickEvent: 心跳诊断 + 注册兜底
    ll::event::ListenerPtr mServerCmdListener; // ServerCommandRegisterEvent: 诊断
    ll::event::ListenerPtr mDieListener;
    ll::event::ListenerPtr mExitLevelListener;
    ll::event::ListenerPtr mFreecamKeyListener;
    ll::event::ListenerPtr mAutoToolKeyListener;
    ll::event::ListenerPtr mFakeSneakKeyListener;
    ll::event::ListenerPtr mNightVisionKeyListener;
    ll::event::ListenerPtr mAutoBridgeKeyListener;
    ll::event::ListenerPtr mAutoMouseKeyListener; // 自动鼠标操作：4 个快捷键（/am）
};

} // namespace Stipuleroo
