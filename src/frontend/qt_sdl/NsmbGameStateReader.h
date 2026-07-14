#ifndef NSMB_GAME_STATE_READER_H
#define NSMB_GAME_STATE_READER_H

#include "NsmbGameState.h"

namespace melonDS {
class NDS;
}

namespace NsmbNetplayPoC::GameStateReader {

void ReadCoreState(melonDS::NDS *nds, GameStateModel::GameStateSample &sample);
void ReadPlayerAndCameraGlobals(melonDS::NDS *nds,
                                GameStateModel::GameStateSample &sample);
void ReadMvlGlobals(melonDS::NDS *nds, GameStateModel::GameStateSample &sample);
void ReadProjectileGlobals(melonDS::NDS *nds,
                           GameStateModel::GameStateSample &sample);

} // namespace NsmbNetplayPoC::GameStateReader

#endif
