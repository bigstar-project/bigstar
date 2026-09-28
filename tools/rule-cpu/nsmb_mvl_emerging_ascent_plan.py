"""Bounded alternative to waiting: coast, jump beside the box, then intercept.

Diagnostic candidate only. Predicts ordinary small-player physics; opponent
contacts and the post-collection transformation are not simulated.
"""
from nsmb_mvl_item_forecast import forecast_emerging
from nsmb_mvl_player_motion_forecast import MovementForecastCache, forecast_player
from nsmb_mvl_native_interception import select_interception


def plan_ascent_interception(initial, previous_input, delay, entity, occupied,
                             enemy_paths=(), horizon=130):
    items = forecast_emerging(entity, occupied, horizon)
    if items is None or not initial['grounded'] or delay not in range(7):
        return None
    dx = (entity['pos']['x']/4096-initial['x']+512)%1024-512
    toward = 16 if dx >= 0 else 32
    trials=[]
    for coast in (0,6,12,18):
        for neutral_jump in (0,6,12):
            for jump in (12,18,24,30):
                for run in (toward,toward|2048):
                    inputs=[previous_input]*delay+[0]*coast+[2]*neutral_jump+[toward|2]*jump+[run]*(horizon-delay-coast-neutral_jump-jump)
                    trials.append(dict(coast=coast,neutral_jump=neutral_jump,jump=jump,held=run,duration=len(inputs)-delay,inputs=inputs))
    native=select_interception(initial,previous_input,occupied,items,enemy_paths,trials,1)
    if native is not None:
        selected,pickup=native
        if selected<0:return None
        trial=trials[selected];inputs=trial['inputs'];item=items[pickup-1]
        points,_=forecast_player(initial['x'],initial['depth'],initial['vx'],initial['vy'],True,
                                previous_input,inputs,occupied,height=16,facing=initial['facing'])
        return dict(pickup_frame=pickup,coast=trial['coast'],neutral_jump=trial['neutral_jump'],jump=trial['jump'],
                    held=trial['held'],duration=trial['duration'],input_sequence=inputs[delay:],points=points,
                    item_points=items,item_guid=entity.get('actorGuid'),item_x=item['x'],item_depth=item['depth'])
    best = None
    forecast = MovementForecastCache(initial, previous_input, occupied)
    for coast in (0, 6, 12, 18):
        for neutral_jump in (0, 6, 12):
            for jump in (12, 18, 24, 30):
                for run in (toward, toward | 2048):
                    inputs = ([previous_input]*delay + [0]*coast + [2]*neutral_jump
                              + [toward | 2]*jump
                              + [run]*(horizon-delay-coast-neutral_jump-jump))
                    points = []
                    stream = forecast.iter_points(inputs)
                    for index, (point, item) in enumerate(zip(stream, items)):
                        points.append(point)
                        # Equal-time candidates cannot replace the earlier one.
                        if best is not None and index+1 >= best['pickup_frame']:
                            break
                        if point['depth'] > 288:
                            break
                        if any(index >= len(path) or
                               (abs((point['x']-path[index]['x']+512)%1024-512) < 16
                                and abs(point['depth']-path[index]['depth']) < 24)
                               for path in enemy_paths):
                            break
                        separation = abs((point['x']-item['x']+512)%1024-512)
                        if item['collectable'] and separation < 10 and abs(point['depth']-item['depth']) < 10:
                            if best is None or index+1 < best['pickup_frame']:
                                # The winning plan keeps its original full trajectory.
                                points.extend(stream)
                                best = dict(pickup_frame=index+1, coast=coast,
                                    neutral_jump=neutral_jump, jump=jump, held=run,
                                    duration=len(inputs)-delay, input_sequence=inputs[delay:],
                                    points=points, item_points=items, item_guid=entity.get('actorGuid'),
                                    item_x=item['x'], item_depth=item['depth'])
                            break
    return best
