"""Freeze native regression cases with the adopted Python layer as oracle."""
import copy
import gzip
import json
from pathlib import Path
import sys
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
import nsmb_mvl_rule_match_v2 as rule

HERE = ROOT/'tests/fixtures'
def load(name):
    return json.loads(gzip.decompress((HERE/(name+'.json.gz')).read_bytes()))

cases = []
def add(name, layer, cls, parent, decision, player, frame, held, previous, trace, state, plan_name):
    for variation in ('recorded', 'missing_clock', 'damage', 'near_opponent', 'jump_input'):
        d = copy.deepcopy(decision)
        h = held
        if variation == 'missing_clock': d.pop('time', None)
        if variation == 'damage': d['runtimePlayers'][player]['damageStateRaw'] = 1
        if variation == 'near_opponent': d['observation']['players'][player^1]['pos'] = dict(d['observation']['players'][player]['pos'])
        if variation == 'jump_input': h |= 2
        controller = getattr(rule, cls)(player, 60)
        controller.trace = copy.deepcopy(trace)
        for k,v in state.items(): setattr(controller, k, v)
        with patch.object(getattr(rule, parent), 'act', return_value=h):
            result = controller.act(d, frame, previous)
        expected = dict(held=result, trace=controller.trace, plan=getattr(controller, plan_name) if plan_name else None)
        cases.append(dict(name=f'{name}/{variation}', layer=layer, decision=d, player=player, frame=frame,
                          held=h, previous=previous, trace=trace, state=state, expected=expected))

f=load('fire-skid')
add('fire-skid',16,'_PredictiveRejumpBase','_SkidJumpBase',f['initial']['decision'],1,f['plan']['start'],2064,2064,{}, {}, 'skid_jump_plan')
f=load('npc-landing')
add('npc-landing',26,'_WaitWallBase','_NpcLandingBase',f['row']['decision'],0,3000,2064,2064,{}, {}, 'npc_landing_plan')
f=load('wait-wall')
add('wait-wall',27,'_FallingGoombaBase','_WaitWallBase',f['initial']['decision'],1,5904,0,2064,{'target':'wait'}, {}, 'wait_wall_plan')
f=load('falling-goomba')
add('falling-goomba',28,'_WallShortBase','_FallingGoombaBase',f['initial']['decision'],0,f['plan']['start'],2064,2064,{}, {}, 'falling_goomba_plan')
f=load('wall-short')
add('wall-short',29,'_ShotRecoveryBase','_WallShortBase',f['initial']['decision'],0,1284,2082,2082,{'target':'carrier'}, {'jump_until':1296}, None)
f=load('pit-return')
add('shot-recovery',30,'_SpacingBoxBase','_ShotRecoveryBase',f['initial']['decision'],0,6552,2080,2080,{'target':'carrier','dx':-450}, {'last_pit_recovery':6540}, 'shot_recovery_plan')
f=load('recovery-reversal')['row']
add('recovery-finish',25,'_NpcLandingBase','_RecoveryFinishBase',f['decision'],1,4593,f['held'],f['previousHeld'],{'target':'protect_lead'}, {'last_pit_recovery':4587}, 'recovery_finish_plan')
data=json.dumps(cases, separators=(',', ':')).encode()
(HERE/'rust-layer-parity.json.gz').write_bytes(gzip.compress(data, mtime=0))
print(json.dumps(dict(cases=len(cases), active=sum(c['expected']['plan'] is not None for c in cases))))
