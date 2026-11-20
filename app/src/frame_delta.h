// Delta-time technique adapted from Budokai-NX by Unknown (MIT) and
// DBZ-Budokai-3-HD-Collection by NovaPowers. See CONTRIBUTORS.md.
#pragma once
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>

namespace b3::timing {
// Guest units are 60 Hz ticks (the verified US normal step is 1.0),
// not seconds. Keep the conversion at this one boundary.
struct DeltaSample {
    float ticks=1;
    double seconds=1.0/60.0;
    bool applied=false, reset=false, clamped=false;
};
class FrameDelta {
    uint64_t previous=0;
    int previousPhase=-1;
    bool primed=false;
public:
    static constexpr uint32_t stepAddress=0x8238A60C;
    static constexpr uint32_t elapsedAddress=0x8238A610;
    static constexpr bool BattleScene(int phase) {return phase==9||phase==15;}
    // Dragon Universe map (phase 8; Budokai-NX moves the story character there). 7 = menu, 14 = character select.
    static constexpr bool WorldScene(int phase) {return phase==8;}
    static constexpr bool Scene(int phase,bool world) {
        return BattleScene(phase)||(world&&WorldScene(phase));
    }
    DeltaSample Sample(uint64_t nowNs,int phase,float originalStep,bool enabled,bool world=false) {
        const bool active=enabled&&Scene(phase,world)&&std::bit_cast<uint32_t>(originalStep)==0x3F800000;
        const bool restart=!primed||previousPhase!=phase||nowNs<=previous;
        const uint64_t elapsed=restart?0:nowNs-previous;
        previous=nowNs;previousPhase=phase;primed=active;
        DeltaSample out;out.ticks=originalStep;
        if(!active)return out; // Preserve zero/pause, PAL and unknown modes.
        out.applied=true;
        // Do not spend time accumulated during a load, suspend or scene change.
        if(restart||elapsed>250000000){out.reset=true;return out;}
        out.seconds=double(elapsed)*1e-9;
        out.clamped=out.seconds>0.100;
        out.ticks=float(std::min(out.seconds,0.100)*60.0);
        return out;
    }
};

// Only undo our own previous store. Guest changes always take priority.
struct GuestDeltaPatch {
    bool owned=false;
    uint32_t original=0, written=0;
    template<class Read,class Write> void Restore(Read read,Write write) {
        if(owned&&read(FrameDelta::stepAddress)==written)
            write(FrameDelta::stepAddress,original);
        owned=false;
    }
    template<class Read,class Write> void Apply(const DeltaSample& sample,Read read,Write write) {
        if(!sample.applied)return;
        original=read(FrameDelta::stepAddress);
        written=std::bit_cast<uint32_t>(sample.ticks);
        // The original function has already consumed/reset the vblank count.
        // Replace elapsed ticks; multiplying by that count again doubles dt.
        write(FrameDelta::stepAddress,written);
        write(FrameDelta::elapsedAddress,written);
        owned=true;
    }
};
// Battle has a separate persistent speed/hit-stop scale. Temporarily scale
// its input, let the ORIGINAL update derive the effective animation/physics
// step, then restore only if guest scripts did not replace it themselves.
struct BattleDeltaPatch {
    bool owned=false;
    uint32_t address=0, original=0, written=0;
    template<class Read,class Write> void Apply(uint32_t at,float ticks,Read read,Write write) {
        owned=false;address=at;original=read(at);
        const float speed=std::bit_cast<float>(original);
        if(!std::isfinite(speed)||speed<0||speed>8||!std::isfinite(ticks)||ticks<=0||ticks>6||ticks==1||speed==0)return;
        written=std::bit_cast<uint32_t>(speed*ticks);
        write(address,written);owned=true;
    }
    template<class Read,class Write> void Restore(Read read,Write write) {
        if(owned&&read(address)==written)write(address,original);
        owned=false;
    }
};

// Own only effective time outputs, never the game's persistent speed controls.
// Remember the exact ORIGINAL output at the write site, avoiding divide/rebase
// roundoff and the previous-frame delta leaking into this frame's update.
struct EffectiveDeltaCell {
    uint32_t address=0, original=0, written=0;
    bool owned=false;
    template<class Read> uint32_t OriginalAt(uint32_t at,Read read)const {
        const uint32_t value=read(at);
        return owned&&address==at&&value==written?original:value;
    }
    template<class Write> void Publish(uint32_t at,uint32_t originalBits,float ticks,Write write) {
        address=at;original=originalBits;written=originalBits;owned=false;
        const float value=std::bit_cast<float>(originalBits);
        if(!std::isfinite(value)||value<0||value>8||!std::isfinite(ticks)||ticks<=0||ticks>6)return;
        if(ticks!=1&&value!=0)written=std::bit_cast<uint32_t>(value*ticks);
        write(at,written);owned=true;
    }
    template<class Read,class Write> void Begin(uint32_t at,float ticks,Read read,Write write) {
        Publish(at,OriginalAt(at,read),ticks,write);
    }
};
struct SameFrameBattleDelta {
    static constexpr uint32_t effectsTicksAddress=0x824392B8;
    static constexpr uint32_t effectsSecondsAddress=0x824392BC;
    EffectiveDeltaCell effective,sequence,effectsTicks,effectsSeconds;
    // The original battle function publishes these at different points. Both
    // must already refer to this update before its state callback runs.
    template<class Read,class Write> void Begin(uint32_t manager,float ticks,Read read,Write write) {
        effective.Begin(manager+35120,ticks,read,write);
        sequence.Begin(0x82395924,ticks,read,write);
        // Learn the original values at their actual store sites first. The
        // first callback may still contain a clock published by another phase.
        if(effectsTicks.owned)effectsTicks.Begin(effectsTicksAddress,ticks,read,write);
        if(effectsSeconds.owned)effectsSeconds.Begin(effectsSecondsAddress,ticks,read,write);
    }
};
}
