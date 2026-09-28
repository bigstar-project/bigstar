"""Grass-stage development CPU with bounded movement and interception plans.

Forecasts cover selected ordinary states, not arbitrary dynamic contacts,
wall kicks or transformations. Plans check observed motion and cancel on
unsupported states or deviations. Preserved opponent profiles are separate.
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
                    facing=1,turn_remaining=0,edge_remaining=0):
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
                        next_depth=y;vy=-2;grounded=True;contact='floor';break
            depth=next_depth
        points.append(dict(x=x,depth=depth,vx=vx,vy=vy,grounded=grounded,
                           contact=contact,launch=launch,was_grounded=was_grounded,
                           facing=facing,turn_remaining=turn_remaining,
                           edge_remaining=edge_remaining))
        previous_input=held
        if depth>352:return points,'deep'
    return points,'horizon'

"""Experimental support selection before landing beside a main-floor pit.

The caller excludes intentional descents and special physics. This is a short
terrain/NPC forecast, not a guarantee against subsequent policy or enemy actions.
"""


def choose_landing_support(initial, proposed, previous, delay, occupied,
                           forecast_player, enemy_paths, horizon=24):
    if initial['grounded'] or not 232 <= initial['depth'] <= 272 or initial['vy'] > -2:
        return None
    if not 0 <= delay <= 6 or occupied(initial['x'], 280):
        return None
    sides = [side for side in (-1, 1)
             if any(occupied(initial['x'] + side * distance, 280) for distance in (8, 16))]
    if not sides or any(len(path) < horizon for path in enemy_paths):
        return None

    def simulate(action):
        return forecast_player(initial['x'], initial['depth'], initial['vx'], initial['vy'],
                               False, previous, [previous] * delay + [action] * (horizon-delay),
                               occupied, height=initial['height'], facing=initial['facing'])

    nominal, _ = simulate(proposed)
    if not any(point['depth'] > 288 for point in nominal):
        return None
    options = []
    for side in sides:
        for run in (0, 2048):
            action = (16 if side > 0 else 32) | run
            points, end = simulate(action)
            if end != 'horizon' or len(points) != horizon:
                continue
            if not all(p['grounded'] and occupied(p['x'], p['depth'] + 8) for p in points[-6:]):
                continue
            unsafe = False
            for i, point in enumerate(points):
                if point['depth'] > 288:
                    unsafe = True
                    break
                for path in enemy_paths:
                    enemy = path[i]
                    dx = (point['x'] - enemy['x'] + 512) % 1024 - 512
                    if abs(dx) < 18 and enemy['depth'] - 14 < point['depth'] < enemy['depth'] + initial['height']:
                        unsafe = True
                        break
                if unsafe:
                    break
            if not unsafe:
                travel = abs(points[-1]['x'] - initial['x'])
                options.append((travel, action, points))
    if not options:
        return None
    _, held, points = min(options, key=lambda option: (option[0], option[1]))
    return dict(held=held, expected_landing=next(i+1 for i,p in enumerate(points) if p['grounded']),
                horizon=horizon, final_x=points[-1]['x'], final_depth=points[-1]['depth'])

"""Human-inspired objectives with stage routes, momentum braking and recovery."""
from nsmb_mvl_rule_match import HumanInspiredRule, delta, terrain_cell, solid
from nsmb_mvl_grass_navigation import GrassNavigator, dx as wrap


class RoutedHumanRule(HumanInspiredRule):
    def targets(self, decision):
        choices=super().targets(decision)
        position=decision['observation']['players'][self.player]['pos']
        for entity in decision['observation']['entities']:
            # Frozen runtimes omit the two-/three-star ground-pound variants.
            # Human replay and native game images identify these exact values;
            # leave unknown settings and the natural star (settings=1) alone.
            if (entity.get('category')!='dropped_star_item' and entity.get('objectId')==34
                    and (entity.get('settings',0)&0x7fffffff) in (0x2102,0x3102,0x3)):
                ex,ey=delta(entity['pos'],position)
                choices.append((abs(ex)+abs(ey)*1.5-80,'dropped_star',entity['pos']))
        return sorted(choices,key=lambda c:c[0])

    def is_equipment_item(self, entity):
        # The native two-client bridge exposes the 0x90000 variant too;
        # ignoring it made Luigi open a box but not pursue its contents.
        return entity.get('objectId')==31 and (
            entity.get('settings') in (0x80000,0x90000) or self.item_kind(entity) in (1,2))

    def item_kind(self, entity):
        kinds=entity.get('kindByPlayer',[])
        if len(kinds)>self.player and kinds[self.player]['confidence']>=2:
            return kinds[self.player]['kind']
        # Three native pickup transitions plus before/after images identify
        # this variant; the frozen runtime currently reports it as unknown.
        if entity.get('objectId')==31 and entity.get('settings')==0x1099:return 3
        # Two native pickups and game images identify this blue-shell variant;
        # the frozen bridge only recognizes the 0x1108b form of the settings.
        if entity.get('objectId')==31 and entity.get('settings')==0x108b:return 4
        return 0

    def needs_equipment(self, runtime):
        return runtime['currentPowerupRaw'] in (0, 1, 4)

    def reset(self):
        super().reset()
        self.navigator=GrassNavigator()
        self.landing_support=None
        self.waypoint=None
        self.last_route=-100
        self.previous_goal=None
        self.takeoff_until=-1
        self.previous_x=None
        self.stuck_frames=0
        self.escape_until=-1
        self.wall_depart_until=-1
        self.wall_depart_direction=0
        self.runup=None
        self.ascent_attempts={}
        self.target_intent=None
        self.item_escape_direction=0
        self.item_escape_until=-1
        self.ledge_return=0

    def act(self, decision, frame, previous_held=0):
        if frame<=self.previous_frame:self.reset()
        elapsed=max(1,frame-self.previous_frame)
        self.previous_frame=frame
        obs=decision['observation']; me=obs['players'][self.player]; other=obs['players'][self.player^1]
        raw=decision['runtimePlayers'][self.player]
        self.trace=dict(frame=frame,player=self.player,target=None,held=0)
        if not me['found'] or me['dead'] or not raw['found']:
            self.waypoint=None;self.previous_x=None;self.target_intent=None
            return 0
        x=me['pos']['x']/4096; depth=-me['pos']['y']/4096; vx=me['vel']['x']/4096
        grounded=bool(raw['collisionFlagRaw']&(1|0x8000)) and not raw['actionFlagRaw']&4
        if frame-self.last_route>=24:self.navigator.update(me)
        if self.previous_x is not None and abs(wrap(x,self.previous_x))<.5 and previous_held&48:
            self.stuck_frames+=elapsed
        else:self.stuck_frames=0
        self.previous_x=x
        choices=self.targets(decision)
        # Remember the two verified grass equipment locations outside the local
        # grid, but never override a closer active drop just to follow a script.
        if self.needs_equipment(raw):
            for bx,by in self.navigator.boxes:
                if not self.navigator.box_active[(bx,by)]:continue
                pos=dict(x=(bx*16+8)*4096,y=-(by*16+8)*4096)
                key=(pos['x']%(1024*4096),pos['y'])
                if frame>=self.box_retry.get(key,-1):
                    tx,ty=delta(pos,me['pos'])
                    choices.append((abs(tx)+abs(ty)*1.5-60,'powerup_box',pos))
        if (me['battleStars']>other['battleStars'] and raw['currentPowerupRaw'] in (0,1,4)
                and not raw['physicsFlagRaw']&32 and any(c[1]!='carrier' for c in choices)):
            # An unarmed leader cannot shoot from the retreat boundary. Chasing
            # the carrier then retreating forever must not block available loot.
            choices=[c for c in choices if c[1]!='carrier']
        if choices:
            selected=min(choices,key=lambda c:c[0])
            if self.target_intent:
                old_kind,old_pos=self.target_intent
                same=[c for c in choices if c[1]==old_kind and all(abs(v)<8 for v in delta(c[2],old_pos))]
                if same:
                    previous=min(same,key=lambda c:c[0])
                    if previous[0]<=selected[0]+64:selected=previous
            _,kind,pos=selected;tx,ty=delta(pos,me['pos'])
            self.target_intent=(kind,pos.copy())
        else:
            self.target_intent=None
            kind='patrol' if raw['currentPowerupRaw']!=2 else 'wait'
            pos=dict(x=((x+160)%1024)*4096,y=-272*4096) if kind=='patrol' else me['pos']
            tx,ty=delta(pos,me['pos'])
        ex,ey=delta(other['pos'],me['pos']) if other['found'] else (1024,0)
        enemy_raw=decision['runtimePlayers'][self.player^1]
        dangerous=other['found'] and not other['dead'] and (enemy_raw['currentPowerupRaw']==3 or enemy_raw['physicsFlagRaw']&32)
        contact_advantage=(raw['currentPowerupRaw']==3 or bool(raw['physicsFlagRaw']&32)) and not dangerous
        if dangerous and abs(ex)<160 and not raw['physicsFlagRaw']&32 and raw['currentPowerupRaw']!=3:
            kind='escape_invincible';pos=dict(x=me['pos']['x']+(-144 if ex>0 else 144)*4096,y=me['pos']['y'])
            tx,ty=delta(pos,me['pos'])
        if (kind=='carrier' and not contact_advantage and me['battleStars']>other['battleStars']
                and abs(ex)<144 and abs(ey)<48):
            kind='protect_lead';pos=dict(x=me['pos']['x']+(-112 if ex>0 else 112)*4096,y=me['pos']['y'])
            tx,ty=delta(pos,me['pos'])
        goal_x=pos['x']/4096; goal_depth=-pos['y']/4096
        if kind=='powerup_box':goal_depth+=56
        goal=(kind,round(goal_x/24),round(goal_depth/16))
        if not grounded and self.previous_goal!=goal:
            # A pickup can change objectives in midair. Steering toward the
            # old star's takeoff point can reverse a safe jump into a pit.
            self.waypoint=self.navigator.route(x,depth,goal_x,goal_depth,under=kind=='powerup_box')
            self.previous_goal=goal
        if grounded and (frame-self.last_route>=24 or self.previous_goal!=goal):
            self.navigator.update(me)
            self.waypoint=self.navigator.route(x,depth,goal_x,goal_depth,under=kind=='powerup_box')
            self.last_route=frame;self.previous_goal=goal
        nav=self.waypoint
        move_dx=wrap(nav['x'],x) if nav else tx
        if abs(tx)<36 and kind!='powerup_box' and (not nav or abs(ty)<24):move_dx=tx
        if kind=='powerup_box' and abs(tx)<36 and ty>30:move_dx=tx
        changing_surface=bool(nav and abs(nav['depth']-depth)>8)
        tolerance=1 if changing_surface else 5
        direction=1 if move_dx>tolerance else -1 if move_dx< -tolerance else 0
        # Start braking before crossing a small target, rather than alternating
        # full-speed left/right after overshooting it.
        if not changing_surface and abs(move_dx)<max(9,abs(vx)*9) and abs(vx)>.65:
            direction=-1 if vx>0 else 1
        # A narrow landing ledge may be directly below us well before landing.
        # Waiting for feet-height alignment overshoots it into the adjacent pit.
        landing_brake=bool(not grounded and nav and depth<nav['depth']
                           and self.navigator.occupied(nav['x'],nav['depth']+8)
                           and move_dx*vx>0 and abs(vx)>.65
                           and abs(move_dx)<vx*vx/.14+3*abs(vx))
        if landing_brake:direction=-1 if vx>0 else 1
        if kind=='carrier' and raw['currentPowerupRaw']==2 and 56<abs(ex)<104 and abs(ey)<32:
            direction=0
        route_jump=bool(nav and nav['jump'] and (abs(move_dx)>12 or depth-nav['depth']>8))
        obstacle=bool(direction and any(solid(terrain_cell(me,direction*d,-16)) for d in (16,32)))
        support=[terrain_cell(me,direction*32,d) for d in (8,24,40)] if direction else [1]
        gap=all(c is not None and not solid(c) for c in support)
        jump=route_jump or obstacle or gap or (ty>20 and abs(tx)<40)
        if nav and not obstacle and self.navigator.can_walk_down(x,depth,nav['x'],nav['depth']):
            # Descend to the selected support; jumping here can put us back on
            # the ceiling whose underside contains the target.
            jump=False
            if grounded:self.jump_until=frame
        if kind=='powerup_box' and abs(tx)<40:
            jump=abs(tx)<10 and ty>24
        stomp_attempt=(kind=='carrier' and not dangerous and raw['currentPowerupRaw'] not in (2,3)
                       and 24<abs(ex)<80 and -12<ey<24 and direction*ex>0)
        close_stomp=(kind=='carrier' and not dangerous and grounded
                     and raw['currentPowerupRaw'] not in (2,3)
                     and abs(ex)<=24 and abs(ey)<12)
        if close_stomp:
            direction=0
            jump=True
        if stomp_attempt:jump=True
        high_ascent=bool(nav and nav['jump'] and nav.get('path')
                         and nav['path'][0][1]-nav['depth']>=56)
        ascent_key=(nav['x']%1024,nav['depth']) if high_ascent else None
        attempts,last_attempt=self.ascent_attempts.get(ascent_key,(0,-1000))
        if frame-last_attempt>360:attempts=0
        if self.runup and (not grounded or frame>self.runup['expires'] or kind!=self.runup['kind']):
            self.runup=None
        if (not self.runup and grounded and high_ascent and attempts>=2 and abs(vx)<1.25
                and 24<abs(move_dx)<64 and kind!='powerup_box'):
            toward=1 if move_dx>0 else -1
            back=x-toward*24
            if all(self.navigator.occupied(back+s,depth+8) for s in (-8,0,8)):
                self.runup=dict(back=back,takeoff=x,toward=toward,phase='back',
                                expires=frame+180,kind=kind)
                self.ascent_attempts[ascent_key]=(0,frame)
        runup_active=self.runup is not None
        if self.runup:
            plan=self.runup
            if plan['phase']=='back':
                offset=wrap(plan['back'],x)
                direction=1 if offset>4 else -1 if offset< -4 else 0
                if abs(offset)<max(8,abs(vx)*9) and abs(vx)>.35:
                    direction=-1 if vx>0 else 1
                if abs(offset)<8 and abs(vx)<.4:plan['phase']='go'
                jump=False
            if plan['phase']=='go':
                direction=plan['toward']
                jump=wrap(plan['takeoff'],x)*direction<4 and vx*direction>1.25
        intentional_drop=kind in ('natural_star','dropped_star','powerup') and ty< -24 and abs(tx)<48
        if intentional_drop:jump=False
        danger=False
        landing_avoid=0
        item_avoid=0
        body_avoid=0
        body_jump=(other['found'] and not other['dead'] and me['battleStars']>0
                   and not raw['physicsFlagRaw']&32 and raw['currentPowerupRaw']!=3
                   and abs(ey)<24 and abs(ex)<64 and direction*ex>0)
        if body_jump and grounded:
            if abs(ex)>24:jump=True
            else:body_avoid=-1 if ex>=0 else 1
        body_evade=False
        if (other['found'] and not other['dead'] and me['battleStars']>0
                and not raw['physicsFlagRaw']&32 and raw['currentPowerupRaw']!=3
                and abs(ex)<48 and abs(ey)<32):
            relative_vx=other['vel']['x']/4096-vx
            # A pursuer can catch a walking landing before the next decision.
            # Do not brake back toward a new star while that contact is near.
            body_evade=ex*relative_vx<0 and abs(ex+relative_vx*8)<24
            if body_evade:body_avoid=-1 if ex>=0 else 1
        overhead_evade=(grounded and not contact_advantage and me['battleStars']>0 and other['found']
                        and not other['dead'] and 24<ey<72 and abs(ex)<32
                        and other['vel']['y']<0)
        if overhead_evade:
            fall_speed=max(0,-other['vel']['y']/4096)
            time_to_head=((fall_speed*fall_speed+2*.3125*max(0,ey-24))**.5-fall_speed)/.3125
            landing_dx=ex+(other['vel']['x']/4096-vx)*min(24,time_to_head)
            body_avoid=-1 if landing_dx>=0 else 1
            body_evade=True
        for entity in obs['entities']:
            category=entity['category']
            if raw['currentPowerupRaw']==2 and self.item_kind(entity) in (3,4):
                hx,hy=delta(entity['pos'],me['pos'])
                if abs(hx)<48 and -8<hy<80:
                    if hy<8 and direction*hx>0:
                        jump=True
                    else:
                        # A falling coin reward initially follows the player.
                        # Tiny horizontal crossings must not reverse escape
                        # before momentum has carried us clear of the drop.
                        if frame>=self.item_escape_until:
                            self.item_escape_direction=-1 if hx>=0 else 1
                            self.item_escape_until=frame+60
                        item_avoid=self.item_escape_direction
            if raw['currentPowerupRaw']==3 or raw['physicsFlagRaw']&32:continue
            if category not in ('enemy_goomba','enemy_koopa','player_fireball'):continue
            if category=='player_fireball' and (entity.get('owner')==self.player or not entity.get('ownerVerified')):continue
            hx,hy=delta(entity['pos'],me['pos'])
            if abs(hx)<48 and -8<hy<32 and (hx*direction>0 or category=='player_fireball'):
                jump=True;danger=True
            if category in ('enemy_goomba','enemy_koopa') and not grounded and me['vel']['y']<0 and -96<hy< -12:
                time_to_level=(-hy)/max(1,-me['vel']['y']/4096)
                future=hx+(entity['vel']['x']/4096-vx)*time_to_level
                if time_to_level<24 and abs(future)<28:
                    landing_avoid=-1 if hx>0 else 1
        if self.stuck_frames>90:
            self.escape_until=frame+30;self.stuck_frames=0
        if frame<self.escape_until:
            direction=-1 if tx>0 else 1;jump=True
        # Stair pits extend above the main floor. Continue wall recovery inside
        # the shaft instead of handing control back at depth 272 mid-climb.
        left=next((n for n in range(8,65,8) if solid(terrain_cell(me,-n,-4))),None)
        right=next((n for n in range(8,65,8) if solid(terrain_cell(me,n,-4))),None)
        in_shaft=(left is not None and right is not None and left+right<=48
                  and not self.navigator.occupied(x,depth+8))
        recovery=not grounded and (depth>272 or in_shaft) and not intentional_drop
        wall_slide=bool(raw['actionFlagRaw']&4)
        if recovery:
            # Press into a pit wall to initiate sliding even when the current
            # objective has vanished. A pickup must not leave the CPU idle here.
            direction=-1 if left is not None and (right is None or left<=right) else 1
            if frame<self.wall_depart_until:direction=self.wall_depart_direction
        if wall_slide and not intentional_drop:
            if not previous_held&2:
                jump=True
                wall_left=bool(raw['collisionFlagRaw']&0x408)
                direction=-1 if wall_left else 1
                self.wall_depart_direction=-direction
                self.wall_depart_until=frame+12
                self.jump_until=frame+18;self.next_jump=frame+12
            else:
                self.jump_until=frame
        if self.runup and not jump:self.jump_until=frame
        if jump and frame>=self.next_jump and (grounded or raw['actionFlagRaw']&4):
            if grounded and high_ascent and not self.runup:
                self.ascent_attempts[ascent_key]=(attempts+1,frame)
            self.jump_until=frame+30;self.next_jump=frame+42
        ledge_reset=0
        # A tile-contact flag can persist while only the body's edge is supported.
        # Recover a stalled takeoff, but do not reverse normal moving descents.
        unsupported_ledge=grounded and route_jump and not intentional_drop and not self.navigator.occupied(x,depth+8)
        if not unsupported_ledge:self.ledge_return=0
        if unsupported_ledge and (self.ledge_return or (abs(vx)<.5 and previous_held&2)):
            supports=[(offset,sign) for offset in (8,16,24) for sign in (-1,1)
                      if self.navigator.occupied(x+sign*offset,depth+8)]
            if supports:
                ledge_reset=self.ledge_return or min(supports)[1]
                self.ledge_return=ledge_reset
                direction=ledge_reset
                self.jump_until=frame
                self.next_jump=frame+6
        held=(16 if direction>0 else 32 if direction<0 else 0)|(2 if frame<self.jump_until else 0)
        if landing_avoid and not recovery:
            held=(held&~(48|2))|(16 if landing_avoid>0 else 32)
            self.jump_until=frame
        if item_avoid and not recovery:
            held=(held&~(48|2))|(16 if item_avoid>0 else 32)
            self.jump_until=frame
        if body_avoid and not recovery:
            held=(held&~48)|(16 if body_avoid>0 else 32)
        # A short, high ascent still needs running speed: walking from rest
        # reaches the block side only after the jump apex, then wall-kicks back.
        ascent_run=high_ascent and not recovery
        if direction and (abs(move_dx)>48 or ascent_run) and kind!='powerup_box':
            held|=2048 if frame%self.period>=6 else 0
        if self.runup:
            held=(held&~2048)|(2048 if self.runup['phase']=='go' else 0)
        if body_evade and not recovery:
            held|=2048
        can_shoot=raw['currentPowerupRaw']==2 and other['found'] and not other['dead'] and abs(ex)<240 and -96<ey<24
        if can_shoot and frame%self.period<12 and not gap and not runup_active and not body_avoid:
            if frame%self.period<6:
                held=(held&~(48|2048))|(32 if ex<0 else 16)
            elif raw['facingKnown'] and raw['facing']==(-1 if ex<0 else 1):
                held|=2048
        if ledge_reset:
            held=(held&~(48|2|2048))|(16 if ledge_reset>0 else 32)
        if raw['inventoryPowerupRaw'] in (1,2) and raw['currentPowerupRaw'] in (0,1,4) and frame%120<6:held|=1024
        self.trace.update(target=kind,dx=tx,dy=ty,nav_dx=move_dx,waypoint=nav,
                          ledge_reset=ledge_reset,gap=gap,danger=danger,grounded=grounded,held=held,stuck=self.stuck_frames,
                          recovery=recovery,in_shaft=in_shaft,landing_avoid=landing_avoid,item_avoid=item_avoid,
                          stomp_attempt=stomp_attempt,close_stomp=close_stomp,ascent_run=ascent_run,
                          landing_brake=landing_brake,runup=self.runup.copy() if self.runup else None,
                          contact_advantage=contact_advantage,overhead_evade=overhead_evade,body_jump=body_jump,body_avoid=body_avoid,body_evade=body_evade,ascent_retries=attempts)
        # Experimental default running, preserving explicit precision phases.
        precision=(recovery or ledge_reset or landing_brake or runup_active or close_stomp
                   or (kind=='powerup_box' and abs(tx)<16))
        shoot_release=can_shoot and frame%self.period<6
        run_default=(bool(held&48) and not held&2048 and raw['currentPowerupRaw'] in (0,1,2)
                     and not precision and not shoot_release)
        if run_default:held|=2048
        self.trace['run_default']=run_default
        self.trace['run_precision']=bool(precision)
        self.trace['held']=held
        support=None
        eligible=(raw['currentPowerupRaw'] in (0,1,2) and not me['dead']
            and not raw.get('damageStateRaw',1) and not raw.get('updateLockedRaw',1)
            and raw['physicsFlagRaw'] in (0,2,130,2050)
            and raw['actionFlagRaw'] in (0,0x100000) and not intentional_drop and not recovery)
        nearby=[e for e in decision['observation']['entities']
                if e.get('category') in ('enemy_goomba','enemy_koopa','player_fireball')
                and abs(delta(e['pos'],me['pos'])[0])<96]
        if not eligible or nearby or (other['found'] and not other['dead'] and abs(ex)<48 and abs(ey)<64):
            self.landing_support=None
        elif self.landing_support and frame>=self.landing_support['until']:
            self.landing_support=None
        if eligible and not nearby and self.landing_support is None and not (other['found'] and not other['dead'] and abs(ex)<48 and abs(ey)<64):
            support=choose_landing_support(dict(x=x,depth=depth,vx=vx,vy=me['vel']['y']/4096,
                grounded=grounded,height=16 if raw['currentPowerupRaw']==0 else 27,facing=raw['facing']),
                held,previous_held,int(decision['time'].get('inputDelay',2)),self.navigator.occupied,forecast_player,[])
            if support:self.landing_support=dict(held=support['held'],until=frame+24,forecast=support)
        if self.landing_support:
            held=self.landing_support['held']
            self.jump_until=frame
        self.trace['landing_support']=self.landing_support.copy() if self.landing_support else None
        self.trace['held']=held
        return held


# Experimental generic state-based interception; not the adopted controller.
from nsmb_mvl_emerging_item_plan import plan_ground_interception
from nsmb_mvl_emerging_ascent_plan import plan_ascent_interception
from nsmb_mvl_interception_watch import invalidation_reason
from nsmb_mvl_goomba_forecast import forecast_ground_goomba
_OriginalRule = RoutedHumanRule
class RoutedHumanRule(_OriginalRule):
    def reset(self):
        super().reset()
        self.emergence_plan = None

    def act(self, decision, frame, previous_held=0):
        held = super().act(decision, frame, previous_held)
        me = decision['observation']['players'][self.player]
        raw = decision['runtimePlayers'][self.player]
        entities = decision['observation']['entities']
        active = getattr(self, 'emergence_plan', None)
        eligible = (raw['currentPowerupRaw'] == 0 and not me['dead']
                    and not raw.get('damageStateRaw',1) and not raw.get('updateLockedRaw',1)
                    and raw['physicsFlagRaw'] in (2, 130, 2050)
                    and raw['actionFlagRaw'] in (0, 0x100000))
        x, depth = me['pos']['x']/4096, -me['pos']['y']/4096
        if active or any(e.get('itemBehaviorFunctionRaw') == 0x020D438C for e in entities):
            self.navigator.update(me)
        terrain_signature = frozenset(key for key, mask in self.navigator.tiles.items() if solid(mask))
        if active:
            reason = ('player_state' if not eligible else
                      invalidation_reason(active, frame, me, entities, terrain_signature))
            if reason:
                self.trace['emergence_plan_cancel'] = reason
                active = None
        if (active is None and eligible and me['vel']['y'] == -8192
                and any(self.navigator.occupied(x+foot, depth+1) for foot in (-5, 0, 4))):
            items = [e for e in entities if e.get('itemBehaviorFunctionRaw') == 0x020D438C
                     and abs(delta(e['pos'], me['pos'])[0]) < 144]
            paths = []
            known = True
            for e in entities:
                if e.get('category') not in ('enemy_goomba', 'enemy_koopa', 'player_fireball'):
                    continue
                if abs(delta(e['pos'], me['pos'])[0]) > 256:
                    continue
                if e.get('category') != 'enemy_goomba' or abs(e['vel']['x']) != 2048:
                    known = False; break
                path, reason = forecast_ground_goomba(e['pos']['x']/4096,
                    -e['pos']['y']/4096, e['vel']['x']/4096, self.navigator.occupied, 160)
                if len(path) != 160:
                    known = False; break
                paths.append(path)
            if known:
                for item in items:
                    initial = dict(x=x, depth=depth, vx=me['vel']['x']/4096,
                                   vy=me['vel']['y']/4096, grounded=True, facing=raw['facing'])
                    plans = [planner(initial, previous_held,
                        int(decision['time'].get('inputDelay', 2)), item,
                        self.navigator.occupied, paths) for planner in
                        (plan_ground_interception, plan_ascent_interception)]
                    valid = [plan for plan in plans if plan]
                    plan = min(valid, key=lambda p:p['pickup_frame']) if valid else None
                    if plan:
                        active = dict(plan, start=frame, terrain_signature=terrain_signature)
                        break
        self.emergence_plan = active
        if active:
            elapsed = frame-active['start']
            sequence = active['input_sequence']
            held = sequence[elapsed] if elapsed < len(sequence) else 0
            self.jump_until = frame
            self.landing_support = None
            self.trace['emergence_intercept'] = {k:active[k] for k in
                ('start', 'held', 'duration', 'pickup_frame', 'item_guid', 'item_x', 'item_depth')}
        self.trace['held'] = held
        return held

from nsmb_mvl_route_launch_plan import choose_launch_delay
from nsmb_mvl_koopa_forecast import forecast_ground_koopa
_LaunchBase = RoutedHumanRule
class RoutedHumanRule(_LaunchBase):
    def reset(self):
        super().reset()
        self.launch_plan = None

    def act(self, decision, frame, previous_held=0):
        held = super().act(decision, frame, previous_held)
        me = decision['observation']['players'][self.player]
        raw = decision['runtimePlayers'][self.player]
        x, depth = me['pos']['x']/4096, -me['pos']['y']/4096
        eligible = (not me['dead'] and raw['currentPowerupRaw'] in (0, 1, 2)
                    and not raw.get('damageStateRaw',1) and not raw.get('updateLockedRaw',1)
                    and raw['physicsFlagRaw'] in (2, 130, 2050)
                    and raw['actionFlagRaw'] in (0, 0x100000))
        active = self.launch_plan
        if active:
            elapsed = frame-active['start']
            self.navigator.update(me)
            signature = frozenset(k for k,m in self.navigator.tiles.items() if solid(m))
            reason = None
            if not eligible or getattr(self, 'emergence_plan', None):reason = 'player_state'
            elif elapsed < 0 or elapsed >= active['arrival']:reason = 'finished'
            elif signature != active['terrain_signature']:reason = 'terrain_changed'
            elif elapsed > 0:
                expected = active['points'][elapsed-1]
                if abs(wrap(x, expected['x'])) > 2 or abs(depth-expected['depth']) > 2:
                    reason = 'player_deviation'
            if not reason and elapsed > 0:
                entities = decision['observation']['entities']
                for hazard in active['hazards']:
                    entity = next((e for e in entities if e.get('actorGuid') == hazard['guid']),None)
                    if entity is None:
                        if hazard['lower_bound']:continue
                        reason = 'enemy_missing';break
                    expected = hazard['points'][min(elapsed-1,len(hazard['points'])-1)]
                    ex,ey = entity['pos']['x']/4096,-entity['pos']['y']/4096
                    if hazard['lower_bound']:
                        if ey < expected['depth']-2:reason = 'enemy_vertical_bound'
                    elif abs(wrap(ex,expected['x'])) > 2 or abs(ey-expected['depth']) > 2:
                        reason = 'enemy_deviation'
                    if reason:break
                known_ids = {h['guid'] for h in active['hazards']}
                if any(e.get('category') in ('enemy_goomba','enemy_koopa','player_fireball')
                       and e.get('actorGuid') not in known_ids
                       and abs(delta(e['pos'],me['pos'])[0]) < 96
                       and abs(delta(e['pos'],me['pos'])[1]) < 64 for e in entities):
                    reason = 'new_hazard'
                other = decision['observation']['players'][1-self.player]
                if other['found'] and not other['dead'] and abs(delta(other['pos'],me['pos'])[0]) < 48:
                    reason = 'opponent_near'
            if reason:
                self.trace['launch_plan_cancel'] = reason
                active = None
        if (active is None and eligible and held & 2 and me['vel']['y'] == -8192
                and not getattr(self, 'emergence_plan', None)):
            self.navigator.update(me)
            grounded = any(self.navigator.occupied(x+f,depth+1) for f in (-5,0,4))
            nav = self.trace.get('waypoint') or {}
            goals = [dict(x=gx,depth=gy) for gx,gy in nav.get('path',[])
                     if 24 <= abs(wrap(gx,x)) <= 144 and -64 <= gy-depth <= -8]
            entities = decision['observation']['entities']
            paths, known, hazards = [], True, []
            for e in entities:
                category = e.get('category')
                if category not in ('enemy_goomba','enemy_koopa','player_fireball'):
                    continue
                if abs(delta(e['pos'],me['pos'])[0]) > 256:continue
                lower_bound = (category == 'enemy_goomba'
                    and e.get('goombaBehaviorFunctionRaw') in (0x020E1478, 0x020E1538)
                    and e['vel']['y'] <= -14336 and -e['pos']['y']/4096 > depth+48)
                if lower_bound:
                    # Worst-case horizontal overlap; only vertical separation is used.
                    path = [dict(x=x,depth=-e['pos']['y']/4096)]*96
                elif category == 'enemy_koopa':
                    path, _ = forecast_ground_koopa(e,self.navigator.occupied,96)
                elif category == 'enemy_goomba' and abs(e['vel']['x']) == 2048:
                    path, _ = forecast_ground_goomba(e['pos']['x']/4096,
                        -e['pos']['y']/4096,e['vel']['x']/4096,self.navigator.occupied,96)
                else:path = []
                if not path:known=False;break
                paths.append(path)
                hazards.append(dict(guid=e.get('actorGuid'),points=path,lower_bound=lower_bound))
            other = decision['observation']['players'][1-self.player]
            if other['found'] and not other['dead'] and abs(delta(other['pos'],me['pos'])[0]) < 64:
                known = False
            if known and grounded:
                initial = dict(x=x,depth=depth,vx=me['vel']['x']/4096,
                    vy=me['vel']['y']/4096,grounded=True,facing=raw['facing'],
                    height=16 if raw['currentPowerupRaw']==0 else 27)
                for goal in goals:
                    plan = choose_launch_delay(initial,previous_held,
                        int(decision['time'].get('inputDelay',2)),goal,self.navigator.occupied,paths)
                    if plan:
                        active = dict(plan,start=frame,goal=goal,hazards=hazards,
                            terrain_signature=frozenset(k for k,m in self.navigator.tiles.items() if solid(m)))
                        break
        self.launch_plan = active
        if active:
            elapsed = frame-active['start']
            sequence = active['input_sequence']
            held = sequence[min(elapsed,len(sequence)-1)]
            self.jump_until = frame
            self.trace['launch_plan'] = {k:active[k] for k in ('start','wait','hold','arrival','goal')}
        self.trace['held'] = held
        return held

from nsmb_mvl_air_landing_plan import choose_air_landing
_AirBase = RoutedHumanRule
class RoutedHumanRule(_AirBase):
    def reset(self):
        super().reset()
        self.air_plan = None

    def act(self, decision, frame, previous_held=0):
        held = super().act(decision, frame, previous_held)
        me = decision['observation']['players'][self.player]
        raw = decision['runtimePlayers'][self.player]
        other = decision['observation']['players'][1-self.player]
        x, depth = me['pos']['x']/4096,-me['pos']['y']/4096
        grounded = bool(raw['collisionFlagRaw'] & 0x8001)
        eligible = (not me['dead'] and raw['currentPowerupRaw'] in (0,1,2)
                    and not raw.get('damageStateRaw',1) and not raw.get('updateLockedRaw',1)
                    and raw['physicsFlagRaw'] in (0,2,128,130,2050)
                    and raw['actionFlagRaw'] in (0,0x100000))
        nearby = any(e.get('category') in ('enemy_goomba','enemy_koopa','player_fireball')
            and abs(delta(e['pos'],me['pos'])[0]) < 128
            and abs(delta(e['pos'],me['pos'])[1]) < 96
            for e in decision['observation']['entities'])
        nearby = nearby or (other['found'] and not other['dead']
            and abs(delta(other['pos'],me['pos'])[0]) < 64
            and abs(delta(other['pos'],me['pos'])[1]) < 64)
        active = self.air_plan
        if active:
            elapsed = frame-active['start']
            self.navigator.update(me)
            signature = frozenset(k for k,m in self.navigator.tiles.items() if solid(m))
            reason = None
            if not eligible or nearby:reason = 'state_or_hazard'
            elif elapsed < 0 or elapsed >= min(active['settle']+6, len(active['points'])):reason = 'finished'
            elif signature != active['terrain_signature']:reason = 'terrain_changed'
            elif elapsed > 0:
                index = min(elapsed-1,len(active['points'])-1)
                points = [active[k][index] for k in ('points','conservative_points')]
                offsets = [wrap(x,q['x']) for q in points]
                if min(offsets)>2 or max(offsets)<-2 or not min(q['depth'] for q in points)-2<=depth<=max(q['depth'] for q in points)+2:
                    reason = 'outside_prediction'
            if reason:
                self.trace['air_plan_cancel'] = reason
                active = None
        intentional_drop = (self.trace.get('target') in ('natural_star','dropped_star','powerup')
            and self.trace.get('dy',0) < -24 and abs(self.trace.get('dx',1024)) < 48)
        if (active is None and eligible and not nearby and not grounded and not intentional_drop
                and not self.launch_plan and not self.emergence_plan
                and raw.get('behaviorFuncRaw') in (0x021135B8,0x02110DC0)):
            self.navigator.update(me)
            initial = dict(x=x,depth=depth,vx=me['vel']['x']/4096,vy=me['vel']['y']/4096,
                grounded=False,height=16 if raw['currentPowerupRaw']==0 else 27,facing=raw['facing'])
            plan = choose_air_landing(initial,held,previous_held,
                int(decision['time'].get('inputDelay',2)),self.navigator.occupied)
            if plan:
                active = dict(plan,start=frame,terrain_signature=frozenset(
                    k for k,m in self.navigator.tiles.items() if solid(m)))
        self.air_plan = active
        if active:
            held = active['held']
            self.jump_until = frame
            self.landing_support = None
            self.trace['air_landing_plan'] = {k:active[k] for k in ('start','held','arrival','settle')}
        self.trace['held'] = held
        return held

from nsmb_mvl_ceiling_npc_plan import choose_ceiling_npc_brake
from nsmb_mvl_koopa_forecast import forecast_ground_koopa
_CeilingNpcBase = RoutedHumanRule
class RoutedHumanRule(_CeilingNpcBase):
    def reset(self):
        super().reset()
        self.ceiling_npc_plan = None
        self.ceiling_navigator = GrassNavigator()

    def act(self, decision, frame, previous_held=0):
        held = super().act(decision, frame, previous_held)
        me = decision['observation']['players'][self.player]
        raw = decision['runtimePlayers'][self.player]
        entities = decision['observation']['entities']
        other = decision['observation']['players'][1-self.player]
        x, depth = me['pos']['x']/4096, -me['pos']['y']/4096
        eligible = (not me['dead'] and raw['currentPowerupRaw']==0
            and not raw.get('damageStateRaw',1) and not raw.get('updateLockedRaw',1)
            and raw['physicsFlagRaw'] in (0,2,130,2050)
            and raw['actionFlagRaw'] in (0,0x100000))
        opponent_near = (other['found'] and not other['dead']
            and abs(delta(other['pos'],me['pos'])[0])<64
            and abs(delta(other['pos'],me['pos'])[1])<64)
        self.ceiling_navigator.update(me)
        signature = frozenset(k for k,m in self.ceiling_navigator.tiles.items() if solid(m))
        active = self.ceiling_npc_plan
        if active:
            elapsed = frame-active['start']
            reason = None
            if not eligible or opponent_near: reason='state_or_opponent'
            elif elapsed<0 or elapsed>=active['duration']: reason='finished'
            elif signature!=active['terrain_signature']: reason='terrain_changed'
            elif elapsed>0:
                branches=(active['points'],active['conservative_points'])
                if not any(abs(wrap(x,p[elapsed-1]['x']))<=2
                    and abs(depth-p[elapsed-1]['depth'])<=2 for p in branches):
                    reason='player_deviation'
                for hazard in active['hazards']:
                    entity=next((e for e in entities if e.get('actorGuid')==hazard['guid']),None)
                    expected=hazard['points'][elapsed-1]
                    if entity is None or abs(wrap(entity['pos']['x']/4096,expected['x']))>2 or abs(-entity['pos']['y']/4096-expected['depth'])>2:
                        reason='enemy_deviation';break
                ids={h['guid'] for h in active['hazards']}
                if any(e.get('category') in ('enemy_goomba','enemy_koopa','player_fireball')
                    and e.get('actorGuid') not in ids
                    and abs(delta(e['pos'],me['pos'])[0])<96
                    and abs(delta(e['pos'],me['pos'])[1])<64 for e in entities):
                    reason='new_hazard'
            if reason:
                self.trace['ceiling_npc_cancel']=reason
                active=None
        if (active is None and eligible and not opponent_near
                and me['vel']['y']<=0 and me['vel']['y']!=-8192
                and not self.emergence_plan and not self.launch_plan and not self.air_plan
                and any(self.ceiling_navigator.occupied(x+dx,depth-20) for dx in (-8,0,8))):
            paths=[];hazards=[];known=True
            for e in entities:
                if e.get('category') not in ('enemy_goomba','enemy_koopa','player_fireball'):continue
                if abs(delta(e['pos'],me['pos'])[0])>128 or abs(delta(e['pos'],me['pos'])[1])>96:continue
                if e.get('goombaBehaviorFunctionRaw')==0x020E13D0:continue
                if e.get('category')!='enemy_koopa':known=False;break
                path,_=forecast_ground_koopa(e,self.ceiling_navigator.occupied,36)
                if len(path)!=36:known=False;break
                paths.append(path);hazards.append(dict(guid=e.get('actorGuid'),points=path))
            if known and paths:
                initial=dict(x=x,depth=depth,vx=me['vel']['x']/4096,vy=me['vel']['y']/4096,
                    grounded=False,height=16,facing=raw['facing'])
                plan=choose_ceiling_npc_brake(initial,held,previous_held,
                    int(decision['time'].get('inputDelay',2)),self.ceiling_navigator.occupied,paths)
                if plan: active=dict(plan,start=frame,hazards=hazards,terrain_signature=signature)
        self.ceiling_npc_plan=active
        if active:
            held=active['held']
            self.jump_until=frame
            self.landing_support=None
            self.trace['ceiling_npc_plan']={k:active[k] for k in ('start','held','duration')}
        self.trace['held']=held
        return held

# Candidate only: bounded escape from a blocked wall-kick ascent.
_WallExitFireBase = RoutedHumanRule
class RoutedHumanRule(_WallExitFireBase):
    def reset(self):
        super().reset()
        self.exit_fire_plan = None
        self.exit_fire_retry = -1

    def act(self, decision, frame, previous_held=0):
        held = super().act(decision, frame, previous_held)
        obs = decision['observation']
        me = obs['players'][self.player]
        raw = decision['runtimePlayers'][self.player]
        x, depth = me['pos']['x']/4096, -me['pos']['y']/4096
        vx, vy = me['vel']['x']/4096, me['vel']['y']/4096
        grounded = self.trace.get('grounded', False)
        if me['dead'] or raw['currentPowerupRaw'] != 2:
            self.exit_fire_plan = None
            return held
        active = self.exit_fire_plan
        if active and grounded and active['landed'] is None:
            active['landed'] = frame
        if active and (frame >= active['end'] or
                       (active['landed'] is not None and frame >= active['landed']+6)):
            self.exit_fire_plan = None
            active = None
        if (active is None and frame >= self.exit_fire_retry and not grounded
                and int(decision.get('time', {}).get('inputDelay', 2)) == 2
                and raw['actionFlagRaw'] & 32 and previous_held & 2
                and not previous_held & 2048 and abs(vx) < .1 and 1.5 < vy < 3.3
                and raw.get('facingKnown') and not raw.get('damageStateRaw', 0)):
            own_balls = sum(e['category']=='player_fireball' and e.get('ownerVerified')
                            and e.get('owner')==self.player for e in obs['entities'])
            if own_balls < 2:
                direction = raw['facing']
                wall_x = x + direction*10
                occupied = self.navigator.occupied
                if occupied(wall_x, depth-4):
                    surfaces = [y for y in range(math.floor(depth/16)*16,
                                                 math.floor((depth-64)/16)*16, -16)
                                if occupied(wall_x, y+1) and not occupied(wall_x, y-1)
                                and not occupied(wall_x, y-24)]
                    velocity, rise, maximum = vy, 0.0, 0.0
                    for _ in range(32):
                        velocity = vertical_step(velocity, True)
                        rise += velocity
                        maximum = max(maximum, rise)
                    options = [y for y in surfaces if 8 < depth-y <= maximum]
                    if options:
                        surface = max(options)
                        active = dict(start=frame, end=frame+42, direction=direction,
                                      surface=surface, predicted_rise=maximum, landed=None)
                        self.exit_fire_plan = active
                        self.exit_fire_retry = frame+90
        if active:
            held = 16 if active['direction'] > 0 else 32
            if not grounded and frame < active['start']+18:
                held |= 2048 | 2
            self.trace.update(held=held, wall_exit_fire=active.copy())
        return held
# Candidate: preserve an existing retreat through an imminent ground landing.
_GroundAwayBase = RoutedHumanRule
class RoutedHumanRule(_GroundAwayBase):
    def reset(self):
        super().reset()
        self.ground_away_plan = None

    def act(self, decision, frame, previous_held=0):
        held = super().act(decision, frame, previous_held)
        obs = decision['observation']
        me = obs['players'][self.player]
        raw = decision['runtimePlayers'][self.player]
        other = obs['players'][self.player^1]
        x, depth = me['pos']['x']/4096, -me['pos']['y']/4096
        vx, vy = me['vel']['x']/4096, me['vel']['y']/4096
        opponent_near = (other['found'] and not other['dead']
                         and abs(delta(other['pos'],me['pos'])[0]) < 64
                         and abs(delta(other['pos'],me['pos'])[1]) < 64)
        eligible = (not me['dead'] and raw['currentPowerupRaw']==0
                    and not raw.get('damageStateRaw',1) and not raw.get('updateLockedRaw',1)
                    and not opponent_near)
        active = self.ground_away_plan
        if active:
            enemy = next((e for e in obs['entities'] if e.get('actorGuid')==active['guid']),None)
            if (not eligible or frame >= active['end'] or enemy is None
                    or abs(delta(enemy['pos'],me['pos'])[0]) >= 28
                    or abs(delta(enemy['pos'],me['pos'])[1]) > 24):
                active = None
        if (active is None and eligible and not self.trace.get('grounded')
                and -4 <= vy < 0 and .25 <= abs(vx) <= 3 and not held & 2
                and raw['actionFlagRaw'] == 0x100000):
            proposed_direction = int(bool(held & 16))-int(bool(held & 32))
            old_direction = int(bool(previous_held & 16))-int(bool(previous_held & 32))
            if proposed_direction and old_direction == -proposed_direction:
                for enemy in obs['entities']:
                    if (enemy.get('category')!='enemy_goomba'
                            or enemy.get('goombaBehaviorFunctionRaw')!=0x020E1538
                            or abs(enemy['vel']['x'])!=2048 or enemy['vel']['y']!=0):
                        continue
                    dx, dy = delta(enemy['pos'],me['pos'])
                    if not (8 < abs(dx) < 24 and -12 <= dy <= 0
                            and dx*vx < 0 and dx*proposed_direction > 0):
                        continue
                    ground_depth = -enemy['pos']['y']/4096
                    if not all(self.navigator.occupied(x+offset,ground_depth+1)
                               for offset in (-8,0,8)):
                        continue
                    retreat_direction = -1 if dx>0 else 1
                    # Maximum ordinary running speed is 3 px/frame. Require
                    # floor through the whole bounded retreat, including feet.
                    # This guard must not exchange a ground hit for a pit fall.
                    if not all(self.navigator.occupied(x+retreat_direction*distance,
                                                       ground_depth+1)
                               for distance in range(0, 97, 4)):
                        continue
                    active = dict(start=frame,end=frame+30,guid=enemy.get('actorGuid'),
                                  direction=retreat_direction)
                    break
        self.ground_away_plan = active
        if active:
            held = (16 if active['direction']>0 else 32) | 2048
            self.trace.update(held=held,ground_away_plan=active.copy())
        return held

# Experimental suffix for the preserved df6bc016 controller.
# Diagnostic forecasts exclude stomps and unknown actor motion.
from nsmb_mvl_player_motion_forecast import forecast_player as ceiling_brake_forecast


def choose_ceiling_contact_persistent_brake(decision, player, held, previous_held, navigator):
    obs=decision['observation']; me=obs['players'][player]
    raw=decision['runtimePlayers'][player]; other=obs['players'][player^1]
    x=me['pos']['x']/4096; depth=-me['pos']['y']/4096
    vx=me['vel']['x']/4096; vy=me['vel']['y']/4096
    if me['contact']['tileGround'] or not -4<=vy<=0 or not .25<=abs(vx)<=3:
        return None
    if other['found'] and not other['dead'] and abs(delta(other['pos'],me['pos'])[0])<96:
        return None
    enemies=[]
    for e in obs['entities']:
        if e.get('category') not in ('enemy_goomba','enemy_koopa','player_fireball'):
            continue
        dx,dy=delta(e['pos'],me['pos'])
        if abs(dx)>128 or abs(dy)>96:
            continue
        if (e.get('category')!='enemy_goomba' or
                e.get('goombaBehaviorFunctionRaw')!=0x020E1538 or
                abs(e['vel']['x'])!=2048 or e['vel']['y']!=0):
            return None
        points,reason=forecast_ground_goomba(e['pos']['x']/4096,
            -e['pos']['y']/4096,e['vel']['x']/4096,navigator.occupied,30)
        if reason!='horizon' or len(points)!=30:
            return None
        enemies.append(dict(guid=e['actorGuid'],dx=dx,dy=dy,points=points))
    approaching=[e for e in enemies if 12<abs(e['dx'])<64 and
                 -48<=e['dy']<=-4 and e['dx']*vx>0]
    if not approaching:
        return None
    enemy=min(approaching,key=lambda e:abs(e['dx']))
    braking=(32 if enemy['dx']>0 else 16)|2048
    initial=dict(x=x,depth=depth,vx=vx,vy=vy,grounded=False,
                 previous_input=previous_held,height=16,facing=raw['facing'])
    delay=int(decision['time'].get('inputDelay',2))
    def near(path):
        return any(abs((p['x']-e['points'][i]['x']+512)%1024-512)<16 and
                   e['points'][i]['depth']-20<=p['depth']<=e['points'][i]['depth']+16
                   for i,p in enumerate(path) for e in enemies)
    nominal=[]; alternatives=[]
    for turn in (False,True):
        a,end=ceiling_brake_forecast(**initial,
            inputs=[previous_held]*delay+[held]*(30-delay),
            occupied=navigator.occupied,counter_landing_turn=turn)
        nominal.append(end=='horizon' and len(a)==30 and near(a))
        b,end=ceiling_brake_forecast(**initial,
            inputs=[previous_held]*delay+[braking]*(30-delay),
            occupied=navigator.occupied,counter_landing_turn=turn)
        if (end!='horizon' or len(b)!=30 or near(b) or
                not b[-1]['grounded'] or any(p['depth']>288 for p in b)):
            return None
        alternatives.append(b)
    if not any(nominal):
        return None
    return dict(held=braking,paths=alternatives,enemies=enemies)


_CeilingNpcBrakeBase=RoutedHumanRule
class RoutedHumanRule(_CeilingNpcBrakeBase):
    def reset(self):
        super().reset()
        self.ceiling_npc_brake=None
        self.last_brake_ceiling=-1000000

    def act(self,decision,frame,previous_held=0):
        held=super().act(decision,frame,previous_held)
        me=decision['observation']['players'][self.player]
        raw=decision['runtimePlayers'][self.player]
        if me.get('contact',{}).get('ceiling'):
            self.last_brake_ceiling=frame
        eligible=(raw['currentPowerupRaw']==0 and not me['dead'] and
                  not raw.get('damageStateRaw',1) and not raw.get('updateLockedRaw',1) and
                  raw['physicsFlagRaw'] in (0,2,128,130,2050) and
                  raw['actionFlagRaw'] in (0,0x100000))
        active=self.ceiling_npc_brake
        if active:
            elapsed=frame-active['start']
            valid=eligible and 0<elapsed<30
            if valid:
                x=me['pos']['x']/4096; depth=-me['pos']['y']/4096
                valid=any(abs((p[elapsed-1]['x']-x+512)%1024-512)<=4 and
                          abs(p[elapsed-1]['depth']-depth)<=4 for p in active['paths'])
                entities=decision['observation']['entities']
                for enemy in active['enemies']:
                    matches=[e for e in entities if e.get('actorGuid')==enemy['guid']
                             and e.get('category')=='enemy_goomba']
                    expected=enemy['points'][elapsed-1]
                    if len(matches)!=1:
                        valid=False; break
                    actual=matches[0]
                    if (actual.get('goombaBehaviorFunctionRaw')!=0x020E1538 or
                            abs((actual['pos']['x']/4096-expected['x']+512)%1024-512)>2 or
                            abs(-actual['pos']['y']/4096-expected['depth'])>2):
                        valid=False; break
                known={e['guid'] for e in active['enemies']}
                if any(e.get('category') in ('enemy_goomba','enemy_koopa','player_fireball')
                       and e.get('actorGuid') not in known and
                       abs(delta(e['pos'],me['pos'])[0])<96 and
                       abs(delta(e['pos'],me['pos'])[1])<64 for e in entities):
                    valid=False
            if not valid:
                active=None
        if active is None and eligible and 0<=frame-self.last_brake_ceiling<=18:
            proposal=choose_ceiling_contact_persistent_brake(decision,self.player,held,previous_held,self.navigator)
            if proposal:
                active=dict(start=frame,**proposal)
        self.ceiling_npc_brake=active
        if active:
            held=active['held']
            self.trace.update(held=held,ceiling_npc_brake=dict(start=active['start'],held=held))
        return held

_DefeatedGoombaBase = RoutedHumanRule
class RoutedHumanRule(_DefeatedGoombaBase):
    def act(self, decision, frame, previous_held=0):
        obs=decision['observation']
        removed=[e for e in obs['entities'] if e.get('category')=='enemy_goomba'
                 and e.get('entityUpdateStateFound')==1 and e.get('entityUpdateStateRaw')==2]
        if removed:
            decision=dict(decision,observation=dict(obs,entities=[e for e in obs['entities'] if e not in removed]))
        held=super().act(decision,frame,previous_held)
        if removed:self.trace['ignored_defeated_goombas']=[e.get('actorGuid') for e in removed]
        return held

_GroundCooldownBase=RoutedHumanRule
class RoutedHumanRule(_GroundCooldownBase):
    def reset(self):
        super().reset()
        self.ground_cooldown_plan=None
    def act(self,decision,frame,previous_held=0):
        held=super().act(decision,frame,previous_held)
        obs=decision['observation'];me=obs['players'][self.player];raw=decision['runtimePlayers'][self.player]
        x=me['pos']['x']/4096;depth=-me['pos']['y']/4096
        eligible=(not me['dead'] and raw['currentPowerupRaw']==0 and me.get('contact',{}).get('tileGround',False)
            and not raw.get('damageStateRaw',1) and not raw.get('updateLockedRaw',1)
            and raw['physicsFlagRaw'] in (0,2,128,130,2050) and raw['actionFlagRaw'] in (0,0x100000))
        other=obs['players'][self.player^1]
        if other['found'] and not other['dead'] and abs(delta(other['pos'],me['pos'])[0])<96:
            eligible=False
        hazards=[]
        for e in obs['entities']:
            if e.get('category') not in ('enemy_goomba','enemy_koopa','player_fireball'):continue
            if abs(delta(e['pos'],me['pos'])[0])>128 or abs(delta(e['pos'],me['pos'])[1])>96:continue
            if e.get('category')=='enemy_goomba' and e.get('entityUpdateStateFound')==1 and e.get('entityUpdateStateRaw')==2:continue
            if e.get('category')!='enemy_koopa':eligible=False;break
            points,end=forecast_ground_koopa(e,self.navigator.occupied,24)
            if end!='horizon':eligible=False;break
            hazards.append(dict(guid=e['actorGuid'],points=points))
        active=self.ground_cooldown_plan
        if active:
            elapsed=frame-active['start']
            valid=eligible and 0<elapsed<24
            if valid:
                p=active['points'][elapsed-1]
                valid=abs(wrap(x,p['x']))<=2 and abs(depth-p['depth'])<=2
                known={h['guid'] for h in active['hazards']}
                if known!={h['guid'] for h in hazards}:valid=False
                for h in active['hazards']:
                    a=next((e for e in obs['entities'] if e.get('actorGuid')==h['guid']),None)
                    p=h['points'][elapsed-1]
                    if a is None or abs(wrap(a['pos']['x']/4096,p['x']))>2 or abs(-a['pos']['y']/4096-p['depth'])>2:valid=False
            if not valid:active=None
        if active is None and eligible and hazards and frame<self.next_jump and not held&3 and not previous_held&3:
            delay=int(decision['time'].get('inputDelay',2))
            initial=dict(x=x,depth=depth,vx=me['vel']['x']/4096,vy=me['vel']['y']/4096,grounded=True,previous_input=previous_held,height=16,facing=raw['facing'])
            def predict(buttons):
                return ceiling_brake_forecast(**initial,inputs=[previous_held]*delay+[buttons]*(24-delay),occupied=self.navigator.occupied)
            def near(points):
                return any(abs(wrap(p['x'],h['points'][i]['x']))<16 and abs(p['depth']-h['points'][i]['depth'])<20 for i,p in enumerate(points) for h in hazards)
            nominal,end=predict(held)
            if near(nominal[:18]):
                for buttons in (2064,2080):
                    points,end=predict(buttons)
                    if end=='horizon' and len(points)==24 and not near(points) and all(p['grounded'] and self.navigator.occupied(p['x'],p['depth']+1) for p in points):
                        active=dict(start=frame,held=buttons,points=points,hazards=hazards);break
        self.ground_cooldown_plan=active
        if active:
            held=active['held'];self.trace.update(held=held,ground_cooldown_brake=dict(start=active['start'],held=held))
        return held

# Candidate only: intervene if the proposed descent cannot reach a wall with margin.
from nsmb_mvl_player_motion_forecast import forecast_player as _shaft_contact_forecast

_ShaftContactBase=RoutedHumanRule
class RoutedHumanRule(_ShaftContactBase):
    def reset(self):
        super().reset()
        self.shaft_contact_plan=None
        self.shaft_contact_previous=None
        self.shaft_contact_navigator=GrassNavigator()

    def act(self,decision,frame,previous_held=0):
        held=super().act(decision,frame,previous_held)
        me=decision['observation']['players'][self.player]
        raw=decision['runtimePlayers'][self.player]
        other=decision['observation']['players'][self.player^1]
        eligible=(not me['dead'] and self.trace.get('recovery') and self.trace.get('in_shaft')
            and not me.get('contact',{}).get('tileGround',1) and me['vel']['y']<0
            and raw['currentPowerupRaw'] in (0,1,2)
            and raw.get('behaviorFuncRaw')==0x021135B8
            and raw['actionFlagRaw']==0x100000 and raw['physicsFlagRaw'] in (0,2,128,130,2050)
            and not raw.get('damageStateRaw',1) and not raw.get('updateLockedRaw',1)
            and frame>=self.wall_depart_until)
        if other['found'] and not other['dead'] and abs(delta(other['pos'],me['pos'])[0])<48 and abs(delta(other['pos'],me['pos'])[1])<64:
            eligible=False
        for e in decision['observation']['entities']:
            if e.get('category') not in ('enemy_goomba','enemy_koopa','player_fireball'):continue
            if e.get('category')=='player_fireball' and e.get('ownerVerified') and e.get('owner')==self.player:continue
            if e.get('category')=='enemy_goomba' and e.get('entityUpdateStateFound') and e.get('entityUpdateStateRaw')==2:continue
            if abs(delta(e['pos'],me['pos'])[0])<48 and abs(delta(e['pos'],me['pos'])[1])<64:eligible=False
        self.shaft_contact_navigator.update(me)
        signature=frozenset(k for k,m in self.shaft_contact_navigator.tiles.items() if solid(m))
        x,depth=me['pos']['x']/4096,-me['pos']['y']/4096
        if eligible:
            # While the body still overlaps the rim height, ordinary lateral
            # motion may land on the upper ledge. Reserve this wall plan for
            # a descent fully below both bordering ledges.
            wall_tops=[]
            for side in (-1,1):
                wall_x=next((x+side*offset for offset in range(8,65,8)
                             if self.shaft_contact_navigator.occupied(x+side*offset,depth-4)),None)
                if wall_x is None:
                    eligible=False
                    break
                top=math.floor((depth-4)/16)*16
                while top>0 and self.shaft_contact_navigator.occupied(wall_x,top-1):
                    top-=16
                wall_tops.append(top)
            height=16 if raw['currentPowerupRaw']==0 else 27
            if not wall_tops or depth-height < max(wall_tops):
                eligible=False
        active=self.shaft_contact_plan
        if active:
            elapsed=frame-active['start']
            reason=None
            if not eligible:reason='state_or_contact'
            elif elapsed>=active['duration']:reason='contact_deadline'
            elif signature!=active['terrain_signature']:reason='terrain_changed'
            elif elapsed>0:
                p=active['points'][elapsed-1]
                if abs(wrap(x,p['x']))>1 or abs(depth-p['depth'])>1:reason='motion_deviation'
            if reason:
                self.trace['shaft_contact_cancel']=reason
                active=None
        direction=int(bool(held&16))-int(bool(held&32))
        previous=int(bool(previous_held&16))-int(bool(previous_held&32))
        if active is None and eligible and direction and not held&3:
            delay=int(decision['time'].get('inputDelay',2))
            if 0<=delay<=6:
                def predict(button):
                    points,reason=_shaft_contact_forecast(x=x,depth=depth,
                        vx=me['vel']['x']/4096,vy=me['vel']['y']/4096,grounded=False,
                        previous_input=previous_held,inputs=[previous_held]*delay+[button]*(36-delay),
                        occupied=self.shaft_contact_navigator.occupied,
                        height=16 if raw['currentPowerupRaw']==0 else 27,facing=raw['facing'])
                    margin=(reason=='wall' and bool(points)
                            and points[-1]['depth']+4*(delay+2)<336)
                    return points,reason,margin
                nominal,nominal_reason,nominal_margin=predict(held)
                if not nominal_margin:
                    choices=[]
                    for button in (16,32):
                        points,reason,margin=predict(button)
                        if margin:choices.append(dict(held=button,points=points,duration=len(points)))
                    if not choices:
                        def late_margin(points, reason, button):
                            if reason != 'wall':
                                return False
                            bound = _late_wall_contact_depth_bound(points, button,
                                previous_held, delay, self.shaft_contact_navigator.occupied,
                                16 if raw['currentPowerupRaw'] == 0 else 27)
                            return bound is not None and bound['peak_depth'] < 352
                        # Preserve all already-feasible early-contact routes.
                        # As in b8c46, a feasible nominal late route needs no override.
                        if not late_margin(nominal, nominal_reason, held):
                            for button in (16, 32):
                                points, reason, _ = predict(button)
                                if late_margin(points, reason, button):
                                    choices.append(dict(held=button, points=points, duration=len(points)))
                    if choices:
                        best=min(choices,key=lambda p:(p['duration'],p['held']!=(held&48)))
                        active=dict(best,start=frame,terrain_signature=signature)
                        self.trace['shaft_missed_contact_nominal']=dict(
                            held=held,reason=nominal_reason,frames=len(nominal))
        self.shaft_contact_previous=dict(frame=frame,eligible=bool(eligible))
        self.shaft_contact_plan=active
        if active:
            held=(held&~(48|2048))|active['held']
            self.trace.update(held=held,shaft_contact_plan={k:active[k] for k in ('start','held','duration')})
        return held

# Candidate: use the measured ceiling contact even when the bumped tile is absent.
from nsmb_mvl_player_motion_forecast import forecast_player as _observed_ceiling_forecast
def _choose_observed_ceiling_brake(initial, proposed, previous, delay, occupied,
                             paths, horizon=36):
    if (initial['grounded'] or initial['vy'] > 0 or not paths
            or not 0 <= delay <= 6):
        return None

    def predict(held, counter):
        return _observed_ceiling_forecast(**initial, previous_input=previous,
            inputs=[previous]*delay+[held]*(horizon-delay), occupied=occupied,
            counter_landing_turn=counter)[0]

    def danger(points):
        return any(i >= len(path) or
                   (abs((p['x']-path[i]['x']+512)%1024-512) < 16
                    and path[i]['depth']-20 < p['depth']
                    < path[i]['depth']+initial['height'])
                   for i, p in enumerate(points) for path in paths)

    nominal = predict(proposed, False)
    if not danger(nominal[:24]):
        return None
    candidates = []
    for held in (2064, 2080, 16, 32, 0):
        branches = [predict(held, counter) for counter in (False, True)]
        if any(len(points) != horizon or danger(points)
               or any(p['depth'] > 288 for p in points)
               or not all(p['grounded'] and occupied(p['x'], p['depth']+1)
                          for p in points[-6:]) for points in branches):
            continue
        candidates.append(dict(held=held, duration=(horizon-delay)//6*6,
                               points=branches[0], conservative_points=branches[1]))
    return min(candidates, key=lambda p: (not bool(p['held'] & 2048),
                (p['held'] ^ proposed).bit_count())) if candidates else None

_ObservedCeilingKoopaBase=RoutedHumanRule
class RoutedHumanRule(_ObservedCeilingKoopaBase):
    def reset(self):
        super().reset()
        self.observed_ceiling_koopa_plan=None
        self.observed_ceiling_navigator=GrassNavigator()

    def act(self,decision,frame,previous_held=0):
        held=super().act(decision,frame,previous_held)
        obs=decision['observation'];me=obs['players'][self.player];raw=decision['runtimePlayers'][self.player]
        active=self.observed_ceiling_koopa_plan
        eligible=(not me['dead'] and raw['currentPowerupRaw']==0
            and not raw.get('damageStateRaw',1) and not raw.get('updateLockedRaw',1)
            and raw.get('physicsFlagRaw') in (0,2,128,130,2050)
            and raw.get('actionFlagRaw') in (0,0x100000) and not held&1024)
        if not eligible or (active is None and not me.get('contact',{}).get('ceiling')):
            self.observed_ceiling_koopa_plan=None
            return held
        self.observed_ceiling_navigator.update(me)
        enemies=[]
        for e in obs['entities']:
            if e.get('category') not in ('enemy_goomba','enemy_koopa','player_fireball'):continue
            hx,hy=delta(e['pos'],me['pos'])
            if abs(hx)>128 or abs(hy)>96:continue
            if e.get('category')=='enemy_goomba' and e.get('entityUpdateStateFound')==1 and e.get('entityUpdateStateRaw')==2:continue
            if (e.get('category')!='enemy_koopa' or e.get('koopaShellModeRaw')!=0
                    or e.get('entityUpdateStateFound')!=1 or e.get('entityUpdateStateRaw')!=0):eligible=False;break
            points,reason=forecast_ground_koopa(e,self.observed_ceiling_navigator.occupied,36)
            if reason!='horizon':eligible=False;break
            enemies.append(dict(guid=e['actorGuid'],points=points))
        other=obs['players'][self.player^1];other_raw=decision['runtimePlayers'][self.player^1]
        def opponent_clear(paths,elapsed=0):
            if not other['found'] or other['dead']:return True
            if other_raw.get('currentPowerupRaw') not in (0,1,2) or other_raw.get('physicsFlagRaw',0)&32:return False
            ox=other['pos']['x']/4096;ovx=other['vel']['x']/4096
            if abs(ovx)>3:return False
            # Current-velocity diagnostic prediction, rechecked at every tick;
            # the opponent's future choices are not known or guaranteed.
            return all(abs(wrap(p['x'],ox+ovx*(i+1)))>=32
                       for path in paths for i,p in enumerate(path[elapsed:]))
        if active:
            elapsed=frame-active['start']
            valid=eligible and 0<elapsed<active['duration']
            if valid:
                x=me['pos']['x']/4096;depth=-me['pos']['y']/4096
                valid=any(abs(wrap(path[elapsed-1]['x'],x))<=2 and abs(path[elapsed-1]['depth']-depth)<=2 for path in active['paths'])
                current={e['actorGuid']:e for e in obs['entities'] if e.get('category')=='enemy_koopa'}
                if {e['guid'] for e in enemies}!={e['guid'] for e in active['enemies']}:valid=False
                for e in active['enemies']:
                    a=current.get(e['guid']);p=e['points'][elapsed-1]
                    if a is None or abs(wrap(a['pos']['x']/4096,p['x']))>2 or abs(-a['pos']['y']/4096-p['depth'])>2:valid=False
                valid=valid and opponent_clear(active['paths'],elapsed)
            if not valid:active=None
        if active is None and eligible and enemies and me.get('contact',{}).get('ceiling') and not me.get('contact',{}).get('tileGround'):
            initial=dict(x=me['pos']['x']/4096,depth=-me['pos']['y']/4096,
                vx=me['vel']['x']/4096,vy=me['vel']['y']/4096,grounded=False,height=16,facing=raw['facing'])
            plan=_choose_observed_ceiling_brake(initial,held,previous_held,int(decision['time'].get('inputDelay',2)),self.observed_ceiling_navigator.occupied,[e['points'] for e in enemies])
            if plan:
                paths=[plan['points'],plan['conservative_points']]
                if opponent_clear(paths):active=dict(start=frame,held=plan['held'],duration=plan['duration'],paths=paths,enemies=enemies)
        self.observed_ceiling_koopa_plan=active
        if active:
            held=active['held'];self.trace.update(held=held,observed_ceiling_koopa_plan={k:active[k] for k in ('start','held','duration')})
        return held

# Diagnostic: stop pursuing a sinking loose star only during a threatened fall.
# This does not change target selection on ordinary ground or above the floor.
_UnreachablePitBase = RoutedHumanRule


class RoutedHumanRule(_UnreachablePitBase):
    def reset(self):
        super().reset()
        self.unreachable_pit_active = False
        self.unreachable_pit_previous_input = 0
        self.unreachable_pit_navigator = GrassNavigator()

    def act(self, decision, frame, previous_held=0):
        self.unreachable_pit_previous_input = previous_held
        return super().act(decision, frame, previous_held)

    def targets(self, decision):
        choices = super().targets(decision)
        obs = decision['observation']
        me = obs['players'][self.player]
        raw = decision['runtimePlayers'][self.player]
        x, depth = me['pos']['x']/4096, -me['pos']['y']/4096
        allowed = (obs.get('stage', {}).get('id') == 0
                   and obs.get('stage', {}).get('vsMode') == 1
                   and not me['dead'] and depth > 272
                   and not me.get('contact', {}).get('tileGround', 1)
                   and raw['currentPowerupRaw'] in (0, 1, 2)
                   and not raw.get('damageStateRaw', 1)
                   and not raw.get('updateLockedRaw', 1))
        if not allowed:
            self.unreachable_pit_active = False
            return choices
        excluded = [e for e in obs['entities']
                    if e.get('category') == 'dropped_star_item'
                    and e['pos']['y'] < -360*4096
                    and e.get('vel', {}).get('y', 0) < 0
                    and abs(delta(e['pos'], me['pos'])[0]) < 48]
        if not excluded:
            self.unreachable_pit_active = False
            return choices
        if not self.unreachable_pit_active:
            if (me['vel']['y'] >= 0 or raw['actionFlagRaw'] != 0x100000
                    or raw.get('behaviorFuncRaw') != 0x021135B8
                    or raw['physicsFlagRaw'] not in (0, 2, 128, 130, 2050)):
                return choices
            other = obs['players'][self.player ^ 1]
            if (other['found'] and not other['dead']
                    and abs(delta(other['pos'], me['pos'])[0]) < 48
                    and abs(delta(other['pos'], me['pos'])[1]) < 64):
                return choices
            for e in obs['entities']:
                if e.get('category') not in ('enemy_goomba', 'enemy_koopa', 'player_fireball'):
                    continue
                if e.get('category') == 'player_fireball' and e.get('ownerVerified') and e.get('owner') == self.player:
                    continue
                if e.get('category') == 'enemy_goomba' and e.get('entityUpdateStateFound') and e.get('entityUpdateStateRaw') == 2:
                    continue
                if abs(delta(e['pos'], me['pos'])[0]) < 48 and abs(delta(e['pos'], me['pos'])[1]) < 64:
                    return choices
            self.unreachable_pit_navigator.update(me)
            delay = int(decision['time'].get('inputDelay', 2))
            if not 0 <= delay <= 6:
                return choices
            trials = []
            for held in (16, 32):
                points, reason = _shaft_contact_forecast(
                    x=x, depth=depth, vx=me['vel']['x']/4096,
                    vy=me['vel']['y']/4096, grounded=False,
                    previous_input=self.unreachable_pit_previous_input,
                    inputs=[self.unreachable_pit_previous_input]*delay+[held]*(36-delay),
                    occupied=self.unreachable_pit_navigator.occupied,
                    height=16 if raw['currentPowerupRaw'] == 0 else 27,
                    facing=raw['facing'])
                if (reason == 'wall' and points
                        and points[-1]['depth']+4*(delay+2) < 336):
                    trials.append(dict(held=held, frames=len(points), depth=points[-1]['depth']))
            if not trials:
                return choices
            self.unreachable_pit_active = True
            self.trace['unreachable_pit_wall_candidates'] = trials
        positions = {(e['pos']['x'], e['pos']['y']) for e in excluded}
        self.trace['unreachable_pit_recovery'] = [e.get('actorGuid') for e in excluded]
        return [c for c in choices if not (c[1] == 'dropped_star' and (c[2]['x'], c[2]['y']) in positions)]

# Diagnostic: an upcoming wall collision is not a slide when input points away.
_ImminentWallEntryBase = RoutedHumanRule


class RoutedHumanRule(_ImminentWallEntryBase):
    def reset(self):
        super().reset()
        self.imminent_wall_entry = None
        self.imminent_wall_navigator = GrassNavigator()

    def act(self, decision, frame, previous_held=0):
        held = super().act(decision, frame, previous_held)
        obs = decision['observation']
        me = obs['players'][self.player]
        raw = decision['runtimePlayers'][self.player]
        x, depth = me['pos']['x']/4096, -me['pos']['y']/4096
        allowed = (obs.get('stage', {}).get('id') == 0
                   and obs.get('stage', {}).get('vsMode') == 1
                   and not me['dead'] and raw['currentPowerupRaw'] in (0, 1, 2)
                   and not raw.get('damageStateRaw', 1)
                   and not raw.get('updateLockedRaw', 1)
                   and not me.get('contact', {}).get('tileGround', 1))
        active = self.imminent_wall_entry
        if active and (not allowed or raw['actionFlagRaw'] & 32
                       or frame - active['start'] >= 24 or me['vel']['y'] > 0):
            self.trace['imminent_wall_entry_end'] = frame
            active = None
        if active is None and allowed and me['vel']['y'] < 0:
            direction = int(bool(held & 16)) - int(bool(held & 32))
            eligible = (direction and raw['actionFlagRaw'] == 0x100000
                        and raw.get('behaviorFuncRaw') == 0x021135B8
                        and raw['physicsFlagRaw'] in (0, 2, 128, 130, 2050)
                        and self.trace.get('target') in ('carrier', 'wait', 'patrol')
                        and frame >= self.wall_depart_until)
            other = obs['players'][self.player ^ 1]
            if (other['found'] and not other['dead']
                    and abs(delta(other['pos'], me['pos'])[0]) < 48
                    and abs(delta(other['pos'], me['pos'])[1]) < 64):
                eligible = False
            for e in obs['entities']:
                if e.get('category') not in ('enemy_goomba', 'enemy_koopa', 'player_fireball'):
                    continue
                if e.get('category') == 'player_fireball' and e.get('ownerVerified') and e.get('owner') == self.player:
                    continue
                if e.get('category') == 'enemy_goomba' and e.get('entityUpdateStateFound') and e.get('entityUpdateStateRaw') == 2:
                    continue
                if abs(delta(e['pos'], me['pos'])[0]) < 48 and abs(delta(e['pos'], me['pos'])[1]) < 64:
                    eligible = False
            if eligible:
                self.imminent_wall_navigator.update(me)
                occupied = self.imminent_wall_navigator.occupied
                delay = int(decision['time'].get('inputDelay', 2))
                if 0 <= delay <= 6:
                    points, reason = _shaft_contact_forecast(
                        x=x, depth=depth, vx=me['vel']['x']/4096,
                        vy=me['vel']['y']/4096, grounded=False,
                        previous_input=previous_held,
                        inputs=[previous_held]*delay+[held]*6,
                        occupied=occupied,
                        height=16 if raw['currentPowerupRaw'] == 0 else 27,
                        facing=raw['facing'])
                    if reason == 'wall' and points and len(points) <= 6:
                        end = points[-1]
                        side = 1 if end['vx'] > 0 else -1
                        left = next((n for n in range(8, 65, 8)
                                     if occupied(end['x']-n, end['depth']-4)), None)
                        right = next((n for n in range(8, 65, 8)
                                      if occupied(end['x']+n, end['depth']-4)), None)
                        shaft = (left is not None and right is not None and left+right <= 48)
                        no_floor = not any(occupied(end['x']+dx, end['depth']+dy)
                                           for dx in (-5, 0, 4) for dy in (1, 8, 16))
                        # One airborne acceleration step is at most 0.0703125.
                        # Require enough speed to preserve the collision-side sign.
                        if (abs(end['vx']) > .0703125 and side != direction and shaft and no_floor
                                and end['depth']+4*(delay+2) < 336):
                            active = dict(start=frame, side=side)
                            self.trace['imminent_wall_entry_prediction'] = dict(
                                nominal_held=held, side=side, frames=len(points),
                                x=end['x'], depth=end['depth'])
        if active:
            side = active['side']
            contact = me.get('contact', {}).get('wallRight' if side > 0 else 'wallLeft')
            # A measured contact can precede the slide state. Supply a fresh B
            # edge while pressing into it; verify this transition in native runs.
            jump = bool(contact) and not bool(previous_held & 2)
            held = (16 if side > 0 else 32) | (2 if jump else 0)
            self.trace.update(held=held, imminent_wall_entry=dict(active, contact=bool(contact)))
        self.imminent_wall_entry = active
        return held

# Diagnostic: a ground jump request can arrive after a narrow support is lost.
_MissedTakeoffBase = RoutedHumanRule


class RoutedHumanRule(_MissedTakeoffBase):
    def reset(self):
        super().reset()
        self.missed_takeoff_retry = None
        self.missed_takeoff_navigator = GrassNavigator()

    def act(self, decision, frame, previous_held=0):
        held = super().act(decision, frame, previous_held)
        obs = decision['observation']
        me = obs['players'][self.player]
        raw = decision['runtimePlayers'][self.player]
        grounded = bool(me.get('contact', {}).get('tileGround'))
        delay = int(decision.get('time', {}).get('inputDelay', -1))
        eligible = (obs.get('stage', {}).get('id') == 0
                    and obs.get('stage', {}).get('vsMode') == 1
                    and not me['dead'] and raw['currentPowerupRaw'] == 0
                    and not raw.get('damageStateRaw', 1)
                    and not raw.get('updateLockedRaw', 1)
                    and raw['actionFlagRaw'] in (0, 0x100000)
                    and raw['physicsFlagRaw'] in (0, 2, 128, 130, 2050)
                    and 0 <= delay <= 6)
        other = obs['players'][self.player ^ 1]
        if (other['found'] and not other['dead']
                and abs(delta(other['pos'], me['pos'])[0]) < 96
                and abs(delta(other['pos'], me['pos'])[1]) < 64):
            eligible = False
        active = self.missed_takeoff_retry
        attempt = grounded and bool(held & 2) and not bool(previous_held & 2)
        if not eligible or (active and (frame-active['start'] >= 36 or me['vel']['y'] > 0)):
            self.missed_takeoff_retry = None
            return held
        if active is None and not attempt:
            return held
        self.missed_takeoff_navigator.update(me)
        occupied = self.missed_takeoff_navigator.occupied
        hazards = []
        for e in obs['entities']:
            if e.get('category') not in ('enemy_goomba', 'enemy_koopa', 'player_fireball'):
                continue
            dx, dy = delta(e['pos'], me['pos'])
            if abs(dx) >= 128 or abs(dy) >= 96:
                continue
            if e.get('category') == 'enemy_goomba' and e.get('entityUpdateStateFound') and e.get('entityUpdateStateRaw') == 2:
                continue
            if (e.get('category') != 'enemy_koopa'
                    or e.get('koopaBehaviorFunctionRaw') != 0x020DFC58
                    or e.get('koopaShellModeRaw') != 0):
                self.missed_takeoff_retry = None
                return held
            points, end = forecast_ground_koopa(e, occupied, 24)
            if end != 'horizon' or len(points) != 24:
                self.missed_takeoff_retry = None
                return held
            hazards.append(dict(guid=e.get('actorGuid'), points=points))
        if not hazards:
            self.missed_takeoff_retry = None
            return held
        initial = dict(x=me['pos']['x']/4096, depth=-me['pos']['y']/4096,
                       vx=me['vel']['x']/4096, vy=me['vel']['y']/4096,
                       grounded=grounded, previous_input=previous_held,
                       occupied=occupied, height=16, facing=raw['facing'])

        def predict(buttons):
            return _shaft_contact_forecast(
                **initial, inputs=[previous_held]*delay+[buttons]*(24-delay))

        hitbox = me.get('hitbox', {})
        if not hitbox.get('found') or hitbox.get('fixedPointShift') != 12:
            self.missed_takeoff_retry = None
            return held
        cx, cy = hitbox['centerOffsetX']/4096, hitbox['centerOffsetY']/4096
        hw, hh = hitbox['halfWidth']/4096, hitbox['halfHeight']/4096
        # ROM ordinary walking Koopa: center (0,8), half-size (8,8).
        # Preserve a small horizontal and larger vertical forecast allowance;
        # this is a bounded estimate, not a proof of future contact avoidance.
        def close(points):
            return any(abs(wrap(p['x']+cx, h['points'][i]['x'])) < hw+8+.25
                       and abs((p['depth']-cy)-(h['points'][i]['depth']-8)) < hh+8+1
                       for i, p in enumerate(points) for h in hazards)

        if active:
            elapsed = frame-active['verified_at']
            if not 0 < elapsed <= len(active['points']):
                self.missed_takeoff_retry = None
                return held
            expected = active['points'][elapsed-1]
            valid = (abs(wrap(initial['x'], expected['x'])) <= .25
                     and abs(initial['depth']-expected['depth']) <= .75)
            for hazard in active['hazards']:
                actual = next((e for e in obs['entities'] if e.get('actorGuid') == hazard['guid']), None)
                expected = hazard['points'][elapsed-1]
                if (actual is None or abs(wrap(actual['pos']['x']/4096, expected['x'])) > .25
                        or abs(-actual['pos']['y']/4096-expected['depth']) > .25):
                    valid = False
            if not valid:
                self.missed_takeoff_retry = None
                return held

        if active is None:
            points, end = predict(held)
            lost_support = any(p.get('contact') == 'edge' for p in points[:delay+1])
            launch_missed = len(points) > delay and not any(p['launch'] for p in points[:delay+1])
            if not (end == 'horizon' and lost_support and launch_missed and close(points)):
                return held
            active = dict(start=frame, verified_at=frame, points=points, hazards=hazards)
            self.trace['missed_takeoff_prediction'] = dict(
                delay=delay, initial_held=held,
                edge_after=next(i+1 for i, p in enumerate(points) if p.get('contact') == 'edge'))
        held &= ~2
        # The canceled request must not consume the ground-jump cooldown.
        self.jump_until = frame
        self.next_jump = frame
        if not previous_held & 2:
            jump_points, jump_end = predict(held | 2)
            nominal, _ = predict(held)
            timely_launch = (len(jump_points) > delay and jump_points[delay]['launch'])
            safe_path = (jump_end == 'horizon' and len(jump_points) == 24
                         and not any(p.get('contact') == 'ceiling' for p in jump_points)
                         and not close(jump_points))
            if timely_launch and safe_path and close(nominal[:18]):
                held |= 2
                self.jump_until = frame+30
                self.next_jump = frame+42
                self.trace['missed_takeoff_retry_launch'] = dict(start=active['start'], frame=frame)
                active = None
        if active:
            points, _ = predict(held)
            active.update(verified_at=frame, points=points, hazards=hazards)
        self.missed_takeoff_retry = active
        self.trace.update(held=held, missed_takeoff_retry=None if active is None else dict(start=active['start']))
        return held

def _late_wall_contact_depth_bound(points, button, previous_held, delay, occupied, height):
    """Diagnostic extra margin for the six-frame rule-control path.

    Keep the original early-contact criterion. This estimates only additional
    late contacts, with an observed-model slide on the next frame and a fresh B
    at the next six-frame decision. It does not predict landing after the kick.
    """
    if not points or previous_held & 3 or button & 3 or not 0 <= delay <= 6:
        return None
    side = int(bool(button & 16))-int(bool(button & 32))
    if not side:
        return None
    sensor = -9 if side < 0 else 8
    for index, point in enumerate(points, 1):
        if point['grounded'] or point['vy'] >= 0:
            return None
        touches = any(occupied(point['x']+sensor, point['depth']-h)
                      for h in (4, height-8))
        if not touches:
            continue
        if point['vx']*side <= .0703125:
            return None
        # Do not count a jump requested before slide entry as a successful edge.
        next_decision = ((index+1+5)//6)*6
        prepare_frame = next_decision+delay+1
        depth, velocity = point['depth'], point['vy']
        for _ in range(index+1, prepare_frame):
            velocity = min(-2.5, velocity+.34375) if velocity < -2.5 else max(-2.5, velocity-.34375)
            depth -= velocity
            # Require the same wall to extend through the predicted slide.
            if not any(occupied(point['x']+sensor, depth-h) for h in (4, height-8)):
                return None
        return dict(contact_after=index, next_decision_after=next_decision,
                    prepare_after=prepare_frame, peak_depth=depth)
    return None

"""Offline grass-stage prototype; ordinary walking/falling, no actor contacts.

