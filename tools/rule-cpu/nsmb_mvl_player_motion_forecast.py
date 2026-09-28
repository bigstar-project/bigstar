"""Diagnostic small/normal player movement approximation, including jump launch.

Used by bounded rule planners and offline diagnostics. NPC/player contacts, block breaking, slopes, wall
kicks, damage and powerup transitions are not modeled. Compare against native
frames before using it for control. Coordinates use downward-positive depth.
"""
import math
from nsmb_mvl_ground_planner import ground_velocity
from nsmb_mvl_jump_physics_audit import horizontal_run_step, horizontal_walk_step, vertical_step


def ground_turn_step(vx,direction,run,facing,turn_remaining):
    """Observed three-frame direction transition; still an approximation."""
    if direction and direction!=facing:
        return math.copysign(max(0,abs(vx)-.03515625),vx),direction,2
    if turn_remaining:
        return math.copysign(max(0,abs(vx)-.078125),vx),facing,turn_remaining-1
    if direction and vx*direction<0:
        return vx+direction*.1875,facing,0
    return ground_velocity(vx,direction,run),facing,0


def forecast_player(x,depth,vx,vy,grounded,previous_input,inputs,occupied,height=16,
                    facing=1,turn_remaining=0,edge_remaining=0,
                    counter_landing_turn=False):
    points=[]
    for held in inputs:
        direction=int(bool(held&16))-int(bool(held&32))
        jump=bool(held&3);was_grounded=grounded
        launch=grounded and jump and not previous_input&3
        if launch:
            speed=abs(vx)
            vy=3.65625 if speed<1 else 3.78125 if speed<1.5 else 3.90625
            grounded=False
        if grounded:
            vx,facing,turn_remaining=ground_turn_step(vx,direction,bool(held&2048),facing,turn_remaining)
        else:
            vx=horizontal_run_step(vx,direction) if direction and held&2048 else horizontal_walk_step(vx,direction)
            # Ordinary airborne input turns the sprite immediately. Carry this
            # facing through landing; otherwise a fictitious ground reversal
            # delays acceleration after an already completed airborne turn.
            if direction:
                facing=direction
                turn_remaining=0
        next_x=x+vx;side=1 if vx>0 else -1;contact=None
        if vx and any(occupied(next_x+side*8,depth-h) for h in (4,11 if height==16 else 19)):
            if not grounded:return points,'wall'
            boundary=math.floor((next_x+side*8)/16)*16
            next_x=boundary-8 if side>0 else boundary+24
            vx=0;contact='ground_wall'
        x=next_x
        if grounded:
            if any(occupied(x+f,depth+1) for f in (-5,0,4)):
                vy=-2  # Observed native grounded velocity field; no movement.
            else:
                # Native walking-off-edge transition: initial grounded vy is
                # integrated with gravity, then temporarily reset by the state
                # transition. Verified in small and super forms, both sides.
                grounded=False;depth+=2.34375;vy=0;edge_remaining=4;contact='edge'
        else:
            if edge_remaining:
                next_depth=depth+.34375
                vy=0 if edge_remaining>=3 else -.34375
                edge_remaining-=1
            else:
                vy=vertical_step(vy,jump);next_depth=depth-vy
            if vy>0:
                for y in range(math.floor((depth-height)/16)*16,math.ceil((next_depth-height)/16)*16-1,-16):
                    if any(occupied(x+f,y-.01) for f in (-2,0,1)):
                        next_depth=y+height;vy=0;contact='ceiling';break
            else:
                for y in range(math.ceil(depth/16)*16,math.floor(next_depth/16)*16+1,16):
                    if any(occupied(x+f,y+.01) for f in (-5,0,4)):
                        next_depth=y;vy=-2;grounded=True;contact='floor'
                        # A measured ordinary-jump landing can enter a turn
                        # state even after airborne facing changed. Shooting
                        # landings need not do so; opt in for that branch or
                        # evaluate both branches as a conservative bound.
                        if counter_landing_turn and direction and vx*direction < 0:
                            facing=-direction
                            turn_remaining=0
                        break
            depth=next_depth
        points.append(dict(x=x,depth=depth,vx=vx,vy=vy,grounded=grounded,
                           contact=contact,launch=launch,was_grounded=was_grounded,
                           facing=facing,turn_remaining=turn_remaining,
                           edge_remaining=edge_remaining))
        previous_input=held
        if depth>352:return points,'deep'
    return points,'horizon'


class MovementForecastCache:
    """Reuse identical movement states within one search on fixed terrain.

    Uses the same integrator, including facing, turn and edge-transition state.
    The cache is local to a planner invocation; neither terrain nor initial
    state may change during that invocation. Returned points are read-only.
    """
    def __init__(self, initial, previous_input, occupied):
        # Item planners previously supplied only these initial physics fields;
        # unrelated metadata in the initial dict must not change the forecast.
        self.initial = {key: initial[key] for key in ('x', 'depth', 'vx', 'vy', 'grounded', 'facing')}
        self.initial['grounded'] = bool(self.initial['grounded'])
        self.previous_input = previous_input
        self.occupied = occupied
        self.transitions = {}

    def iter_points(self, inputs):
        state = self.initial
        previous = self.previous_input
        for held in inputs:
            key = (state['x'], state['depth'], state['vx'], state['vy'],
                   state['grounded'], previous, held, state['facing'],
                   state.get('turn_remaining', 0), state.get('edge_remaining', 0))
            result = self.transitions.get(key)
            if result is None:
                points, reason = forecast_player(
                    *key[:5], previous, [held], self.occupied, height=16,
                    facing=key[7], turn_remaining=key[8], edge_remaining=key[9])
                result = (points[0] if points else None, reason)
                self.transitions[key] = result
            point, reason = result
            if point is not None:
                yield point
                state = point
            if reason != 'horizon':
                return reason
            previous = held
        return 'horizon'

    def forecast(self, inputs):
        stream = self.iter_points(inputs)
        points = []
        while True:
            try:
                points.append(next(stream))
            except StopIteration as end:
                return points, end.value
