"""Small, bounded ground interception search for an emerging normal mushroom.

No opponent trajectory is assumed. The caller must abort a committed plan if
measured motion diverges, the item disappears, or player state changes.
"""
from nsmb_mvl_item_forecast import forecast_emerging
from nsmb_mvl_player_motion_forecast import MovementForecastCache, forecast_player
from nsmb_mvl_native_interception import select_interception


def plan_ground_interception(initial, previous_input, delay, entity, occupied,
                             enemy_paths=(), horizon=160):
    items = forecast_emerging(entity, occupied, horizon)
    if items is None or not initial['grounded'] or delay not in range(7):
        return None
    trials=[dict(held=held,duration=duration,inputs=[previous_input]*delay+[held]*duration+[0]*(horizon-delay-duration))
            for held in (2064,2080,16,32) for duration in range(6,97,6)]
    native=select_interception(initial,previous_input,occupied,items,enemy_paths,trials,0)
    if native is not None:
        selected,pickup=native
        if selected<0:return None
        trial=trials[selected];inputs=trial['inputs'];item=items[pickup-1]
        points,_=forecast_player(initial['x'],initial['depth'],initial['vx'],initial['vy'],True,
                                previous_input,inputs,occupied,height=16,facing=initial['facing'])
        return dict(pickup_frame=pickup,held=trial['held'],duration=trial['duration'],
                    input_sequence=inputs[delay:pickup],points=points[:pickup],item_points=items[:pickup],
                    item_guid=entity.get('actorGuid'),item_x=item['x'],item_depth=item['depth'])
    candidates = []
    forecast = MovementForecastCache(initial, previous_input, occupied)
    best_pickup = horizon+1
    for held in (2064, 2080, 16, 32):
        for duration in range(6, 97, 6):
            inputs = [previous_input]*delay + [held]*duration + [0]*(horizon-delay-duration)
            points = []
            for index, (point, item) in enumerate(zip(forecast.iter_points(inputs), items)):
                points.append(point)
                if index+1 > best_pickup:
                    break
                if not point['grounded'] or point['depth'] > 288:
                    break
                if any(index >= len(path) or
                       (abs((point['x']-path[index]['x']+512)%1024-512) < 16
                        and abs(point['depth']-path[index]['depth']) < 24)
                       for path in enemy_paths):
                    break
                separation = abs((point['x']-item['x']+512)%1024-512)
                if item['collectable'] and separation < 10 and abs(point['depth']-item['depth']) < 10:
                    candidates.append(dict(pickup_frame=index+1, held=held, duration=duration,
                        input_sequence=inputs[delay:index+1], points=points[:index+1],
                        item_points=items[:index+1],
                        item_guid=entity.get('actorGuid'), item_x=item['x'], item_depth=item['depth']))
                    best_pickup = min(best_pickup, index+1)
                    break
    return min(candidates, key=lambda p: (p['pickup_frame'],
               not bool(p['held'] & 2048), p['duration'])) if candidates else None