Gravity .1875 and terminal speed 4 were measured previously in the stationary
NPC study. This adds support departure and floor interception for validation;
it is not yet an online planner dependency. Side turns stop the prediction.
"""
import math


def _short_jump_goomba_forecast(entity, occupied, frames=48):
    if (entity.get('objectId') != 83
            or entity.get('goombaBehaviorFunctionRaw') != 0x020E1538
            or entity.get('entityUpdateStateFound') != 1
            or entity.get('entityUpdateStateRaw') != 0):
        return [], 'unsupported_state'
    x, depth = entity['pos']['x']/4096, -entity['pos']['y']/4096
    vx, vy = entity['vel']['x']/4096, entity['vel']['y']/4096
    if abs(vx) != .5 or not -4 <= vy <= 0:
        return [], 'unsupported_velocity'
    points=[]
    for _ in range(frames):
        nx=x+vx
        if occupied(nx+(7 if vx>0 else -8),depth-8):
            return points, 'side_contact'
        supported = vy == 0 and depth % 16 == 0 and occupied(nx,depth+.01)
        if supported:
            ny=depth
        else:
            vy=max(-4,vy-.1875)
            ny=depth-vy
            # The original point support leaves the ledge before gravity;
            # descending feet crossing a tile top snap to that floor.
            for floor in range(math.ceil(depth/16)*16, math.floor(ny/16)*16+1,16):
                if occupied(nx,floor+.01):
                    ny=float(floor);vy=0;break
        x,depth=nx,ny
        points.append(dict(x=x,depth=depth,vx=vx,vy=vy))
    return points,'horizon'

def _upper_npc_release_forecast(x,depth,vx,vy,grounded,previous_input,inputs,occupied,height=16,
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
                # The head can enter an occupied cell horizontally while
                # already above its bottom plane; detect that corner too.
                head=next_depth-height
                if any(occupied(x+f,head) for f in (-2,0,1)):
                    next_depth=(math.floor(head/16)+1)*16+height
                    vy=0;contact='ceiling'
                for y in ([] if contact=='ceiling' else range(math.floor((depth-height)/16)*16,math.ceil((next_depth-height)/16)*16-1,-16)):
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

"""Shorten an ongoing small-player jump between predicted upper/lower NPCs.

