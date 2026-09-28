"""Normal jump approximations shared by the CPU motion planners."""
def vertical_step(velocity, jump_held):
    # US overlay 10: Player::updateTallJumpGravity / updateJumpFallGravity,
    # normal jumpCurveAccelTables row. Excludes mini/mega and special states.
    if jump_held and velocity > 2.5:
        acceleration = -0.0625
    elif (jump_held and velocity > 1.5) or -2 < velocity < 0:
        acceleration = -0.25
    else:
        acceleration = -0.34375
    if velocity < -4.0:
        return min(-4.0, velocity - acceleration)
    return max(-4.0, velocity + acceleration)


def horizontal_run_step(velocity, direction):
    """Normal free-air Y+direction approximation, measured from human replay."""
    along = velocity * direction
    acceleration = (.0703125 if along < .5 else .04296875 if along < 1.5
                    else .03125 if along < 2.25 else .0234375)
    return direction * min(3.0, along + acceleration)


def horizontal_walk_step(velocity, direction):
    """Normal free-air direction without Y; existing run momentum is retained."""
    if not direction:
        return velocity
    along = velocity * direction
    if along >= 1.5:
        return velocity
    return direction * min(1.5, along + (.0703125 if along < .5 else .03515625))


