import copy,json,sys,importlib.util,unittest
from pathlib import Path
from unittest.mock import patch
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import nsmb_mvl_rule_match_v2 as rule
import gzip
fixture=json.loads(gzip.decompress((Path(__file__).parent/'fixtures/pit-return.json.gz').read_bytes()))
rows={x['episodeFrame']:x for x in [fixture['initial'],*fixture['following']]}
d0=rows[6552]['decision']
class ShotRecoveryTest(unittest.TestCase):
 def choose(self,d,recent=6540,target='carrier'):
  c=rule.RoutedHumanRule(0,60);c.trace=dict(target=target,dx=-450);c.last_pit_recovery=recent
  with patch.object(rule._ShotRecoveryBase,'act',return_value=2080):held=c.act(d,6552,2080)
  return held,c.shot_recovery_plan
 def test_native_positions_until_landing(self):
  held,p=self.choose(d0);self.assertEqual(held,2064);self.assertEqual(p['landing_after'],25)
  for i,q in enumerate(p['points'][:31],1):
   me=rows[6552+i]['decision']['observation']['players'][0]
   self.assertEqual(rule.wrap(q['x'],me['pos']['x']/4096),0);self.assertEqual(q['depth'],-me['pos']['y']/4096)
 def test_live_or_unknown_enemy_is_not_ignored(self):
  for fn in (0x020E1538,0):
   d=copy.deepcopy(d0);next(e for e in d['observation']['entities'] if e.get('actorGuid')==132)['goombaBehaviorFunctionRaw']=fn
   self.assertIsNone(self.choose(d)[1])
 def test_close_opponent_and_unrelated_movement_do_not_trigger(self):
  d=copy.deepcopy(d0);d['observation']['players'][1]['pos']=dict(d['observation']['players'][0]['pos'])
  self.assertIsNone(self.choose(d)[1]);self.assertIsNone(self.choose(d0,recent=6500)[1]);self.assertIsNone(self.choose(d0,target='natural_star')[1])
 def test_missing_clock_or_wrong_stage_does_not_trigger(self):
  d=copy.deepcopy(d0);d.pop('time');self.assertIsNone(self.choose(d)[1])
  d=copy.deepcopy(d0);d['observation']['stage']['id']=1;self.assertIsNone(self.choose(d)[1])
 def test_unknown_hitbox_is_not_used_for_hazard_clearance(self):
  d=copy.deepcopy(d0);d['observation']['players'][0]['hitbox']['found']=False
  self.assertIsNone(self.choose(d)[1])
  d=copy.deepcopy(d0);d['observation']['players'][0]['hitbox']['fixedPointShift']=0
  self.assertIsNone(self.choose(d)[1])
 def test_continuation_releases_control_on_landing_damage_or_position_error(self):
  _,initial=self.choose(d0)
  for kind in ('landing','damage','position','opponent'):
   c=rule.RoutedHumanRule(0,60);c.shot_recovery_plan=copy.deepcopy(initial)
   c.trace=dict(target='carrier',dx=-450);c.last_pit_recovery=6540
   frame=6582 if kind=='landing' else 6558
   d=copy.deepcopy(rows[frame]['decision'])
   if kind=='damage':d['runtimePlayers'][0]['damageStateRaw']=1
   if kind=='position':d['observation']['players'][0]['pos']['x']+=4096*8
   if kind=='opponent':d['observation']['players'][1]['pos']=dict(d['observation']['players'][0]['pos'])
   # A jump selected by ordinary control must regain control when invalidated.
   with patch.object(rule._ShotRecoveryBase,'act',return_value=2082):held=c.act(d,frame,2064)
   self.assertEqual(held,2082,kind);self.assertIsNone(c.shot_recovery_plan,kind)
if __name__=='__main__':unittest.main()