The trigger's horizontal forecast ignores wall contacts; it is a threat
heuristic. The released terrain-aware arc must avoid known NPC hitbox margins and
land within 24 frames. Commit six frames, then reconsider. This is an
approximation; dynamic contacts and changes of enemy motion are not modeled.
"""
_UpperNpcReleaseBase=RoutedHumanRule


class RoutedHumanRule(_UpperNpcReleaseBase):
    def reset(self):
        super().reset()
        self.upper_npc_release=None

    def act(self,decision,frame,previous_held=0):
        held=super().act(decision,frame,previous_held)
        obs=decision['observation'];me=obs['players'][self.player];raw=decision['runtimePlayers'][self.player]
        eligible=(not me['dead'] and raw['currentPowerupRaw']==0
                  and not me.get('visual',{}).get('starInvincible')
                  and not raw.get('damageStateRaw',1) and not raw.get('updateLockedRaw',1)
                  and raw['physicsFlagRaw'] in (2,128,2050)
                  and raw['actionFlagRaw'] in (0,0x100000)
                  and 'tileGround' in me.get('contact',{}) and not me['contact']['tileGround'])
        active=self.upper_npc_release
        if active and (not eligible or frame-active['start']>=6 or frame<active['start']):active=None
        delay=int(decision.get('time',{}).get('inputDelay',-1))
        other=obs['players'][self.player^1]
        nearby_player=other['found'] and not other['dead'] and abs(delta(other['pos'],me['pos'])[0])<96 and abs(delta(other['pos'],me['pos'])[1])<96
        if (active is None and eligible and not nearby_player and held&3 and previous_held&3
                and me['vel']['y']/4096>1 and 0<=delay<=6
                and raw['physicsFlagRaw'] in (2,128)):
            self.navigator.update(me)
            paths=[];known=True;upper=False
            for e in obs['entities']:
                category=e.get('category')
                if category not in ('enemy_goomba','enemy_koopa','player_fireball'):continue
                ex,ey=delta(e['pos'],me['pos'])
                if abs(ex)>96 or abs(ey)>96:continue
                if category=='enemy_goomba' and e.get('entityUpdateStateFound')==1 and e.get('entityUpdateStateRaw')==2:continue
                if category=='enemy_goomba':points,end=_short_jump_goomba_forecast(e,self.navigator.occupied,24)
                elif category=='enemy_koopa' and e.get('koopaShellModeRaw')==0 and e.get('koopaBehaviorFunctionRaw')==0x020DFC58:
                    points,end=forecast_ground_koopa(e,self.navigator.occupied,24)
                else:known=False;break
                if end!='horizon' or len(points)!=24:known=False;break
                paths.append(dict(guid=e.get('actorGuid'),points=points))
                upper=upper or (category=='enemy_goomba' and 20<ey<72 and abs(ex)<48 and ex*me['vel']['x']>0)
            if known and upper:
                x=me['pos']['x']/4096;depth=-me['pos']['y']/4096
                short_depth=depth;long_depth=depth;vx=me['vel']['x']/4096
                short_vy=me['vel']['y']/4096;long_vy=short_vy
                safe=True;threat=None;minimum_gap=1000
                for i in range(24):
                    command=previous_held if i<delay else held
                    direction=int(bool(command&16))-int(bool(command&32))
                    vx=horizontal_run_step(vx,direction) if direction and command&2048 else horizontal_walk_step(vx,direction)
                    x+=vx
                    long_vy=vertical_step(long_vy,True);long_depth-=long_vy
                    short_vy=vertical_step(short_vy,i<delay);short_depth-=short_vy
                    for path in paths:
                        enemy=path['points'][i]
                        if (abs(wrap(x,enemy['x']))<16 and enemy['depth']-20<long_depth<enemy['depth']+16):
                            threat=i+1 if threat is None else threat
                short_points, short_end = _upper_npc_release_forecast(
                    me['pos']['x']/4096, -me['pos']['y']/4096,
                    me['vel']['x']/4096, me['vel']['y']/4096,
                    False, previous_held, [previous_held]*delay+[held & ~3]*(24-delay),
                    self.navigator.occupied, height=16, facing=raw['facing'])
                safe = (short_end == 'horizon' and len(short_points) == 24
                        and short_points[-1]['grounded']
                        and all(p['depth'] <= 288 for p in short_points))
                minimum_gap = 1000
                if safe:
                    for i, point in enumerate(short_points):
                        for path in paths:
                            enemy = path['points'][i]
                            # Conservative approximate AABB margins; horizontal
                            # clearance matters once a lower enemy is behind us.
                            gap = max(abs(wrap(point['x'],enemy['x']))-16,
                                      point['depth']-16-enemy['depth'],
                                      enemy['depth']-20-point['depth'])
                            minimum_gap = min(minimum_gap,gap)
                            if gap < (0 if i < delay else 4):
                                safe = False
                if safe and threat is not None and threat <= 12:
                    active=dict(start=frame,threat_after=threat,minimum_predicted_clearance=minimum_gap)
        if active:
            held &= ~3
            self.jump_until=frame
            self.trace.update(held=held,upper_npc_release=active.copy())
        self.upper_npc_release=active
        return held

def _skid_opponent_can_reach_plan(other, other_raw, points):
    """Conservative horizontal reach bound, not a future-input prediction.

    Ordinary opponent movement is bounded at 4px/f; a fire-form opponent can
    emit a 3.625px/f projectile immediately. Height and obstacles are ignored,
    so this may reject safe routes. A new shot cannot be treated as absent
    just because no current projectile is nearby.
    """
    if not other.get('found') or other.get('dead'):
        return False
    form=other_raw.get('currentPowerupRaw')
    if form not in (0,1,2):
        return True
    x=other['pos']['x']/4096
    speed=max(4.0,abs(other['vel']['x']/4096))
    if form==2:
        speed+=3.625
    return any(abs(wrap(p['x'],x))<=16+speed*i
               for i,p in enumerate(points,1))

"""Jump recovery only after native high-speed skid predicts loss of support."""
from nsmb_mvl_player_motion_forecast import forecast_player as _skid_jump_forecast
_SkidJumpBase=RoutedHumanRule

class RoutedHumanRule(_SkidJumpBase):
    def reset(self):
        super().reset()
        self.skid_jump_plan=None

    def act(self,decision,frame,previous_held=0):
        held=super().act(decision,frame,previous_held)
        obs=decision['observation'];me=obs['players'][self.player];raw=decision['runtimePlayers'][self.player]
        delay=int(decision.get('time',{}).get('inputDelay',-1))
        allowed=(not me['dead'] and raw['currentPowerupRaw'] in (0,1)
                 and not raw.get('damageStateRaw',1) and not raw.get('updateLockedRaw',1)
                 and raw['actionFlagRaw'] in (0,0x100000) and 0<=delay<=6
                 and 'tileGround' in me.get('contact',{}))
        other=obs['players'][self.player^1]
        if other['found'] and not other['dead']:
            dx,dy=delta(other['pos'],me['pos'])
            if abs(dx)<112 and abs(dy)<96:allowed=False
        for enemy in obs['entities']:
            if enemy.get('category') not in ('enemy_goomba','enemy_koopa','player_fireball'):continue
            if enemy.get('entityUpdateStateFound') and enemy.get('entityUpdateStateRaw')==2:continue
            dx,dy=delta(enemy['pos'],me['pos'])
            if abs(dx)<112 and abs(dy)<96:allowed=False
        x=me['pos']['x']/4096;depth=-me['pos']['y']/4096;vx=me['vel']['x']/4096
        active=self.skid_jump_plan
        if active:
            elapsed=frame-active['start'];points=active['points']
            if (not allowed or elapsed<0 or elapsed>=len(points)
                    or (elapsed>delay+2 and me['contact']['tileGround'])):active=None
            elif elapsed:
                predicted=points[elapsed-1]
                if abs(wrap(x,predicted['x']))>2 or abs(depth-predicted['depth'])>2:active=None
        direction=int(bool(held&16))-int(bool(held&32))
        previous_direction=int(bool(previous_held&16))-int(bool(previous_held&32))
        if (active is None and allowed and raw.get('behaviorFuncRaw')==0x02114978
                and raw.get('behaviorStepRaw')==2 and raw['physicsFlagRaw'] in (0,130)
                and me['contact']['tileGround'] and abs(depth-272)<.01
                and not (held|previous_held)&3 and direction==previous_direction and direction*vx<-.5):
            self.navigator.update(me)
            px=x;pv=vx;edge=None;queued=[]
            for i in range(32):
                pv=math.copysign(max(0,abs(pv)-.09375),pv);px+=pv
                if i<delay:queued.append(dict(x=px,depth=depth))
                if not any(self.navigator.occupied(px+f,depth+1) for f in (-5,0,4)):
                    edge=i+1;break
                if pv==0:break
            # Already skidding against the intended direction: do not replace
            # ordinary forward travel or the existing early route selection.
            if edge is not None and delay+2<edge<=delay+12 and len(queued)==delay:
                px=queued[-1]['x'] if queued else x
                pv=math.copysign(max(0,abs(vx)-delay*.09375),vx)
                horizontal=(16 if direction>0 else 32)|2048
                options=[]
                for hold in (12,18,24,30):
                    commands=[horizontal|2]*hold+[horizontal]*(72-hold)
                    points,end=_skid_jump_forecast(px,depth,pv,-2,True,previous_held,
                        commands,self.navigator.occupied,height=16 if raw['currentPowerupRaw']==0 else 32,facing=raw['facing'])
                    for i,point in enumerate(points):
                        if point['contact']=='floor' and point['depth']<=272:
                            stable=points[i:i+7]
                            if (len(stable)==7 and all(q['grounded'] and q['depth']==point['depth'] for q in stable)
                                    and not _skid_opponent_can_reach_plan(other, decision['runtimePlayers'][self.player^1], queued+points[:i+7])):
                                options.append(dict(start=frame,hold=hold,horizontal=horizontal,edge_after=edge,
                                    landing_after=delay+i+1,points=queued+points[:i+7]))
                            break
                if options:active=min(options,key=lambda q:(q['landing_after'],q['hold']))
        if active:
            elapsed=frame-active['start']
            held=active['horizontal']|(2 if elapsed<active['hold'] else 0)
            self.jump_until=frame
            self.trace.update(held=held,skid_jump={k:v for k,v in active.items() if k!='points'})
        self.skid_jump_plan=active
        return held

"""Bounded NPC avoidance when a rule cooldown blocks an available ground jump.

