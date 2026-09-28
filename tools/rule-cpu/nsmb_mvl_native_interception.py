"""Optional compiled search; source-validated, with the Python path as fallback."""
import ctypes as C
import hashlib
import json
import math
import os
from pathlib import Path

from nsmb_mvl_grass_navigation import GrassNavigator
from nsmb_mvl_rule_match import solid

ROOT=Path(__file__).resolve().parent


class State(C.Structure):
    _fields_=[(k,C.c_double) for k in ('x','depth','vx','vy')]+[(k,C.c_int) for k in ('grounded','facing','turn','edge')]


class Point(C.Structure):
    _fields_=[('x',C.c_double),('depth',C.c_double)]


def _load():
    if os.environ.get('NSMB_RULE_NATIVE_INTERCEPTION')=='0':return None
    root=ROOT/'native'
    try:
        manifest=json.loads((root/'manifest.json').read_text(encoding='utf8'))
        if manifest.get('abi')!=1:return None
        if any(hashlib.sha256((ROOT/p).read_bytes()).hexdigest()!=sha for p,sha in manifest['sources'].items()):return None
        dll=root/'nsmb_interception.dll'
        if hashlib.sha256(dll.read_bytes()).hexdigest()!=manifest['dll_sha256']:return None
        lib=C.CDLL(str(dll))
        lib.choose_interception.argtypes=[C.POINTER(State),C.c_int,C.c_int,C.POINTER(C.c_uint8),C.c_int,C.c_int,
            C.POINTER(C.c_int),C.c_int,C.c_int,C.POINTER(C.c_int),C.POINTER(C.c_int),
            C.POINTER(Point),C.POINTER(C.c_uint8),C.c_int,C.POINTER(Point),C.POINTER(C.c_int),C.c_int,C.POINTER(C.c_int)]
        lib.choose_interception.restype=C.c_int
        return lib
    except (OSError,ValueError,KeyError):return None


LIBRARY=_load()  # Before the live worker's READY; no DLL load on a gameplay frame.


def select_interception(initial,previous,occupied,items,enemies,candidates,mode):
    """None means unsupported; (index, pickup) also represents no valid plan (-1)."""
    if LIBRARY is None or getattr(occupied,'__func__',None) is not GrassNavigator.occupied:return None
    horizon=len(items)
    values=[initial[k] for k in ('x','depth','vx','vy')]
    if not 0<horizon<=512 or not candidates or not all(math.isfinite(v) and abs(v)<1e6 for v in values):return None
    nav=occupied.__self__
    cells=[key for key,mask in nav.tiles.items() if solid(mask)]
    if any(not isinstance(x,int) or not isinstance(y,int) or not 0<=x<64 for x,y in cells):return None
    top=min((y for x,y in cells),default=0)
    rows=max((y for x,y in cells),default=0)-top+1
    if not 0<rows<=4096:return None
    grid=(C.c_uint8*(rows*64))()
    for x,y in cells:grid[(y-top)*64+x]=1
    stride=max(len(c['inputs']) for c in candidates)
    if stride<horizon:return None
    inputs=(C.c_int*(len(candidates)*stride))()
    for i,candidate in enumerate(candidates):
        for j,held in enumerate(candidate['inputs']):inputs[i*stride+j]=held
    preference=(C.c_int*len(candidates))(*(not bool(c['held']&2048) for c in candidates))
    duration=(C.c_int*len(candidates))(*(c['duration'] for c in candidates))
    item_points=(Point*horizon)(*(Point(p['x'],p['depth']) for p in items))
    collectable=(C.c_uint8*horizon)(*(p['collectable'] for p in items))
    enemy_points=(Point*(len(enemies)*horizon))()
    for i,path in enumerate(enemies):
        for j,p in enumerate(path[:horizon]):enemy_points[i*horizon+j]=Point(p['x'],p['depth'])
    lengths=(C.c_int*len(enemies))(*(len(p) for p in enemies))
    state=State(*values,1,initial['facing'],0,0)
    pickup=C.c_int()
    index=LIBRARY.choose_interception(C.byref(state),previous,mode,grid,top,rows,inputs,len(candidates),stride,
        preference,duration,item_points,collectable,horizon,enemy_points,lengths,len(enemies),C.byref(pickup))
    return index,pickup.value
