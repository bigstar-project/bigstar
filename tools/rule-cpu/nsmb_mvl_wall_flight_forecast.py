"""Diagnostic forecast of an already launched, ordinary wall jump.

Supports an existing slide into a normal wall jump and its locked ascent.
Entry into sliding, ceilings, enemies, mini/mega forms and firing eligibility
are deliberately not inferred here.
The caller supplies known successful fire transitions separately from buttons.
Bounded controller corrections use forecast_wall_flight; wall-slide prediction
remains available for offline diagnostics.
"""
import math

from nsmb_mvl_jump_physics_audit import vertical_step, horizontal_run_step, horizontal_walk_step


def near_floor(x, depth, occupied):
    # Predict-ground/ledge transitions are not ordinary airborne motion. Stop
    # conservatively before them; do not turn a possible ledge grab into a
    # guaranteed landing. Native failures include both ascending pit exits
    # and descending wall slides near the main floor.
    return any(occupied(x + offset, depth + distance)
               for offset in (-5, 0, 4) for distance in (1, 8, 16))


def forecast_wall_flight(x, depth, vx, vy, inputs, occupied, height=27,
                         wall_left=False, wall_right=False, fire_frames=()):
    """Return predicted locked frames and a stop reason before unknown states.

    Wall contact from the preceding frame stops locked horizontal motion on the
    next update. Direction buttons cannot reverse the lock. A successful fire
    transition or a previous vertical speed below .75 releases it.
    """
    points = []
    for frame, held in enumerate(inputs, 1):
        if frame in fire_frames:
            return points, 'fire_release'
        if vy < .75:
            return points, 'vertical_release'
        if (vx > 0 and wall_right) or (vx < 0 and wall_left):
            vx = 0
        x += vx
        vy = vertical_step(vy, bool(held & 3))
        next_depth = depth - vy
        if near_floor(x, next_depth, occupied):
            return points, 'floor_proximity'
        # Do not silently forecast through an unmodeled ceiling transition.
        if any(occupied(x + offset, next_depth - height) for offset in (-2, 0, 1)):
            return points, 'ceiling'
        depth = next_depth
        wall_left = any(occupied(x - 9, depth - h) for h in (4, height - 8))
        wall_right = any(occupied(x + 8, depth - h) for h in (4, height - 8))
        # Native fixed-point side corrections are asymmetric by one pixel.
        # Only clamp penetration, not a center still inside the sensor margin.
        if wall_right:
            boundary = math.floor((x + 8) / 16) * 16
            x = min(x, boundary - 7 - 1 / 4096)
        if wall_left:
            boundary = math.floor((x - 9) / 16) * 16 + 16
            x = max(x, boundary + 8)
        points.append(dict(x=x, depth=depth, vx=vx, vy=vy,
                           wall_left=wall_left, wall_right=wall_right))
    return points, 'horizon'


def forecast_wall_slide(x, depth, vy, previous_input, inputs, occupied, side,
                        height=27, fire_frames=(), vx=0):
    """Forecast an existing normal wall slide, then its jump launch.

    Restricted to input into the wall. Departures, loss of wall support and
    other state transitions stop the diagnostic instead of being guessed.
    A fresh jump press freezes vertical movement for one frame before launch.
    """
    if side not in (-1, 1):
        raise ValueError('Wall side must be -1 or +1')
    points = []
    prepare = False
    for frame, held in enumerate(inputs, 1):
        if frame in fire_frames:
            return points, 'fire_release'
        if prepare:
            vx = -side * 2.25
            x += vx
            vy = 3.4375
            depth -= vy
            if near_floor(x, depth, occupied):
                return points, 'floor_proximity'
            point = dict(x=x, depth=depth, vx=vx, vy=vy,
                         wall_left=False, wall_right=False)
            points.append(point)
            rest, end = forecast_wall_flight(
                x, depth, vx, vy, inputs[frame:], occupied, height=height,
                fire_frames=tuple(t-frame for t in fire_frames if t > frame))
            return points + rest, end
        direction = int(bool(held & 16)) - int(bool(held & 32))
        if direction != side:
            return points, 'wall_departure_input'
        sensor = 8 if side > 0 else -9
        if not any(occupied(x + sensor, depth - h) for h in (4, height - 8)):
            return points, 'lost_wall'
        if vx * side > 0:
            vx = 0
        vx = horizontal_run_step(vx, side) if held & 2048 else horizontal_walk_step(vx, side)
        x += vx
        boundary = math.floor((x + sensor) / 16) * 16
        x = min(x, boundary - 7 - 1/4096) if side > 0 else max(x, boundary + 24)
        prepare = bool(held & 3) and not previous_input & 3
        if prepare:
            vy = 0
        else:
            vy = min(-2.5, vy + .34375) if vy < -2.5 else max(-2.5, vy - .34375)
            depth -= vy
        if near_floor(x, depth, occupied):
            return points, 'floor_proximity'
        points.append(dict(x=x, depth=depth, vx=vx, vy=vy,
                           wall_left=side < 0, wall_right=side > 0))
        previous_input = held
    return points, 'horizon'