Diagnostic candidate. Enemy rectangles are ROM defaults for ordinary walking
states. They are not general collision callbacks or terrain body dimensions.
"""
_PredictiveRejumpBase=RoutedHumanRule

class RoutedHumanRule(_PredictiveRejumpBase):
    def act(self,decision,frame,previous_held=0):
        held=super().act(decision,frame,previous_held)
        obs=decision['observation'];me=obs['players'][self.player];raw=decision['runtimePlayers'][self.player]
        delay=int(decision.get('time',{}).get('inputDelay',-1))
        direction=int(bool(held&16))-int(bool(held&32))
        previous_direction=int(bool(previous_held&16))-int(bool(previous_held&32))
        if not (obs.get('stage',{}).get('id')==0 and obs.get('stage',{}).get('vsMode')==1
                and not me['dead'] and raw['currentPowerupRaw']==1
                and not raw.get('damageStateRaw',1) and not raw.get('updateLockedRaw',1)
                and raw['actionFlagRaw'] in (0,0x100000) and raw['physicsFlagRaw'] in (0,130)
                and me.get('contact',{}).get('tileGround') and 0<=delay<=6
                and frame<self.next_jump and not (held|previous_held)&3
                and direction!=0 and direction==previous_direction
                and raw.get('behaviorFuncRaw')==0x02115AAC
                and direction*me['vel']['x']>0):return held
        other=obs['players'][self.player^1]
        if other['found'] and not other['dead']:
            dx,dy=delta(other['pos'],me['pos'])
            if abs(dx)<112 and abs(dy)<96:return held
        hitbox=me.get('hitbox',{})
        if not hitbox.get('found') or hitbox.get('fixedPointShift')!=12:return held
        self.navigator.update(me);occupied=self.navigator.occupied;hazards=[]
        for e in obs['entities']:
            cat=e.get('category')
            if cat not in ('enemy_goomba','enemy_koopa','player_fireball'):continue
            dx,dy=delta(e['pos'],me['pos'])
            if abs(dx)>=112 or abs(dy)>=96:continue
            if e.get('entityUpdateStateFound') and e.get('entityUpdateStateRaw')==2:continue
            if cat=='enemy_goomba' and e.get('goombaBehaviorFunctionRaw')==0x020E1538:
                points,end=_short_jump_goomba_forecast(e,occupied,24);hw=7
            elif cat=='enemy_koopa' and e.get('koopaBehaviorFunctionRaw')==0x020DFC58 and e.get('koopaShellModeRaw')==0:
                points,end=forecast_ground_koopa(e,occupied,24);hw=8
            else:return held
            if len(points)!=24 or end!='horizon':return held
            hazards.append((points,hw))
        if not hazards:return held
        cx=hitbox['centerOffsetX']/4096;cy=hitbox['centerOffsetY']/4096
        hw=hitbox['halfWidth']/4096;hh=hitbox['halfHeight']/4096
        def clearance(points,margin_x=0,margin_y=0):
            return min(max(abs(wrap(p['x']+cx,e[i]['x']))-hw-ew-margin_x,
                           abs((p['depth']-cy)-(e[i]['depth']-8))-hh-8-margin_y)
                       for i,p in enumerate(points) for e,ew in hazards)
        initial=dict(x=me['pos']['x']/4096,depth=-me['pos']['y']/4096,
                     vx=me['vel']['x']/4096,vy=me['vel']['y']/4096,
                     grounded=True,previous_input=previous_held,occupied=occupied,
                     height=32,facing=raw['facing'])
        nominal,end=_upper_npc_release_forecast(**initial,inputs=[previous_held]*delay+[held]*(24-delay))
        if end!='horizon' or len(nominal)!=24 or clearance(nominal[:delay+10])>=0:return held
        jump,end=_upper_npc_release_forecast(**initial,inputs=[previous_held]*delay+[held|2]*6+[held]*(18-delay))
        if (end!='horizon' or len(jump)!=24 or not jump[delay]['launch']
                or any(p.get('contact') in ('ceiling','edge') for p in jump)
                # Same directional forecast allowances as the existing
                # missed-takeoff check: 0.25px horizontally, 1px vertically.
                or clearance(jump,.25,1)<0):return held
        held|=2
        # One decision pulse; do not extend the inherited 42f cooldown.
        self.trace.update(held=held,predictive_rejump=dict(frame=frame,next_jump=self.next_jump,
            nominal_clearance=clearance(nominal[:delay+10]),jump_clearance=clearance(jump)))
        return held

"""Release jump for a predicted upper NPC collision in ordinary larger forms.

