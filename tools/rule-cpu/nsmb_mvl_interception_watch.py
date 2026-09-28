"""Invalidate a short item plan when the observed world no longer matches it."""


def invalidation_reason(plan, frame, player, entities, terrain_signature):
    elapsed = frame-plan['start']
    if elapsed < 0 or elapsed > plan['pickup_frame']+6:
        return 'expired'
    if terrain_signature != plan['terrain_signature']:
        return 'terrain_changed'
    entity = next((e for e in entities if e.get('actorGuid') == plan['item_guid']), None)
    if entity is None:
        return 'item_missing'
    if elapsed == 0:
        return None
    for name, observed, points in (
            ('player', player, plan['points']), ('item', entity, plan['item_points'])):
        expected = points[min(elapsed-1, len(points)-1)]
        x, depth = observed['pos']['x']/4096, -observed['pos']['y']/4096
        if (abs((x-expected['x']+512)%1024-512) > 2
                or abs(depth-expected['depth']) > 2):
            return name+'_deviation'
    return None
