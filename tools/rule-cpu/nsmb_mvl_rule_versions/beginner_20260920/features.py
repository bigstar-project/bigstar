def wrapped_dx(x, origin):
    width = 1024 * 4096
    return (x - origin + width // 2) % width - width // 2