The same horizontal command is retained. Ordinary walking Koopa turns are
bounded by a reachable horizontal interval on a continuously supported floor;
no exact turn animation or future opponent command is assumed.
"""
def _rising_release_koopa_bounds(entity,occupied,horizon):
    points,end=forecast_ground_koopa(entity,occupied,horizon)
    points=[dict(p,radius=0) for p in points]
    if end=='horizon':return points,end
    if end!='walking_turn' or not points:return points,end
    last=points[-1];x=last['x']+entity['vel']['x']/4096;depth=last['depth'];known=len(points)
    for i in range(known,horizon):
        # 23 native walk->turn->walk transitions: nine zero-velocity
        # observations, then one walk initialization without displacement.
        # A stomp/actor collision can interrupt this ordinary-state model.
        radius=.5*max(0,i-known-9)
        # A walking turn cannot create upward motion. Reject any reachable
        # support gap instead of extrapolating a fall at an unknown time.
        if not all(occupied(q,depth+1) for q in
                   [x-radius,x+radius]+list(range(math.floor((x-radius)/16)*16,math.ceil((x+radius)/16)*16+1,16))):
            return points,'uncertain_support'
        points.append(dict(x=x,depth=depth,radius=radius))
    return points,'bounded_turn'

_RisingNpcReleaseBase=RoutedHumanRule
class RoutedHumanRule(_RisingNpcReleaseBase):
    def reset(self):
        super().reset()
        self.rising_release_plan = None
    def act(self,decision,frame,previous_held=0):
        held=super().act(decision,frame,previous_held)
        obs=decision['observation'];me=obs['players'][self.player];raw=decision['runtimePlayers'][self.player]
        plan=self.rising_release_plan
        if plan:
            elapsed=frame-plan['start']
            points=plan['points']
            actual=dict(x=me['pos']['x']/4096,depth=-me['pos']['y']/4096,
                        vx=me['vel']['x']/4096,vy=me['vel']['y']/4096)
            expected=points[elapsed-1] if 0<elapsed<=len(points) else None
            other=obs['players'][self.player^1]
            clear_other=not(other['found'] and not other['dead'] and all(abs(v)<128 for v in delta(other['pos'],me['pos'])))
            clear_fire=not any(e.get('category')=='player_fireball'
                and not(e.get('ownerVerified') and e.get('owner')==self.player)
                and abs(delta(e['pos'],me['pos'])[0])<256 and abs(delta(e['pos'],me['pos'])[1])<128
                for e in obs['entities'])
            error=None if expected is None else max(abs(wrap(actual['x'],expected['x'])),
                *(abs(actual[k]-expected[k]) for k in ('depth','vx','vy')))
            clear_npc=True
            for e in obs['entities']:
                if e.get('category') not in ('enemy_goomba','enemy_koopa'):continue
                if e.get('entityUpdateStateFound') and e.get('entityUpdateStateRaw')==2:continue
                dx,dy=delta(e['pos'],me['pos'])
                if abs(dx)>128 or abs(dy)>128:continue
                track=plan['enemy_tracks'].get(str(e.get('actorGuid')))
                ep=track[elapsed-1] if track and 0<elapsed<=len(track) else None
                if (ep is None or abs(wrap(e['pos']['x']/4096,ep['x']))>ep.get('radius',0)+.25
                        or abs(-e['pos']['y']/4096-ep['depth'])>.25
                        or e.get('category')=='enemy_koopa' and e.get('koopaShellModeRaw')!=0):
                    clear_npc=False;break
            valid=(expected is not None and error<=.25 and not me['dead']
                   and raw['currentPowerupRaw']==plan['form'] and not raw.get('damageStateRaw',1)
                   and not raw.get('updateLockedRaw',1) and clear_other and clear_fire and clear_npc)
            if valid and elapsed<plan['landing_after']:
                held=plan['held'];self.jump_until=frame
                self.trace.update(held=held,rising_release_plan=dict(start=plan['start'],elapsed=elapsed,
                    landing_after=plan['landing_after'],error=error))
                return held
            self.trace['rising_release_plan_end']=dict(start=plan['start'],elapsed=elapsed,
                expected_landing=plan['landing_after'],valid=valid,error=error,
                clear_other=clear_other,clear_fire=clear_fire,clear_npc=clear_npc)
            self.rising_release_plan=None
        delay=int(decision.get('time',{}).get('inputDelay',-1));direction=int(bool(held&16))-int(bool(held&32))
        if not (obs.get('stage',{}).get('id')==0 and obs.get('stage',{}).get('vsMode')==1
                and not me['dead'] and raw['currentPowerupRaw'] in (1,2)
                and not me.get('visual',{}).get('starInvincible')
                and not raw.get('damageStateRaw',1) and not raw.get('updateLockedRaw',1)
                and raw['physicsFlagRaw'] in (2,128) and raw['actionFlagRaw'] in (0,0x100000)
                and raw.get('behaviorFuncRaw')==0x021135B8 and not me.get('contact',{}).get('tileGround')
                and me['vel']['y']/4096>1 and 0<=delay<=6 and held&3 and previous_held&3
                and direction*me['vel']['x']>0):return held
        other=obs['players'][self.player^1]
        if other['found'] and not other['dead'] and all(abs(v)<128 for v in delta(other['pos'],me['pos'])):return held
        hb=me.get('hitbox',{})
        if not hb.get('found') or hb.get('fixedPointShift')!=12:return held
        self.navigator.update(me);occupied=self.navigator.occupied;horizon=48;hazards=[];enemy_tracks={};upper=False
        for e in obs['entities']:
            cat=e.get('category')
            if cat not in ('enemy_goomba','enemy_koopa','player_fireball'):continue
            dx,dy=delta(e['pos'],me['pos'])
            if abs(dx)>256 or abs(dy)>128:continue
            if e.get('entityUpdateStateFound') and e.get('entityUpdateStateRaw')==2:continue
            if cat=='player_fireball':
                if e.get('ownerVerified') and e.get('owner')==self.player:continue
                return held
            if abs(dx)>128:continue
            if cat=='enemy_goomba':
                points,end=_short_jump_goomba_forecast(e,occupied,horizon);ew=7
                upper=upper or (20<dy<72 and abs(dx)<64 and dx*me['vel']['x']>0)
            elif e.get('koopaShellModeRaw')==0 and e.get('koopaBehaviorFunctionRaw')==0x020DFC58:
                points,end=_rising_release_koopa_bounds(e,occupied,horizon);ew=8
            else:return held
            if len(points)!=horizon or end not in ('horizon','bounded_turn'):return held
            hazards.append((points,ew))
            if e.get('actorGuid') is None:return held
            enemy_tracks[str(e['actorGuid'])]=points
        if not upper:return held
        cx=hb['centerOffsetX']/4096;cy=hb['centerOffsetY']/4096;hw=hb['halfWidth']/4096;hh=hb['halfHeight']/4096
        def clearance(points,mx=0,my=0):
            return min(max(max(0,abs(wrap(p['x']+cx,ep[i]['x']))-ep[i].get('radius',0))-hw-ew-mx,
                           abs((p['depth']-cy)-(ep[i]['depth']-8))-hh-8-my)
                       for i,p in enumerate(points) for ep,ew in hazards)
        initial=dict(x=me['pos']['x']/4096,depth=-me['pos']['y']/4096,vx=me['vel']['x']/4096,
                     vy=me['vel']['y']/4096,grounded=False,previous_input=previous_held,
                     occupied=occupied,height=32,facing=raw['facing'])
        nominal,end=_upper_npc_release_forecast(**initial,inputs=[previous_held]*delay+[held]*(horizon-delay))
        # A constant-direction forecast is not a reliable reason to override
        # navigation when the current waypoint is crossed before the next
        # controller update. Preserve normal navigation in that case.
        waypoint=self.trace.get('waypoint')
        if waypoint and len(nominal)>=6:
            remaining=direction*wrap(waypoint['x'],initial['x'])
            if remaining<=0 or any(direction*wrap(p['x'],initial['x'])>=remaining for p in nominal[:6]):
                return held
        if len(nominal)<24 or clearance(nominal[:24])>=0:return held
        release,end=_upper_npc_release_forecast(**initial,inputs=[previous_held]*delay+[held&~3]*(horizon-delay))
        if (end!='horizon' or len(release)!=horizon or not release[-1]['grounded']
                or any(p['depth']>288 or p['contact']=='ceiling' for p in release)
                or clearance(release,.25,1)<0):return held
        landing=next((i+1 for i,p in enumerate(release) if p['contact']=='floor'),None)
        if landing is None:return held
        self.rising_release_plan=dict(start=frame,held=held&~3,points=release,
            landing_after=landing,form=raw['currentPowerupRaw'],enemy_tracks=enemy_tracks)
        held&=~3;self.jump_until=frame
        self.trace.update(held=held,rising_npc_release=dict(frame=frame,nominal_clearance=clearance(nominal[:24]),
            released_clearance=clearance(release),landing_after=next((i+1 for i,p in enumerate(release) if p['contact']=='floor'),None)))
        return held


"""Reconsider an existing ground wait when a rolling mushroom is contested.

