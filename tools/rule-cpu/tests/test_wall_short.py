import copy,gzip,importlib.util,json,sys,unittest
from pathlib import Path
from unittest.mock import patch
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import nsmb_mvl_rule_match_v2 as rule
fixture=json.loads(gzip.decompress((Path(__file__).parent/'fixtures/wall-short.json.gz').read_bytes()))

class WallShortTest(unittest.TestCase):
 def choose(self,d,held=2082,previous=2082,target='carrier'):
  c=rule.RoutedHumanRule(0,60);c.jump_until=1296;c.trace={'target':target}
  with patch.object(rule._WallShortBase,'act',return_value=held):result=c.act(d,1284,previous)
  return result,c.trace
 def test_recorded_rescue_and_both_landing_branches(self):
  d=fixture['initial']['decision'];held,trace=self.choose(d)
  self.assertEqual(held,2080);self.assertEqual(trace['wall_short']['landing_after'],32)
  nav=rule.GrassNavigator();nav.update(d['observation']['players'][0])
  points,end=rule._wall_short_path(d,0,[2082]*2+[2080]*6+[2082]*6+[2080]*46,nav,True)
  self.assertEqual(len(points),60)
  for p,q in zip(points,fixture['following']):
   me=q['player'];self.assertEqual(rule.wrap(p['x'],me['pos']['x']/4096),0)
   self.assertEqual(p['depth'],-me['pos']['y']/4096)
 def test_close_opponent_unknown_enemy_and_bad_clock(self):
  d=copy.deepcopy(fixture['initial']['decision']);d['observation']['players'][1]['pos']=dict(d['observation']['players'][0]['pos'])
  self.assertEqual(self.choose(d)[0],2082)
  d=copy.deepcopy(fixture['initial']['decision']);d['time']['inputDelay']=7
  self.assertEqual(self.choose(d)[0],2082)
  d=copy.deepcopy(fixture['initial']['decision']);e=next(e for e in d['observation']['entities'] if e.get('category')=='enemy_koopa')
  e['koopaBehaviorFunctionRaw']=0
  self.assertEqual(self.choose(d)[0],2082)
 def test_does_not_replace_normal_jump_or_new_shot(self):
  d=copy.deepcopy(fixture['initial']['decision']);d['runtimePlayers'][0]['behaviorFuncRaw']=0x02115AAC
  self.assertEqual(self.choose(d)[0],2082)
  d=fixture['initial']['decision']
  self.assertEqual(self.choose(d,previous=34)[0],2082)
  self.assertEqual(self.choose(d,target='natural_star')[0],2082)
 def test_one_unsafe_landing_branch_vetoes_plan(self):
  original=rule._wall_short_path
  def unsafe(*a,**kw):
   result=original(*a,**kw)
   if kw.get('counter_landing_turn') and result:
    points,end=result;points=copy.deepcopy(points)
    for p in points:p['grounded']=False
    return points,end
   return result
  with patch.object(rule,'_wall_short_path',side_effect=unsafe):
   self.assertEqual(self.choose(fixture['initial']['decision'])[0],2082)

if __name__=='__main__':unittest.main()
