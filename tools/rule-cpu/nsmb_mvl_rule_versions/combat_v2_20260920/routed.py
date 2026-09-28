"""Human-inspired objectives with stage routes, momentum braking and recovery."""
from .base import HumanInspiredRule, delta, terrain_cell, solid
from .navigation import GrassNavigator, dx as wrap


class RoutedHumanRule(HumanInspiredRule):
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
        return 0

    def needs_equipment(self, runtime):
        return runtime['currentPowerupRaw'] in (0, 1, 4)

    def reset(self):
        super().reset()
        self.navigator=GrassNavigator()
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
        if dangerous and abs(ex)<160 and not raw['physicsFlagRaw']&32 and raw['currentPowerupRaw']!=3:
            kind='escape_invincible';pos=dict(x=me['pos']['x']+(-144 if ex>0 else 144)*4096,y=me['pos']['y'])
            tx,ty=delta(pos,me['pos'])
        if (kind=='carrier' and me['battleStars']>other['battleStars']
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
        overhead_evade=(grounded and me['battleStars']>0 and other['found']
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
        if raw['inventoryPowerupRaw'] in (1,2) and raw['currentPowerupRaw'] in (0,1,4) and frame%120<6:held|=1024
        self.trace.update(target=kind,dx=tx,dy=ty,nav_dx=move_dx,waypoint=nav,
                          gap=gap,danger=danger,grounded=grounded,held=held,stuck=self.stuck_frames,
                          recovery=recovery,in_shaft=in_shaft,landing_avoid=landing_avoid,item_avoid=item_avoid,
                          stomp_attempt=stomp_attempt,close_stomp=close_stomp,ascent_run=ascent_run,
                          landing_brake=landing_brake,runup=self.runup.copy() if self.runup else None,
                          overhead_evade=overhead_evade,body_jump=body_jump,body_avoid=body_avoid,body_evade=body_evade,ascent_retries=attempts)
        return held
