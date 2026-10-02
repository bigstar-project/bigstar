"""Preserve a carried object while counter-steering for an airborne landing."""


def retain_during_brake(decision, player, held, previous, landing_brake):
    me = decision['observation']['players'][player]
    raw = decision['runtimePlayers'][player]
    direction = int(bool(held & 16))-int(bool(held & 32))
    # PlayerSubActionFlag bit 0 is carrying; bit 6 means airborne. Releasing
    # run throws the actor, so walking is not an equivalent precision input.
    # Only counter-steering is eligible: run/walk use the same airborne brake
    # acceleration there. Preserve deliberate releases for shooting and other
    # maneuvers. actionFlag bit 7 means the actor has already been released.
    return bool(landing_brake and not me['dead']
                and not me.get('contact', {}).get('tileGround', True)
                and raw.get('currentPowerupRaw') in (0, 1)
                and raw.get('subActionFlagRaw', 0) & 1
                and not raw.get('actionFlagRaw', 0) & 0x80
                and raw.get('physicsFlagRaw') in (0, 2, 128, 130)
                and not raw.get('damageStateRaw', 1)
                and not raw.get('updateLockedRaw', 1)
                and direction*me['vel']['x'] < 0
                and previous & 2048 and not held & 2048)
