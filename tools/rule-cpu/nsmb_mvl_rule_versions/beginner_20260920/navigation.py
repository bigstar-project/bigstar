"""Observed grass geometry: supported waypoints and conservative jump routing."""
import heapq
import json
import math
from pathlib import Path

from .base import solid


def dx(a, b):
    return (a - b + 512) % 1024 - 512


class GrassNavigator:
    def __init__(self):
        path = Path(__file__).with_name('grass-navigation-tiles.json')
        spec = json.loads(path.read_text())
        self.tiles = {(c['x'], c['y']): c['mask'] for c in spec['cells']}
        self.boxes = spec['powerup_boxes']
        self.box_active={tuple(p):True for p in self.boxes}
        self.rebuild()

    def occupied(self, x, y):
        return solid(self.tiles.get((math.floor(x/16)%64, math.floor(y/16))))

    def can_walk_down(self, x, depth, target_x, target_depth):
        if target_depth<=depth+24:return False
        offset=dx(target_x,x)
        # Descending to a lower platform still requires a jump if the corridor
        # crosses a pit deeper than that destination.
        return all(any(self.occupied(x+offset*t,y)
                       for y in range(int(depth)+8,int(target_depth)+17,8))
                   for t in (.2,.4,.6,.8,1.0))

    def rebuild(self):
        self.nodes = [(x*16+8, y*16) for (x,y),mask in self.tiles.items()
                      if 5 <= y <= 21 and solid(mask)
                      and not self.occupied(x*16+8,y*16-8)
                      and not self.occupied(x*16+8,y*16-24)]
        self.edges = {p:[] for p in self.nodes}
        for p in self.nodes:
            for q in self.nodes:
                offset, fall = dx(q[0],p[0]), q[1]-p[1]
                if p == q or abs(offset)>144 or fall < -64 or fall>112:
                    continue
                if fall<0 and abs(offset)>112+.85*fall:
                    continue
                walk = fall==0 and abs(offset)<=32 and all(self.occupied(p[0]+offset*t,p[1]+8) for t in (.25,.5,.75))
                if walk:
                    self.edges[p].append((q,abs(offset),False))
                    continue
                # Approximate arch is a route feasibility filter, not a claim
                # that every surviving edge was validated in the emulator.
                apex = max(40, -fall+12)
                clear = True
                for i in range(1,16):
                    t=i/16
                    x=p[0]+offset*t
                    y=p[1]+fall*t-4*apex*t*(1-t)
                    if any(self.occupied(x+s,y-h) for s in (-5,5) for h in (2,20)):
                        clear=False
                        break
                if clear:
                    self.edges[p].append((q,abs(offset)+45+max(0,-fall),True))

    def update(self, player):
        grid=player['terrain']; ax=player['pos']['x']//65536; ay=(-player['pos']['y'])//65536
        changed=False
        local={(c['rx'],c['ry']):c for c in grid['cells']}
        for rx in range(grid['minRelTileX'],grid['minRelTileX']+grid['width']):
            for ry in range(grid['minRelTileY'],grid['minRelTileY']+grid['height']):
                cell=local.get((rx,ry),dict(found=grid.get('omittedCellFound',False),mask=0))
                if not cell['found'] or cell.get('status',0)!=0:continue
                key=((ax+rx)%64,ay+ry)
                if key in self.box_active:
                    behavior=cell.get('behavior',0)
                    behavior=int(behavior,0) if isinstance(behavior,str) else behavior
                    self.box_active[key]=behavior==0x50000
                if solid(self.tiles.get(key)) != solid(cell['mask']):changed=True
                self.tiles[key]=cell['mask']
        if changed:self.rebuild()

    def route(self, x, depth, goal_x, goal_depth, under=False):
        if not self.nodes:return None
        start=min(self.nodes,key=lambda n:abs(dx(n[0],x))+3*abs(n[1]-depth))
        distance={start:0}; prev={}; heap=[(0,start)]
        while heap:
            cost,p=heapq.heappop(heap)
            if cost!=distance[p]:continue
            for q,w,jump in self.edges[p]:
                new=cost+w
                if new<distance.get(q,float('inf')):
                    distance[q]=new;prev[q]=(p,jump);heapq.heappush(heap,(new,q))
        def goal_cost(n):
            vertical=n[1]-goal_depth
            height_cost=2*abs(vertical) if under else 2*max(0,vertical)+.15*max(0,-vertical)
            if not under and 0 < vertical <= 64 and not any(
                    self.occupied(goal_x,y) for y in range(int(goal_depth),int(n[1])-8,8)):
                # A clear jump from below is preferable to a same-height ledge
                # far away, especially when the star hangs below a ceiling.
                height_cost=.35*vertical
            # A star below a solid ceiling cannot be reached by aligning on
            # top of it. Prefer a route around the platform in that case.
            if vertical < -24 and any(self.occupied(goal_x,y)
                    for y in range(int(n[1])+1,int(goal_depth)-8,8)):
                height_cost += 240
            return abs(dx(n[0],goal_x))+height_cost+distance[n]*.08
        goal=min(distance,key=goal_cost)
        path=[goal]
        while path[-1]!=start:path.append(prev[path[-1]][0])
        path.reverse()
        if len(path)==1:return dict(x=goal_x,depth=goal_depth,jump=False,path=path)
        # Skip intermediate flat walking nodes, stopping at the jump takeoff.
        index=1
        while index+1<len(path) and not prev[path[index]][1] and not prev[path[index+1]][1]:index+=1
        node=path[index]
        return dict(x=node[0],depth=node[1],jump=prev[node][1],path=path)
