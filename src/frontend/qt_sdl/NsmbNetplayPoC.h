/*
    Experimental NSMB Mario vs Luigi input-lockstep PoC.

    This is intentionally isolated from melonDS' regular LAN/local MP paths.
    It only overrides per-frame controller input when MELONDS_NSML_POC=1.
*/

#ifndef NSMBNETPLAYPOC_H
#define NSMBNETPLAYPOC_H

#include "types.h"

namespace melonDS
{
class NDS;
}

namespace NsmbNetplayPoC
{

struct InputState
{
    melonDS::u32 KeyMask = 0xFFF;
    bool Touching = false;
    melonDS::u16 TouchX = 0;
    melonDS::u16 TouchY = 0;
};

bool IsEnabled();
void InitFromEnvironment();
InputState BeforeRunFrame(int instanceID, melonDS::u32 frame, melonDS::NDS* nds, const InputState& polledInput);
void AfterRunFrame(int instanceID, melonDS::u32 frame, melonDS::NDS* nds);
bool ShouldQuitAfterFrame(int instanceID, melonDS::u32 frame);
void Shutdown();

}

#endif