Only ordinary small-player, near-stationary waits are eligible. The proposal
models neutral jumps, not an opponent's future choices or pickup tie breaking.
"""
from nsmb_mvl_item_forecast import forecast as _contest_item_forecast
from nsmb_mvl_player_motion_forecast import forecast_player as _contest_player_forecast

def _contested_mushroom_hop(decision, player, previous, navigator, waiting):
    if not waiting or previous & 3:
        return None
    me=decision['observation']['players'][player]
    other=decision['observation']['players'][player^1]
    raw=decision['runtimePlayers'][player]
    other_raw=decision['runtimePlayers'][player^1]
    if (me['dead'] or raw['currentPowerupRaw']!=0 or raw['behaviorFuncRaw']!=0x02115AAC
            or raw['damageStateRaw'] or raw['updateLockedRaw']
            or raw['physicsFlagRaw'] not in (2,130,2050) or raw['actionFlagRaw'] not in (0,0x100000)
            or not me.get('contact',{}).get('tileGround') or abs(me['vel']['x']/4096)>.25
            or not other['found'] or other['dead'] or other_raw['currentPowerupRaw']==3
            or other_raw['physicsFlagRaw']&32):
        return None
    ox,oy=delta(other['pos'],me['pos'])
    if not 12<abs(ox)<64 or abs(oy)>16:
        return None
    entities=decision['observation']['entities']
    item=next((e for e in entities if e.get('actorGuid')==waiting['item_guid']),None)
    if (not item or item.get('objectId')!=31 or item.get('itemKindRaw')!=0
            or item.get('itemBehaviorFunctionRaw')!=0x020D3A30
            or item.get('entityUpdateStateRaw')!=0 or abs(item['vel']['x'])!=4096
            or not -16384<=item['vel']['y']<=0):
        return None
    navigator.update(me)
    horizon=42
    delay=int(decision['time'].get('inputDelay',2))
    if not 0<=delay<=6:return None
    item_points=_contest_item_forecast(item['pos']['x']/4096,-item['pos']['y']/4096,
        item['vel']['x']/4096,item['vel']['y']/4096,navigator.occupied,horizon)
    hazards=[]
    for entity in entities:
        kind=entity['category']
        if kind not in ('enemy_goomba','enemy_koopa','player_fireball'):continue
        if entity.get('entityUpdateStateFound') and entity.get('entityUpdateStateRaw')==2:continue
        dx,dy=delta(entity['pos'],me['pos'])
        if abs(dx)>160 or abs(dy)>128:continue
        if kind=='enemy_goomba':
            path,status=_short_jump_goomba_forecast(entity,navigator.occupied,horizon)
        elif (kind=='enemy_koopa' and entity.get('koopaShellModeRaw')==0
                and entity.get('koopaBehaviorFunctionRaw')==0x020DFC58):
            path,status=forecast_ground_koopa(entity,navigator.occupied,horizon)
        else:return None
        if len(path)!=horizon or status!='horizon':return None
        hazards.append(path)
    initial=dict(x=me['pos']['x']/4096,depth=-me['pos']['y']/4096,
        vx=me['vel']['x']/4096,vy=me['vel']['y']/4096,grounded=True,
        previous_input=previous,occupied=navigator.occupied,height=16,facing=raw['facing'])
    # Six-pixel positional margins deliberately differ from the older ten-pixel
    # wait test. They select candidates; actual collection must be measured.
    def first_overlap(points):
        return next((i+1 for i,(p,q) in enumerate(zip(points,item_points))
            if abs(wrap(p['x'],q['x']))<6 and abs(p['depth']-q['depth'])<6),None)
    staying,_=_contest_player_forecast(**initial,inputs=[previous]*delay+[0]*(horizon-delay))
    wait_pickup=first_overlap(staying)
    proposals=[]
    for hold in (6,12,18,24,30):
        points,status=_contest_player_forecast(**initial,
            inputs=[previous]*delay+[2]*hold+[0]*(horizon-delay-hold))
        pickup=first_overlap(points)
        if pickup is None or (wait_pickup is not None and pickup+6>wait_pickup):continue
        # Follow the model through landing as well as through the estimated
        # pickup; no stomp rebounds or speculative NPC knockback are allowed.
        if len(points)!=horizon or any(p['depth']>288 for p in points):continue
        safe=True
        for path in hazards:
            for p,q in zip(points,path):
                margin=max(abs(wrap(p['x'],q['x']))-16,p['depth']-16-q['depth'],q['depth']-20-p['depth'])
                if margin<4:safe=False;break
            if not safe:break
        if safe:
            proposals.append(dict(hold=hold,pickup_after=pickup,wait_pickup_after=wait_pickup,
                item_guid=item['actorGuid'],points=points,item_points=item_points))
    return min(proposals,key=lambda p:(p['pickup_after'],p['hold'])) if proposals else None

_ContestedMushroomBase=RoutedHumanRule
class RoutedHumanRule(_ContestedMushroomBase):
    def reset(self):
        super().reset()
        self.contested_mushroom_hop=None

    def act(self,decision,frame,previous_held=0):
        held=super().act(decision,frame,previous_held)
        me=decision['observation']['players'][self.player]
        raw=decision['runtimePlayers'][self.player]
        active=self.contested_mushroom_hop
        if active:
            elapsed=frame-active['start']
            entity=next((e for e in decision['observation']['entities'] if e.get('actorGuid')==active['item_guid']),None)
            if (elapsed<0 or elapsed>len(active['points']) or elapsed>=active['pickup_after']+6 or me['dead']
                    or raw['currentPowerupRaw']!=0 or raw['damageStateRaw']
                    or raw['updateLockedRaw'] or entity is None):
                active=None
            elif elapsed:
                p=active['points'][elapsed-1];q=active['item_points'][elapsed-1]
                if (abs(wrap(me['pos']['x']/4096,p['x']))>2 or abs(-me['pos']['y']/4096-p['depth'])>2
                        or abs(wrap(entity['pos']['x']/4096,q['x']))>2 or abs(-entity['pos']['y']/4096-q['depth'])>2):
                    active=None
        if active is None and self.trace.get('target')=='powerup':
            proposal=_contested_mushroom_hop(decision,self.player,previous_held,self.navigator,self.emergence_plan)
            if proposal:active=dict(proposal,start=frame)
        if active:
            held=2 if frame-active['start']<active['hold'] else 0
            self.trace.update(held=held,contested_mushroom_hop={k:v for k,v in active.items() if k not in ('points','item_points')})
        self.contested_mushroom_hop=active
        return held


"""Continue a grounded run when a reversing takeoff meets a falling Goomba.

