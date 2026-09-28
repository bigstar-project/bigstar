"""Grass match controller, inspired by documented human tactics.

Thresholds are engineering hypotheses, not fitted human optima. World Y is
up-positive. Controllers receive actual player indices, never swapped raw IDs.
"""
import math

def wrapped_dx(x, origin):
    width = 1024 * 4096
    return (x - origin + width // 2) % width - width // 2


def delta(target, position):
    return wrapped_dx(target['x'], position['x']) / 4096, (target['y'] - position['y']) / 4096


def terrain_cell(player, offset_x, offset_depth):
    grid = player['terrain']
    x, depth = player['pos']['x'] / 4096, -player['pos']['y'] / 4096
    rx = math.floor((x + offset_x) / 16) - math.floor(x / 16)
    ry = math.floor((depth + offset_depth) / 16) - math.floor(depth / 16)
    if not (grid['minRelTileX'] <= rx < grid['minRelTileX'] + grid['width']
            and grid['minRelTileY'] <= ry < grid['minRelTileY'] + grid['height']):
        return None
    cell = next((c for c in grid['cells'] if (c['rx'], c['ry']) == (rx, ry)), None)
    if cell is None:
        return 0 if grid.get('omittedCellFound') else None
    return cell['mask'] if cell['found'] else None


def solid(mask):
    return mask is not None and bool(mask & (1 | 32768)) and not bool(mask & (2048 | 8192))


class HumanInspiredRule:
    """Target selection, local navigation and combat with inspectable reasons."""
    def __init__(self, player, period=48):
        if player not in (0, 1) or period < 24:
            raise ValueError('Invalid player or fire period')
        self.player, self.period = player, period
        self.reset()

    def reset(self):
        self.previous_frame = -1
        self.jump_until = -1
        self.next_jump = 0
        self.box_retry = {}
        self.trace = {}

    def targets(self, decision):
        obs = decision['observation']
        me, other = obs['players'][self.player], obs['players'][self.player ^ 1]
        runtime = decision['runtimePlayers'][self.player]
        position = me['pos']
        candidates = []

        def add(kind, pos, bonus=0):
            dx, dy = delta(pos, position)
            candidates.append((abs(dx) + abs(dy) * 1.5 - bonus, kind, pos))

        natural = obs['targets']['bigStarActor']
        if natural['found']:
            add('natural_star', natural['pos'])
        for entity in obs['entities']:
            if entity['category'] == 'dropped_star_item':
                add('dropped_star', entity['pos'], 80)
            # Verified mushroom/flower actor from grass power-up boxes.
            if (self.needs_equipment(runtime)
                    and self.is_equipment_item(entity)):
                add('powerup', entity['pos'], 120)
        if self.needs_equipment(runtime):
            for cell in me['terrain']['cells']:
                behavior = cell.get('behavior', 0)
                behavior = int(behavior, 0) if isinstance(behavior, str) else behavior
                if cell['found'] and cell.get('status', 0) == 0 and behavior == 0x50000:
                    pos = dict(x=(position['x'] // 65536 + cell['rx']) * 65536 + 32768,
                               y=-((-position['y']) // 65536 + cell['ry']) * 65536 - 32768)
                    key = (pos['x'] % (1024 * 4096), pos['y'])
                    # The sampled behavior alone need not identify a freshly
                    # exhausted box. Observe emergence, then delay revisiting.
                    for entity in obs['entities']:
                        if self.is_equipment_item(entity):
                            ex, ey = delta(entity['pos'], pos)
                            if abs(ex) < 20 and -8 <= ey <= 48:
                                self.box_retry[key] = self.previous_frame + 300
                    if self.previous_frame >= self.box_retry.get(key, -1):
                        add('powerup_box', pos, 80)
        if other['found'] and not other['dead'] and other['battleStars']:
            # Behind: seek an attack opportunity. Nearby loose stars still win.
            add('carrier', other['pos'], 20 if me['battleStars'] < other['battleStars'] else -100)
        return sorted(candidates, key=lambda row: row[0])

    def needs_equipment(self, runtime):
        return runtime['currentPowerupRaw'] in (0, 1)

    def is_equipment_item(self, entity):
        return entity.get('objectId') == 31 and entity.get('settings') == 524288

    def act(self, decision, frame, previous_held=0):
        if frame <= self.previous_frame:
            self.reset()
        self.previous_frame = frame
        obs = decision['observation']
        if obs['stage']['id'] != 0 or obs['stage']['group'] != 9:
            raise ValueError('Rule controller is calibrated only for grass')
        me, other = obs['players'][self.player], obs['players'][self.player ^ 1]
        raw = decision['runtimePlayers'][self.player]
        self.trace = {'frame': frame, 'player': self.player, 'target': None, 'held': 0}
        if not me['found'] or me['dead'] or not raw['found']:
            return 0
        choices = self.targets(decision)
        if choices:
            _, kind, pos = choices[0]
            dx, dy = delta(pos, me['pos'])
        else:
            kind, dx, dy = 'wait', 0, 0
        descend_for_box = kind == 'powerup_box' and dy < 24
        if descend_for_box and abs(dx) < 40:
            # A box must be struck from below; standing on it cannot open it.
            dx = -48 if dx >= 0 else 48
        enemy_dx, enemy_dy = delta(other['pos'], me['pos']) if other['found'] else (1024, 0)
        # Do not pursue a carrier right into contact while already leading.
        if kind == 'carrier' and me['battleStars'] > other['battleStars'] and abs(enemy_dx) < 112:
            dx = -enemy_dx
            kind = 'protect_lead'
        tolerance = 5 if kind == 'powerup_box' else 10
        direction = 1 if dx > tolerance else -1 if dx < -tolerance else 0
        if kind == 'carrier' and abs(dx) < 64 and raw['currentPowerupRaw'] == 2:
            direction = 0
        grounded = bool(raw['collisionFlagRaw'] & (1 | 0x8000))
        wall = bool(raw['collisionFlagRaw'] & (0x810 if direction > 0 else 0x408)) if direction else False
        obstacle = direction and any(solid(terrain_cell(me, direction * 20, d)) for d in (-12, -28))
        supports = [terrain_cell(me, direction * 32, d) for d in (8, 24, 40)] if direction else [1]
        gap = all(v is not None and not solid(v) for v in supports)
        need_jump = (direction and (wall or obstacle or gap)) or (dy > 20 and abs(dx) < 80)
        if kind == 'powerup_box':
            need_jump = ((direction and (wall or obstacle or gap)) or (abs(dx) < 9 and dy > 16)) and not descend_for_box
        # Moving ground enemies are not walls: create a jump opportunity before
        # contact. This is a local safety heuristic, not perfect fire evasion.
        danger = False
        for entity in obs['entities']:
            if entity['category'] not in ('enemy_goomba', 'enemy_koopa'):
                continue
            ex, ey = delta(entity['pos'], me['pos'])
            if abs(ex) < 56 and abs(ey) < 24 and direction * ex > 0:
                danger = True
                need_jump = True
        if raw['actionFlagRaw'] & 4 and frame >= self.next_jump:
            # Wall-slide recovery; native wall jump supplies the kick.
            need_jump = True
        if need_jump and frame >= self.next_jump and (grounded or raw['actionFlagRaw'] & 4):
            self.jump_until, self.next_jump = frame + 30, frame + 42
        held = 16 if direction > 0 else 32 if direction < 0 else 0
        if frame < self.jump_until:
            held |= 2
        # Human data: more fire above/same level, reposition below the rival.
        can_shoot = raw['currentPowerupRaw'] == 2 and other['found'] and not other['dead']
        facing_enemy = raw['facingKnown'] and raw['facing'] == (-1 if enemy_dx < 0 else 1)
        shot = can_shoot and abs(enemy_dx) < 240 and -96 <= enemy_dy <= 24 and frame % self.period < 6
        if shot and not direction and not facing_enemy:
            held |= 32 if enemy_dx < 0 else 16
        if shot and facing_enemy:
            held |= 2048
        elif direction and kind not in ('powerup_box', 'protect_lead') and abs(dx) > 96 and not gap:
            # A release window permits another Y edge while traversing.
            held |= 2048 if frame % self.period >= 6 else 0
        self.trace.update(target=kind, dx=dx, dy=dy, gap=gap, obstacle=bool(obstacle), danger=danger,
                          grounded=grounded, held=held)
        return held