This is an experimental guard, not a general collision-avoidance guarantee.
The input plan is short, checked against measurements, and has no map/time key.
"""
from nsmb_mvl_player_motion_forecast import forecast_player as _forward_npc_forecast

def _forward_npc_plan(decision,player,held,previous,navigator):
    me=decision['observation']['players'][player];raw=decision['runtimePlayers'][player]
    required=('currentPowerupRaw','behaviorFuncRaw','physicsFlagRaw','actionFlagRaw',
              'damageStateRaw','damageCooldownRaw','updateLockedRaw','facing')
    if any(k not in raw for k in required):return None
    direction=1 if me['vel']['x']>0 else -1
    wanted=int(bool(held&16))-int(bool(held&32))
    if (me['dead'] or raw['currentPowerupRaw'] not in (1,2)
            or raw['behaviorFuncRaw']!=0x02115AAC or raw['physicsFlagRaw'] not in (2,130,2050)
            or raw['actionFlagRaw'] not in (0,0x100000) or raw['damageStateRaw']
            or raw['damageCooldownRaw'] or raw['updateLockedRaw']
            or me.get('visual',{}).get('starInvincible')
            or not me['contact']['tileGround'] or not 1<=abs(me['vel']['x']/4096)<=3
            or wanted!=-direction or not held&3 or previous&3
            or raw['facing']!=direction):return None
    hb=me.get('hitbox',{})
    if not hb.get('found') or hb.get('fixedPointShift')!=12:return None
    delay=int(decision['time'].get('inputDelay',-1));horizon=30
    if not 0<=delay<=6:return None
    navigator.update(me);tracks={};falling=False
    for e in decision['observation']['entities']:
        cat=e['category']
        if cat not in ('enemy_goomba','enemy_koopa','player_fireball'):continue
        if e.get('entityUpdateStateFound') and e.get('entityUpdateStateRaw')==2:continue
        dx,dy=delta(e['pos'],me['pos'])
        if cat=='player_fireball':
            if e.get('ownerVerified') and e.get('owner')==player:continue
            if abs(dx)<32+4*horizon and abs(dy)<128:return None
            continue
        # 3 px/f player + 0.5 px/f ordinary Goomba + body allowance.
        # A remote Goomba's wall turn need not invalidate this local plan.
        if abs(dx)>32+3.5*horizon or abs(dy)>128:continue
        if cat!='enemy_goomba':return None
        points,status=_short_jump_goomba_forecast(e,navigator.occupied,horizon)
        if status!='horizon' or len(points)!=horizon or e.get('actorGuid') is None:return None
        tracks[str(e['actorGuid'])]=points
        falling=falling or (0<direction*dx<64 and 24<dy<80 and e['vel']['y']<0)
    if not falling:return None
    initial=dict(x=me['pos']['x']/4096,depth=-me['pos']['y']/4096,
        vx=me['vel']['x']/4096,vy=me['vel']['y']/4096,grounded=True,
        previous_input=previous,occupied=navigator.occupied,height=32,facing=raw['facing'])
    cx=hb['centerOffsetX']/4096;cy=hb['centerOffsetY']/4096
    hw=hb['halfWidth']/4096;hh=hb['halfHeight']/4096
    def clearance(p,q):
        return max(abs(wrap(p['x']+cx,q['x']))-hw-7,
                   abs(p['depth']-cy-(q['depth']-8))-hh-8)
    nominal,_=_forward_npc_forecast(**initial,inputs=[previous]*delay+[held]*(horizon-delay))
    if len(nominal)<18 or min(clearance(p,q[i]) for i,p in enumerate(nominal[:18]) for q in tracks.values())>=0:return None
    forward=(16 if direction>0 else 32)|2048
    points,status=_forward_npc_forecast(**initial,inputs=[previous]*delay+[forward]*(horizon-delay))
    if (status!='horizon' or len(points)!=horizon
            or any(not p['grounded'] or p['contact'] or p['depth']!=initial['depth'] for p in points)):
        return None
    minimum=min(clearance(p,q[i]) for i,p in enumerate(points) for q in tracks.values())
    # Half a pixel is a narrow geometric reserve, not a measured error bound.
    # This candidate must therefore pass native comparison before adoption.
    if minimum<.5:return None
    other=decision['observation']['players'][player^1]
    if other['found'] and not other['dead']:
        for i,p in enumerate(points,1):
            if abs(wrap(p['x'],other['pos']['x']/4096))<24+4*i:return None
    finish=next((i for i in range(6,horizon+1,6)
        if all(direction*wrap(points[i-1]['x']+cx,q[i-1]['x'])>hw+9 for q in tracks.values())),None)
    if finish is None:return None
    return dict(held=forward,points=points,tracks=tracks,duration=finish,
        minimum_clearance=minimum,form=raw['currentPowerupRaw'])

_ForwardNpcBase=RoutedHumanRule
class RoutedHumanRule(_ForwardNpcBase):
    def reset(self):
        super().reset();self.forward_npc_plan=None
    def act(self,decision,frame,previous_held=0):
        held=super().act(decision,frame,previous_held)
        me=decision['observation']['players'][self.player];raw=decision['runtimePlayers'][self.player]
        plan=self.forward_npc_plan
        if plan:
            elapsed=frame-plan['start'];expected=plan['points'][elapsed-1] if 0<elapsed<=len(plan['points']) else None
            error=None if expected is None else max(abs(wrap(me['pos']['x']/4096,expected['x'])),
                abs(-me['pos']['y']/4096-expected['depth']),abs(me['vel']['x']/4096-expected['vx']),
                abs(me['vel']['y']/4096-expected['vy']))
            valid=(expected is not None and error<=.25 and not me['dead']
                and raw.get('currentPowerupRaw')==plan['form'] and not raw.get('damageStateRaw',1)
                and not raw.get('damageCooldownRaw',1) and not raw.get('updateLockedRaw',1)
                and raw.get('behaviorFuncRaw')==0x02115AAC and me.get('contact',{}).get('tileGround'))
            for e in decision['observation']['entities']:
                if e['category'] not in ('enemy_goomba','enemy_koopa','player_fireball'):continue
                if e.get('entityUpdateStateFound') and e.get('entityUpdateStateRaw')==2:continue
                dx,dy=delta(e['pos'],me['pos'])
                if abs(dx)>128 or abs(dy)>128:continue
                if e['category']=='player_fireball' and e.get('ownerVerified') and e.get('owner')==self.player:continue
                track=plan['tracks'].get(str(e.get('actorGuid')))
                q=track[elapsed-1] if track and 0<elapsed<=len(track) else None
                if q is None or abs(wrap(e['pos']['x']/4096,q['x']))>.25 or abs(-e['pos']['y']/4096-q['depth'])>.25:valid=False
            if valid and elapsed<plan['duration']:
                held=plan['held'];self.jump_until=frame
                self.trace.update(held=held,forward_npc_active=dict(start=plan['start'],elapsed=elapsed,duration=plan['duration'],error=error))
                return held
            self.trace['forward_npc_end']=dict(start=plan['start'],elapsed=elapsed,valid=valid,error=error)
            self.forward_npc_plan=None
        plan=_forward_npc_plan(decision,self.player,held,previous_held,self.navigator)
        if plan:
            plan['start']=frame;self.forward_npc_plan=plan;held=plan['held'];self.jump_until=frame
            self.trace.update(held=held,forward_npc_start=dict(duration=plan['duration'],minimum_clearance=plan['minimum_clearance']))
        return held


"""Do not override navigation around an invincible opponent for an NPC crossing."""
def _forward_npc_opponent_unsafe(decision,player):
    other=decision['observation']['players'][player^1]
    raw=decision['runtimePlayers'][player^1]
    if not other.get('found') or 'currentPowerupRaw' not in raw or 'physicsFlagRaw' not in raw:
        return True
    return (raw['currentPowerupRaw']==3 or bool(raw['physicsFlagRaw']&32)
            or bool(other.get('visual',{}).get('starInvincible')))

_ForwardNpcOrdinaryPlan=_forward_npc_plan
def _forward_npc_plan(decision,player,held,previous,navigator):
    # Preserve the existing helper's navigator updates before filtering its
    # proposal. Ordinary-opponent proposals are returned without alteration.
    proposal=_ForwardNpcOrdinaryPlan(decision,player,held,previous,navigator)
    return None if _forward_npc_opponent_unsafe(decision,player) else proposal

_ForwardNpcInvincibilityBase=RoutedHumanRule
class RoutedHumanRule(_ForwardNpcInvincibilityBase):
    def act(self,decision,frame,previous_held=0):
        if self.forward_npc_plan and _forward_npc_opponent_unsafe(decision,self.player):
            self.forward_npc_plan=None
        return super().act(decision,frame,previous_held)


"""Experimental early shot during a known descent toward a walking Koopa.

The forecast detects a potential encounter; it does not certify a projectile
hit. Keep all horizontal/jump choices of the base controller and abort on
changed direction, nearby opponents, missing state, or a changed target NPC.
"""

def _descent_koopa_shot_plan(decision,player,held,previous,navigator):
    obs=decision['observation'];me=obs['players'][player];raw=decision['runtimePlayers'][player]
    required=('currentPowerupRaw','behaviorFuncRaw','damageStateRaw','damageCooldownRaw',
              'updateLockedRaw','physicsFlagRaw','actionFlagRaw','facing')
    if any(k not in raw for k in required):return None
    direction=int(bool(held&16))-int(bool(held&32))
    previous_direction=int(bool(previous&16))-int(bool(previous&32))
    delay=int(decision.get('time',{}).get('inputDelay',-1))
    if (me['dead'] or raw['currentPowerupRaw']!=2 or raw['behaviorFuncRaw']!=0x021135B8
            or raw['damageStateRaw'] or raw['damageCooldownRaw'] or raw['updateLockedRaw']
            or raw['physicsFlagRaw'] not in (0,2,128,130,2050)
            or raw['actionFlagRaw'] not in (0,0x100000) or not 0<=delay<=6
            or me.get('contact',{}).get('tileGround',True) or me['vel']['y']>=0
            or not 1<=abs(me['vel']['x']/4096)<=3 or direction*me['vel']['x']<=0
            or direction!=previous_direction or raw['facing']!=direction
            or (held|previous)&3 or not held&2048 or not previous&2048):return None
    other=obs['players'][player^1];other_raw=decision['runtimePlayers'][player^1]
    if (other['found'] and not other['dead']
            and (other_raw.get('currentPowerupRaw') not in (0,1,2)
                 or other_raw.get('physicsFlagRaw',32)&32
                 or abs(delta(other['pos'],me['pos'])[0])<128)):return None
    own_balls=0;enemies=[]
    for e in obs['entities']:
        cat=e.get('category');ex,ey=delta(e['pos'],me['pos'])
        if cat=='player_fireball':
            if e.get('ownerVerified') and e.get('owner')==player:own_balls+=1
            elif abs(ex)<128 and abs(ey)<96:return None
        if cat not in ('enemy_koopa','enemy_goomba'):continue
        if e.get('entityUpdateStateFound') and e.get('entityUpdateStateRaw')==2:continue
        if abs(ex)>=128 or abs(ey)>=96:continue
        if (cat!='enemy_koopa' or e.get('koopaBehaviorFunctionRaw')!=0x020DFC58
                or e.get('koopaShellModeRaw')!=0 or abs(e['vel']['x'])!=2048
                or e['vel']['y']!=0 or not 24<direction*ex<96 or not -80<ey<-16):return None
        enemies.append(e)
    if own_balls>=2 or len(enemies)!=1:return None
    enemy=enemies[0];navigator.update(me);horizon=30
    ep,status=forecast_ground_koopa(enemy,navigator.occupied,horizon)
    if status!='horizon' or len(ep)!=horizon:return None
    hb=me.get('hitbox',{})
    if not hb.get('found') or hb.get('fixedPointShift')!=12:return None
    initial=dict(x=me['pos']['x']/4096,depth=-me['pos']['y']/4096,
        vx=me['vel']['x']/4096,vy=me['vel']['y']/4096,grounded=False,
        previous_input=previous,occupied=navigator.occupied,height=32,facing=raw['facing'])
    contact=[]
    for counter in (False,True):
        nominal,end=_upper_npc_release_forecast(**initial,
            inputs=[previous]*delay+[held]*(horizon-delay),counter_landing_turn=counter)
        if end!='horizon' or len(nominal)!=horizon:return None
        # Do not spend the whole release/repress cycle on a descent which
        # lands early enough for the base ground avoidance to act. A constant
        # input forecast cannot model that later jump decision.
        if any(p['grounded'] for p in nominal[:delay+12]):return None
        for i,(p,q) in enumerate(zip(nominal,ep),1):
            separation=max(abs(wrap(p['x']+hb['centerOffsetX']/4096,q['x']))-hb['halfWidth']/4096-8,
                abs(p['depth']-hb['centerOffsetY']/4096-(q['depth']-12))-hb['halfHeight']/4096-12)
            if separation<4:contact.append(i);break
        released,end=_upper_npc_release_forecast(**initial,
            inputs=[previous]*delay+[held&~2048]*6+[held]*(horizon-delay-6),counter_landing_turn=counter)
        if end!='horizon' or len(released)!=horizon or any(p['depth']>288 for p in released):return None
    if not contact or not delay+12<=min(contact)<=30:return None
    return dict(guid=enemy['actorGuid'],direction=direction,contact_after=min(contact),release=6,duration=12)

_DescentKoopaShotBase=RoutedHumanRule
class RoutedHumanRule(_DescentKoopaShotBase):
    def reset(self):
        super().reset();self.descent_koopa_shot=None;self.descent_koopa_retry=-1

    def act(self,decision,frame,previous_held=0):
        held=super().act(decision,frame,previous_held)
        me=decision['observation']['players'][self.player];raw=decision['runtimePlayers'][self.player]
        plan=self.descent_koopa_shot
        if plan:
            enemy=next((e for e in decision['observation']['entities'] if e.get('actorGuid')==plan['guid']),None)
            other=decision['observation']['players'][self.player^1]
            other_raw=decision['runtimePlayers'][self.player^1]
            direction=int(bool(held&16))-int(bool(held&32))
            valid=(not me['dead'] and raw.get('currentPowerupRaw')==2 and not raw.get('damageStateRaw',1)
                and not raw.get('damageCooldownRaw',1) and not raw.get('updateLockedRaw',1)
                and raw.get('physicsFlagRaw') in (0,2,128,130,2050)
                and direction==plan['direction'] and not held&3
                and raw.get('behaviorFuncRaw') in (0x021135B8,0x02110DC0,0x02115AAC)
                and enemy is not None and enemy.get('entityUpdateStateRaw')==0
                and enemy.get('koopaShellModeRaw')==0
                and (not other['found'] or other['dead'] or (
                    other_raw.get('currentPowerupRaw') in (0,1,2)
                    and not other_raw.get('physicsFlagRaw',32)&32
                    and abs(delta(other['pos'],me['pos'])[0])>=96)))
            for e in decision['observation']['entities']:
                if e.get('category')!='player_fireball':continue
                if e.get('ownerVerified') and e.get('owner')==self.player:continue
                ex,ey=delta(e['pos'],me['pos'])
                if abs(ex)<96 and abs(ey)<96:valid=False
            if frame-plan['start']>=plan['duration'] or not valid:
                self.trace['descent_koopa_shot_end']=dict(start=plan['start'],elapsed=frame-plan['start'],valid=valid)
                plan=None
        if plan is None and frame>=self.descent_koopa_retry and self.trace.get('target')=='carrier':
            proposal=_descent_koopa_shot_plan(decision,self.player,held,previous_held,self.navigator)
            if proposal:
                plan=dict(proposal,start=frame);self.descent_koopa_retry=frame+60
        if plan:
            held=held&~2048 if frame-plan['start']<plan['release'] else held|2048
            self.trace.update(held=held,descent_koopa_shot=plan.copy())
        self.descent_koopa_shot=plan
        return held


# Isolated candidate; leave adopted control and rolling-interception experiments separate.
from nsmb_mvl_item_forecast import forecast as _falling_mushroom_forecast

def _falling_mushroom_beyond_reach(decision, player, entity, navigator):
    obs=decision['observation'];me=obs['players'][player];raw=decision['runtimePlayers'][player]
    depth=-me['pos']['y']/4096
    if (obs.get('stage',{}).get('id')!=0 or obs.get('stage',{}).get('vsMode')!=1
        or me['dead'] or raw.get('currentPowerupRaw')!=0 or not 0<=depth<=288
        or raw.get('damageStateRaw',1) or raw.get('updateLockedRaw',1)
        or raw.get('behaviorFuncRaw') not in (0x02115AAC,0x021135B8,0x02114978)
        or entity.get('objectId')!=31 or entity.get('itemKindRaw')!=0
        or entity.get('itemBehaviorFunctionRaw')!=0x020D3A30
        or entity.get('itemRollingSuppressedRaw')!=0
        or entity.get('vel',{}).get('y')!=-16384):return None
    item_depth=-entity['pos']['y']/4096
    if item_depth<=288 or abs(delta(entity['pos'],me['pos'])[0])>96:return None
    other=obs['players'][player^1]
    if other['found'] and not other['dead'] and abs(delta(other['pos'],entity['pos'])[0])<160:return None
    predicted=_falling_mushroom_forecast(entity['pos']['x']/4096,item_depth,
        entity['vel']['x']/4096,-4,navigator.occupied,48)
    points=[dict(frame=0,depth=item_depth,vy=-4)]+predicted
    minimum=float('inf')
    for point in points:
        # An immediate 6 px/frame dive is more generous than normal falling;
        # allow foot depth 364 (observed grass deaths are around 360) and a
        # 20 px pickup allowance (ordinary item top is 16 px above origin).
        # These are explicit physical assumptions, not a full reachability proof.
        if point['vy']!=-4:return None
        best_foot=min(depth+6*point['frame'],364)
        gap=point['depth']-20-best_foot
        minimum=min(minimum,gap)
        if gap<=0:return None
        if point['depth']>384:
            return dict(item_guid=entity.get('actorGuid'),item_depth=item_depth,
                player_depth=depth,frames=point['frame'],minimum_gap=minimum)
    return None

_FallingMushroomBase=RoutedHumanRule
class RoutedHumanRule(_FallingMushroomBase):
    def reset(self):
        super().reset()
        self.falling_mushroom_navigator=GrassNavigator()
        self.falling_mushroom_excluded=[]

    def targets(self, decision):
        choices=super().targets(decision)
        self.falling_mushroom_excluded=[]
        obs=decision['observation'];me=obs['players'][self.player]
        # Separate navigation state: target inspection must not mutate base routing.
        entities=[e for e in obs['entities'] if self.is_equipment_item(e)
            and e.get('vel',{}).get('y')==-16384 and -e['pos']['y']/4096>288]
        if not entities:return choices
        self.falling_mushroom_navigator.update(me)
        excluded=set()
        for entity in entities:
            reason=_falling_mushroom_beyond_reach(decision,self.player,entity,self.falling_mushroom_navigator)
            if reason:
                excluded.add((entity['pos']['x'],entity['pos']['y']))
                self.falling_mushroom_excluded.append(reason)
        return [c for c in choices if not(c[1]=='powerup' and (c[2]['x'],c[2]['y']) in excluded)]

    def act(self, decision, frame, previous_held=0):
        held=super().act(decision,frame,previous_held)
        if self.falling_mushroom_excluded:
            self.trace['falling_mushroom_excluded']=self.falling_mushroom_excluded
        return held

def _counterjump_hazards_clear(decision,player,points,occupied):
    me=decision['observation']['players'][player];hb=me['hitbox']
    for e in decision['observation']['entities']:
        cat=e.get('category');dx,dy=delta(e['pos'],me['pos'])
        if cat in ('enemy_goomba','enemy_koopa'):
            if e.get('entityUpdateStateFound') and e.get('entityUpdateStateRaw')==2:continue
            if abs(dx)<160 and abs(dy)<128:return False
        if cat!='player_fireball':continue
        if e.get('ownerVerified') and e.get('owner')==player:continue
        if abs(dx)>256:continue
        x=e['pos']['x']/4096;depth=-e['pos']['y']/4096
        vx=e['vel']['x']/4096;vy=e['vel']['y']/4096
        if abs(vx)!=3.625 or not -4<=vy<=4:return False
        for p in points:
            x+=vx;depth-=vy;next_vy=max(-4,vy-.4375)
            floor=math.floor(depth/16)*16
            if occupied(x,depth) and not occupied(x,floor-.01):
                depth=float(floor)
                if vy<0:next_vy=4
            vy=next_vy
            separation=max(abs(wrap(p['x']+hb['centerOffsetX']/4096,x))-hb['halfWidth']/4096-4,
                abs(p['depth']-hb['centerOffsetY']/4096-depth)-hb['halfHeight']/4096-4)
            if separation<4:return False
    return True



# A local risk reduction, not a guarantee against future shots or human inputs.
def _overhead_pit_jump_plan(decision,player,held,previous,navigator,trace):
    me=decision['observation']['players'][player];raw=decision['runtimePlayers'][player]
    other=decision['observation']['players'][player^1];ort=decision['runtimePlayers'][player^1]
    direction=int(bool(held&16))-int(bool(held&32))
    if (not trace.get('overhead_evade') or not trace.get('grounded')
            or direction!=trace.get('body_avoid') or not direction
            or held&~(48|2048) or not held&2048 or previous&3
            or me['dead'] or not me.get('contact',{}).get('tileGround')
            or raw.get('currentPowerupRaw') not in (0,1,2)
            or raw.get('behaviorFuncRaw') not in (0x02115AAC,0x021135B8,0x02110DC0)
            or raw.get('actionFlagRaw') not in (0,0x100000)
            or raw.get('physicsFlagRaw') not in (0,2,128,130,2050)
            or raw.get('damageStateRaw',1) or raw.get('damageCooldownRaw',1)
            or raw.get('updateLockedRaw',1) or me.get('visual',{}).get('starInvincible')
            or ort.get('currentPowerupRaw') not in (0,1,2)
            or ort.get('physicsFlagRaw',32)&32 or other.get('visual',{}).get('starInvincible')):
        return None
    delay=int(decision['time'].get('inputDelay',-1))
    if not 0<=delay<=6:return None
    x=me['pos']['x']/4096;depth=-me['pos']['y']/4096
    if not 0<=depth<=272 or abs(me['vel']['x']/4096)>3:return None
    navigator.update(me)
    if all(navigator.occupied(x+direction*n,depth+8) for n in (8,16,24,32,40,48)):
        return None
    initial=dict(x=x,depth=depth,vx=me['vel']['x']/4096,vy=me['vel']['y']/4096,
        grounded=True,previous_input=previous,occupied=navigator.occupied,
        height=16 if raw['currentPowerupRaw']==0 else 27,facing=raw['facing'])
    nominal,_=forecast_player(**initial,inputs=[previous]*delay+[held]*(60-delay))
    edge=next((i+1 for i,p in enumerate(nominal) if p['contact']=='edge'),None)
    if edge is None or edge>36:return None
    button=held|2
    points,status=forecast_player(**initial,inputs=[previous]*delay+[button]*(24-delay)+[held]*36)
    if (status!='horizon' or len(points)!=60 or not points[-1]['grounded']
            or any(p['depth']>depth+1 or p['contact'] in ('ceiling','ground_wall') for p in points)
            or not _counterjump_hazards_clear(decision,player,points[:24],navigator.occupied)):
        return None
    landing=next((i+1 for i,p in enumerate(points) if i>delay and p['contact']=='floor'),None)
    if landing is None:return None
    return dict(held=button,points=points,form=raw['currentPowerupRaw'],duration=24,
                nominal_edge_after=edge,predicted_landing_after=landing)

_OverheadPitBase=RoutedHumanRule
class RoutedHumanRule(_OverheadPitBase):
    def reset(self):
        super().reset();self.overhead_pit_plan=None;self.overhead_pit_nav=GrassNavigator()
        self.overhead_pit_retry=-1
    def act(self,decision,frame,previous_held=0):
        held=super().act(decision,frame,previous_held)
        me=decision['observation']['players'][self.player];raw=decision['runtimePlayers'][self.player]
        plan=self.overhead_pit_plan
        if plan:
            elapsed=frame-plan['start']
            point=plan['points'][elapsed-1] if 0<elapsed<plan['duration'] else None
            error=None if point is None else max(abs(wrap(me['pos']['x']/4096,point['x'])),
                abs(-me['pos']['y']/4096-point['depth']))
            valid=(point is not None and error<=.5 and not me['dead']
                and raw.get('currentPowerupRaw')==plan['form']
                and not raw.get('damageStateRaw',1) and not raw.get('damageCooldownRaw',1)
                and not raw.get('updateLockedRaw',1)
                and raw.get('behaviorFuncRaw') in (0x02115AAC,0x021135B8,0x02110DC0)
                and raw.get('actionFlagRaw') in (0,0x100000))
            if valid:
                self.overhead_pit_nav.update(me)
                valid=_counterjump_hazards_clear(decision,self.player,plan['points'][elapsed:elapsed+6],self.overhead_pit_nav.occupied)
            if not valid:
                self.trace['overhead_pit_end']=dict(start=plan['start'],elapsed=elapsed,error=error)
                plan=None
        if plan is None and frame>=self.overhead_pit_retry:
            proposal=_overhead_pit_jump_plan(decision,self.player,held,previous_held,self.overhead_pit_nav,self.trace)
            if proposal:
                plan=dict(proposal,start=frame);self.overhead_pit_retry=frame+60
        if plan:
            held=plan['held']
            self.trace.update(held=held,overhead_pit_jump={k:v for k,v in plan.items() if k!='points'})
        self.overhead_pit_plan=plan
        return held
