"""Public strategy API; this is the only arena code mounted in a policy sandbox.

Implement act(observation, state) -> (actions, new_state).
Actions are keyed by robot slot, e.g. {"1": action(...)}. In independent modes
only your own controlled_ids slot is accepted. State is private and JSON serializable.
"""
import math
import json
from pathlib import Path
from typing import Any, Literal, TypedDict


class RobotState(TypedDict, total=False):
    id: str
    slot: int
    model_slot: int
    team: str
    role: str
    x: float
    y: float
    z: float
    vx: float
    vy: float
    hp: float
    max_hp: float
    ammo: int
    aim: float
    pitch: float
    heading: float
    quaternion: list[float]
    heat: float
    energy: float
    alive: bool
    upright: float
    tipped: bool
    powered: bool
    legs: list[float]
    arm_mode: str
    arm_joints: list[float]
    arm_tip: list[float]
    rescue_contact: bool


class Action(TypedDict, total=False):
    move: list[float]
    aim: float | None
    pitch: float
    fire: bool
    power: float
    altitude: float
    profile: str | dict[str, str]
    buy_ammo: dict[str, Any]
    remote_heal: bool
    revive: bool
    deploy: bool
    support: bool
    pick_unit: bool
    assembly: int
    assembly_insert: bool
    assembly_translate: bool
    assembly_rotate: float
    assembly_rotate2: float
    assembly_confirm: bool
    rune_start: bool
    heading: float
    crouch: bool
    rescue: int | str
    yaw_rate: float
    brake: bool
    leg_height: float
    jump: float
    lean: float
    arm: dict[str, Any]
    messages: list[dict[str, Any]]
    # RMUC 2027
    install_launcher: bool
    assembly_core: int
    assembly_lift: bool
    assembly_flip: bool
    posture: str
    posture_enhanced: bool
    drones: dict[str, dict[str, Any]]


class _TeamInbox(TypedDict, total=False):
    # Team mode aggregates all controlled inboxes; single-robot modes keep one inbox.
    messages: list[dict[str, Any]]
    message_inboxes: dict[str,list[dict[str,Any]]]


class Observation(_TeamInbox):
    api_version: int
    seed: int
    tick: int
    time: float
    dt: float
    policy_dt: float
    remaining: float
    team: str
    controlled_ids: list[str]
    robots: list[RobotState]
    enemies: list[RobotState]
    vision: dict[str,dict[str,Any]]
    arena: dict[str, Any]
    bases: dict[str, Any]
    teams: dict[str, Any]
    zones: dict[str, Any]
    projectiles: list[dict[str, Any]]
    specs: dict[str, Any]
    referee: dict[str, Any]

API_VERSION = 3
TEAM_SLOTS = (1, 2, 3, 4, 6, 7)
TEAM_SIZE = 6
RULES_PROFILE = "rmuc-2026-v1.0.0-arena-v3"
# RMUC 2027: heavy 1, infantry 3 and 4, aerial system 6, sentry 7.
TEAM_SLOTS_2027 = (1, 3, 4, 6, 7)
TEAM_SIZE_2027 = 5
RULES_PROFILE_2027 = "rmuc-2027-v1.0.0-arena-v1"

def _load_rules():
    """Rules and reference field of every season available to this runtime."""
    here = Path(__file__).resolve().parent
    for file in (here / "rules.json", here.parent / "config/rules.json"):
        if file.is_file():
            bundle=json.loads(file.read_text(encoding="utf-8"))
            seasons={'2026':(bundle["rulebook"],bundle["maps"]["rmuc2026"])}
            if 'rulebook_2027' in bundle and 'rmuc2027' in bundle['maps']:
                seasons['2027']=(bundle['rulebook_2027'],bundle['maps']['rmuc2027'])
            return seasons,bundle.get('robot_designs',{})
    file = here / "arena.json"
    if file.is_file():
        bundle=json.loads(file.read_text(encoding="utf-8"))
        return {bundle.get('season','2026'):(bundle["rulebook_rules"],bundle["map"])},bundle.get('robot_designs',{})
    raise RuntimeError("Public rules reference missing from SDK runtime")

_SEASONS, DESIGNS = _load_rules()
RULES_BY_SEASON = {season: value[0] for season, value in _SEASONS.items()}
ARENAS_BY_SEASON = {season: value[1] for season, value in _SEASONS.items()}
# 2026 strategies read RULES; a 2027 workspace (or the full runtime) also has RULES_2027.
RULES = RULES_BY_SEASON.get('2026') or next(iter(RULES_BY_SEASON.values()))
RULES_2027 = RULES_BY_SEASON.get('2027')
_REFERENCE_ARENA = ARENAS_BY_SEASON.get('2026') or next(iter(ARENAS_BY_SEASON.values()))


def season_of(observation):
    """'2027' or '2026': the rules season of this match."""
    return observation.get('season') or observation.get('arena', {}).get('season') or '2026'


def rules(observation):
    """The full public rules of the observation's season."""
    return RULES_BY_SEASON[season_of(observation)]


def action(move=(0.0, 0.0), aim=None, fire=False, skill=False, pitch=0.04, upgrade=None, **operations):
    """Game action. Move is world x/y; aerial may also supply z intent.

    Additional operations: profile, power, altitude, buy_ammo, remote_heal,
    revive, deploy, support, pick_unit, assembly, assembly_insert,
    assembly_translate, assembly_rotate, assembly_rotate2, assembly_confirm, rune_start, rescue,
    yaw_rate, brake, leg_height, jump, climb, lean, arm, messages (all control modes).
    rescue=<own slot> makes the engineer grip and right that tipped robot;
    arm={"tip":[x,y,z],"dir":[dx,dy,dz],"frame":"world"|"robot"} or
    {"joints":[7 values]} or {"stow":True} drives the engineer arm directly.
    The profile is selectable only on the first decision (tick 0).
    """
    if skill or upgrade is not None:
        raise ValueError("Unsupported action; use the documented game operations")
    allowed = {"profile", "power", "altitude", "buy_ammo", "remote_heal", "revive", "deploy", "support", "pick_unit", "assembly", "assembly_insert", "assembly_translate", "assembly_rotate", "assembly_confirm", "rune_start", "heading", "crouch", "rescue", "yaw_rate", "brake", "leg_height", "jump", "lean", "arm", "arm2", "pickup_arm", "transform", "messages",
               "install_launcher", "assembly_core", "assembly_lift", "assembly_flip", "assembly_rotate2", "assembly_extract", "assembly_cancel", "posture", "posture_enhanced", "drones", "climb"}
    if not set(operations) <= allowed:
        raise ValueError("Unknown action operation")
    # A move from `navigate` that backs a chassis off the road bumps keeps its heading.
    if getattr(move, "heading", None) is not None and not {"heading", "yaw_rate"} & set(operations):
        operations["heading"] = move.heading
    return {"move": list(move), "aim": aim, "pitch": pitch, "fire": fire, **operations}


def message(data, to="all"):
    """Build a team message: a teammate instance or broadcast in any control mode.

    Pass it via action(messages=[message(...)]) and read obs['messages'] on
    the next decision; Team mode can read obs['message_inboxes'][slot]. Each payload is JSON, <=512 bytes; at most four sends
    per decision, still within the robot action's total 1 KiB budget. In 2027 the live cap falls with the
    team's logistics coefficient λ; `obs['logistics_effects']['interaction_bandwidth_bytes']` is the
    effective limit after base damage, and the host drops payloads above it (the 512-byte protocol
    ceiling itself remains a hard error).
    """
    recipient = str(to)
    if recipient != "all" and not (recipient.isascii() and recipient.isdigit() and 0 < int(recipient) <= 2**32-1):
        raise ValueError("Message recipient must be all or an own robot slot")
    if len(json.dumps(data, ensure_ascii=False, allow_nan=False, separators=(",", ":")).encode()) > 512:
        raise ValueError("Message data must be <=512 bytes")
    return {"to": recipient, "data": data}


def referee_info(observation: Observation) -> dict[str, Any]:
    """Read the referee snapshot delivered with this decision, without network I/O.

    Both teams' health is public. Enemy position, aim, ammunition, heat,
    economy, source code and private strategy state are not exposed here.
    Event IDs can be retained in strategy state to deduplicate broadcasts.
    """
    return observation["referee"]


def _design(robot):
    return DESIGNS.get(str(robot.get("model_slot") or robot.get("slot")), {})


def is_42(robot):
    """True for 42 mm launchers (2026 hero, 2027 heavy robot)."""
    return robot["role"] in ("hero", "heavy")


def muzzle_speed(robot):
    if robot["role"] == "heavy":
        return _design(robot).get("muzzle_speed_mps", 15.6)
    if robot["role"] == "hero":
        return 16.5 if robot.get("deployed") else 12.0
    return _design(robot).get("muzzle_speed_mps", 25.0)


def aim_height(target):
    if "aim_z" in target:
        return target["aim_z"]
    if target.get("kind") == "rune_target":
        return target["z"]
    if "armor_open" in target:
        return target.get("z",0) + ((1.05 if RULES.get('terrain_geometry',{}).get('chapter4_dimensions') else .8) if target.get("kind") == "outpost" else .225 if target["armor_open"] else 1.043)
    return target.get("z", 0) + target.get('armor_center_z_m',.50)


def _rotate(q,vector):
    x,y,z,w=q;vx,vy,vz=vector
    tx=2*(y*vz-z*vy);ty=2*(z*vx-x*vz);tz=2*(x*vy-y*vx)
    return (vx+w*tx+y*tz-z*ty,vy+w*ty+z*tx-x*tz,vz+w*tz+x*ty-y*tx)


def _aim_target(robot,target):
    if 'aim_z' in target:
        return (target['x'],target['y'],target['aim_z'])
    if target.get('armor_plates'):
        candidates=[p for p in target['armor_plates'] if not p['heavy_only'] or is_42(robot)]
        if target.get('armor_open') and target.get('kind')!='outpost':
            lower=[p for p in candidates if p.get('weakspot')]
            if lower:candidates=lower
        def score(plate):
            point=plate['center'];normal=plate['normal']
            delta=(robot['x']-point[0],robot['y']-point[1],robot.get('z',0)+robot.get('muzzle_height_m',.45)-point[2])
            length=max(.001,math.sqrt(sum(v*v for v in delta)))
            return sum(a*b for a,b in zip(normal,delta))/length
        if candidates:return tuple(max(candidates,key=score)['center'])
    if target.get('chassis_size_m') and target.get('quaternion') and target.get('role')!='aerial':
        size=target['chassis_size_m'];q=target['quaternion'];best=None
        for face in range(4):
            angle=face*math.pi/2;nx,ny=math.cos(angle),math.sin(angle)
            distance_=(size[0] if face%2==0 else size[1])/2-.032
            height=target.get('armor_center_z_m',.265)
            if face%2:height=target.get('armor_side_center_z_m',height)
            offset=_rotate(q,(nx*distance_,ny*distance_,height))
            point=(target['x']+offset[0],target['y']+offset[1],target.get('z',0)+offset[2])
            normal=_rotate(q,(nx*math.cos(math.pi/12),ny*math.cos(math.pi/12),math.sin(math.pi/12)))
            difference=(robot['x']-point[0],robot['y']-point[1],robot.get('z',0)+robot.get('muzzle_height_m',.45)-point[2])
            score=sum(a*b for a,b in zip(normal,difference))/max(.001,math.sqrt(sum(v*v for v in difference)))
            if best is None or score>best[0]:best=(score,point)
        return best[1]
    return (target['x'],target['y'],aim_height(target))


_AIM_CACHE={}
def _ballistic_solution(robot,target,speed=None):
    speed=speed or muzzle_speed(robot);point=_aim_target(robot,target)
    dx,dy=point[0]-robot['x'],point[1]-robot['y'];distance_=max(.01,math.hypot(dx,dy));ux,uy=dx/distance_,dy/distance_
    inherited=robot.get('vx',0)*ux+robot.get('vy',0)*uy
    pivot=_rotate(robot.get('quaternion',(0,0,0,1)),(0,0,robot.get('muzzle_height_m',.25 if robot['role']=='aerial' else .55)))
    base_z=robot.get('z',0)+pivot[2];reach=robot.get('muzzle_reach_m',.3)
    key=tuple(round(v,3) for v in (distance_,point[2]-base_z,speed,inherited,robot.get('vz',0),reach))+(is_42(robot),)
    if key in _AIM_CACHE:return _AIM_CACHE[key]
    radius=.02125 if is_42(robot) else .0084;mass=.0445 if is_42(robot) else .0032
    drag=.5*1.225*.47*math.pi*radius*radius/mass;dt=1/120
    def height(angle):
        x=reach*math.cos(angle);z=base_z+reach*math.sin(angle);vx=speed*math.cos(angle)+inherited;vz=speed*math.sin(angle)+robot.get('vz',0)
        for _ in range(540):
            v=math.hypot(vx,vz);ax=-drag*v*vx;az=-9.81-drag*v*vz
            xn=x+vx*dt+.5*ax*dt*dt;zn=z+vz*dt+.5*az*dt*dt
            if xn>=distance_:return z+(zn-z)*(distance_-x)/max(1e-8,xn-x)
            x,z=xn,zn;vx+=ax*dt;vz+=az*dt
            if vx<=0 or z<base_z-8:return None
        return None
    low,high=-math.pi/12,math.pi/4;h=height(high)
    reachable=h is not None and h>=point[2]
    if reachable:
        for _ in range(13):
            mid=(low+high)/2;value=height(mid)
            if value is None or value<point[2]:low=mid
            else:high=mid
    result=((low+high)/2 if reachable else math.pi/4,reachable)
    if len(_AIM_CACHE)>=256:_AIM_CACHE.pop(next(iter(_AIM_CACHE)))
    _AIM_CACHE[key]=result
    return result


def aim_pitch(robot,target,speed=None):
    """Lower ballistic arc with drag, muzzle geometry and inherited velocity."""
    return _ballistic_solution(robot,target,speed)[0]


_LOB_CACHE={}
def lob_aim(robot,point,speed=None):
    """Yaw and elevation that drop the shell onto a world point on the *flat* arc.

    "吊射" is precise flat-arc gunnery, not a mortar: the launcher keeps its normal forward
    elevation and the only thing that makes the crosshair the impact point is the ballistic solve —
    the shell leaves slightly up, falls back onto the point under the reticle, and never climbs into
    a near-vertical lob. The 42 mm shell is slow enough that the drop is real at ten metres, so the
    solve carries the same drag and inherited velocity the engine integrates. `None` when the point
    is beyond the flat arc — the low branch cannot reach it.
    """
    dx,dy=point[0]-robot['x'],point[1]-robot['y'];distance_=max(.01,math.hypot(dx,dy))
    yaw=math.atan2(dy,dx)
    # `aim_z` makes the solver take the point literally instead of lifting it to an armour centre.
    target={"x":point[0],"y":point[1],"aim_z":point[2]}
    pitch,reachable=_ballistic_solution(robot,target,speed or muzzle_speed(robot))
    return (yaw,pitch) if reachable else None


def ballistic_reachable(robot,target):
    return _ballistic_solution(robot,target)[1]


def can_fire(robot):
    if 0.0<robot.get('transform_progress',0.0)<1.0:return False
    if robot["role"]=="engineer" or robot.get("drones") is not None and robot["role"]=="aerial":
        return False
    # The 2027 heavy robot fires only with its modular launcher on board.
    if robot["role"]=="heavy" and robot.get("launcher") not in ("fitted","installed"):
        return False
    if not robot["alive"] or robot.get("weak") or robot.get("overheated") or robot.get("heat_permanent_lock") or robot.get("overspeed_locked") or robot.get("overspeed_permanent_lock") or robot.get("launcher_fault") or robot.get("red_card"):
        return False
    heat = 100 if is_42(robot) else 10
    if robot.get("posture")=="attack" and robot.get("posture_enhanced"):heat=0
    return robot["ammo"]>0 and robot["heat"]+heat<=robot["max_heat"]+1e-8


def remote_heal_cost(elapsed_seconds):
    return 50 + math.ceil(elapsed_seconds/60*20)


def instant_revive_cost(robot, elapsed_seconds):
    return math.ceil(elapsed_seconds/60)*80 + robot["level"]*20


def intercept(robot, target, speed=None):
    """Lead a moving visible target; strategies can improve this prediction."""
    speed=speed or muzzle_speed(robot)
    time=distance(robot,target)/speed
    point=dict(target)
    point["x"] += target.get("vx",0)*time
    point["y"] += target.get("vy",0)*time
    return point


def tipped_teammates(observation):
    """Own ground robots lying on a side or roof (body up-axis below 60 degrees) that need
    an engineer. Serial-leg robots (``hip_angle`` present) right themselves while powered."""
    return [r for r in observation['robots'] if r.get('tipped')
            and not ('hip_angle' in r and r.get('alive') and r.get('powered', True))]


def rescue_point(engineer, target, standoff=.62):
    """Where the engineer should stop to grip a tipped robot: on the line from the
    robot toward the engineer, close enough for the arm (about 0.85 m tool reach)."""
    dx,dy=engineer['x']-target['x'],engineer['y']-target['y'];d=math.hypot(dx,dy)
    if d<1e-6:dx,dy,d=1.0,0.0,1.0
    return (target['x']+dx/d*standoff,target['y']+dy/d*standoff)


def sight_block_point(engineer, viewer, protected, height=None):
    """Arm tip that blocks `viewer`'s camera line to `protected` (both robot dicts):
    the point on that line nearest the engineer's mast, for action(arm={'tip': ...})."""
    eye=(viewer['x'],viewer['y'],viewer['z']+viewer.get('muzzle_height_m',.45)+.08)
    goal=(protected['x'],protected['y'],protected['z']+(height if height is not None else protected.get('armor_center_z_m',.3)))
    d=[g-e for g,e in zip(goal,eye)];length=sum(v*v for v in d) or 1e-9
    t=((engineer['x']-eye[0])*d[0]+(engineer['y']-eye[1])*d[1])/(d[0]*d[0]+d[1]*d[1] or 1e-9)
    t=max(0.0,min(1.0,t))
    return [eye[k]+d[k]*t for k in range(3)]


def fly_slope_lane(arena, team):
    """Fly-slope geometry at the end of `team`'s highway: lip (low end, flush with the
    highway) and crest of the 17 degree ramp, which climbs toward the opponent's half,
    and the landing pad across the 650 mm gap. Two-wheel-legged robots can also jump
    the gap in reverse, from the landing pad up onto the crest."""
    ramp=next(t for t in arena['terrain'] if t.get('id')==team+'_fly_slope')
    landing=next(t for t in arena['terrain'] if t.get('id')==team+'_fly_landing')
    xs=[p[0] for p in landing['polygon']];ys=[p[1] for p in landing['polygon']]
    return {'lip':ramp['ramp_start'],'crest':ramp['ramp_end'],'lip_height':ramp['base_elevation'],
            'crest_height':ramp['elevation'],'landing':[sum(xs)/len(xs),sum(ys)/len(ys)],'landing_height':landing['elevation']}


def toward(robot, point):
    dx, dy = point[0] - robot["x"], point[1] - robot["y"]
    distance = math.hypot(dx, dy)
    return (dx / distance, dy / distance) if distance > 0.05 else (0.0, 0.0)


def aim_at(robot, target):
    point=_aim_target(robot,target);dx,dy=point[0]-robot['x'],point[1]-robot['y'];time=math.hypot(dx,dy)/muzzle_speed(robot)
    return math.atan2(dy-robot.get('vy',0)*time,dx-robot.get('vx',0)*time)


def distance(a, b):
    return math.hypot(a["x"] - b["x"], a["y"] - b["y"])


def nearest_enemy(robot, observation):
    enemies = [r for r in observation["enemies"] if r["alive"] and r.get("targetable",r.get("role")!="aerial")]
    return min(enemies, key=lambda r: (distance(robot, r), r["id"]), default=None)


def navigate(robot, point, arena):
    """Small obstacle-aware helper; strategies can replace it with pathfinding.

    Follow the cheapest collision-free waypoint graph around expanded wall
    corners. The public geometry is identical for every strategy. Pass the
    result to `action`: a move backing a chassis off the road bumps also holds
    its heading there.
    """
    import heapq

    if robot["role"] == "aerial":
        if robot.get("drones") is not None:
            return (0.0, 0.0)  # a 2027 hangar is fixed; steer its aerial robots with `drones`
        lane=arena["flight_lanes"][robot["team"]]
        return _physical_toward(robot,(max(lane["min_x"],min(point[0],lane["max_x"])),lane["center_y"]))
    if arena.get("layout_revision"):
        return _chapter4_navigate(robot,point,arena)
    margin = robot.get("radius", 0.28) + 0.08
    for platform in arena.get("terrain", []):
        if platform.get("kind") != "platform":
            continue
        inside_goal = platform["x"] <= point[0] <= platform["x"] + platform["w"] and platform["y"] <= point[1] <= platform["y"] + platform["h"]
        inside_robot = platform["x"] <= robot["x"] <= platform["x"] + platform["w"] and platform["y"] <= robot["y"] <= platform["y"] + platform["h"]
        if inside_goal and not inside_robot:
            ramps = [t for t in arena["terrain"] if t.get("kind") == "ramp" and abs(t.get("elevation", 0) - platform["elevation"]) < .01]
            if ramps:
                ramp = min(ramps, key=lambda t: math.hypot(robot["x"] - (t["x"] + t["w"] / 2), robot["y"] - (t["y"] + t["h"] / 2)))
                in_ramp = ramp["x"] - .1 <= robot["x"] <= ramp["x"] + ramp["w"] + .1 and ramp["y"] <= robot["y"] <= ramp["y"] + ramp["h"]
                if not in_ramp:
                    point = (ramp["x"] + .1 if ramp["direction"] > 0 else ramp["x"] + ramp["w"] - .1, max(ramp["y"] + margin, min(point[1], ramp["y"] + ramp["h"] - margin)))
    obstacles=list(arena["obstacles"])
    for zone in arena.get("forbidden",[]):
        if zone["kind"] in ("base","pad","highway","dart_station") or zone.get("owner") not in (None,robot["team"]):
            obstacles.append(zone)
    walls = [{"x": w["x"] - margin, "y": w["y"] - margin,
              "w": w["w"] + margin * 2, "h": w["h"] + margin * 2}
             for w in obstacles]

    def clear(a, b):
        for w in walls:
            lo, hi = 0.0, 1.0
            for p, d, low, high in ((a[0], b[0] - a[0], w["x"], w["x"] + w["w"]),
                                    (a[1], b[1] - a[1], w["y"], w["y"] + w["h"])):
                if abs(d) < 1e-9:
                    if not low < p < high:
                        lo = 2
                        break
                else:
                    aa, bb = sorted(((low - p) / d, (high - p) / d))
                    lo, hi = max(lo, aa), min(hi, bb)
            if lo < hi and hi > 0 and lo < 1:
                return False
        return True

    start, goal = (robot["x"], robot["y"]), tuple(point)
    if clear(start, goal):
        return toward(robot, goal)
    nodes = [start, goal]
    for w in walls:
        for x in (w["x"] - 0.03, w["x"] + w["w"] + 0.03):
            for y in (w["y"] - 0.03, w["y"] + w["h"] + 0.03):
                if margin < x < arena["width"] - margin and margin < y < arena["height"] - margin:
                    nodes.append((x, y))
    queue, best = [(0.0, 0, [])], {0: 0.0}
    while queue:
        cost, node, path = heapq.heappop(queue)
        if cost > best[node]:
            continue
        if node == 1:
            return toward(robot, nodes[path[0]])
        for i, candidate in enumerate(nodes):
            if i == node or not clear(nodes[node], candidate):
                continue
            distance_ = math.dist(nodes[node], candidate)
            new_cost = cost + distance_
            if new_cost < best.get(i, float("inf")):
                best[i] = new_cost
                heapq.heappush(queue, (new_cost, i, path + [i]))
    return (0.0, 0.0)


def point_in_polygon(x, y, points):
    inside=False
    for a,b in zip(points,points[1:]+points[:1]):
        cross=(x-a[0])*(b[1]-a[1])-(y-a[1])*(b[0]-a[0])
        if abs(cross)<1e-8 and min(a[0],b[0])-1e-8<=x<=max(a[0],b[0])+1e-8 and min(a[1],b[1])-1e-8<=y<=max(a[1],b[1])+1e-8:return True
        if (a[1]>y)!=(b[1]>y) and x<(b[0]-a[0])*(y-a[1])/(b[1]-a[1])+a[0]:inside=not inside
    return inside


def _terrain_surface(t,x,y):
    if t['kind']=='ramp' and t.get('ramp_start'):
        a,b=t['ramp_start'],t['ramp_end'];dx,dy=b[0]-a[0],b[1]-a[1]
        p=max(0,min(1,((x-a[0])*dx+(y-a[1])*dy)/(dx*dx+dy*dy)))
        return t.get('base_elevation',0)+(t['elevation']-t.get('base_elevation',0))*p
    if t['kind']=='bumps':
        p=((x-t['x'])/t['bump_pitch'])%1
        return t['elevation']+t['bump_height']*(1-math.cos(math.tau*p))/2
    if t.get('edge_ramp_m',0):
        points=t['polygon']
        d=min(abs((x-a[0])*(b[1]-a[1])-(y-a[1])*(b[0]-a[0]))/math.dist(a,b) for a,b in zip(points,points[1:]+points[:1]))
        return t.get('base_elevation',0)+(t['elevation']-t.get('base_elevation',0))*min(1,d/t['edge_ramp_m'])
    return t['elevation']


def terrain_height(arena,x,y):
    """Ground datum for chapter 4 polygons, sloped faces and road bumps."""
    value=0
    for t in arena['terrain']:
        if t['x']<=x<=t['x']+t['w'] and t['y']<=y<=t['y']+t['h'] and (not t.get('polygon') or point_in_polygon(x,y,t['polygon'])):
            value=max(value,_terrain_surface(t,x,y))
    return value+max(0,min(y,arena['height']-y))*math.tan(math.radians(arena.get('floor_slope_deg',0)))


_NAV_GRAPHS={}


def _prepare_chapter4(arena,team,folded=True):
    # Roofs are overhead and do not block the route, except that in 2027 only a folded robot
    # fits under the 250-260 mm tunnel roofs; others route around the tunnels.
    under=folded or arena.get('season')!='2027'
    key=(arena['layout_revision'],team,under)
    if key in _NAV_GRAPHS:return _NAV_GRAPHS[key]
    margin=.36
    walls=[w for w in arena['obstacles'] if 'roof' not in w.get('id','') or (not under and '_tunnel_roof' in w['id'])]
    # The highway no-go area behind a fly slope is entered only by flying the ramp (R58).
    walls += [w for w in arena['forbidden'] if w['kind'] in ('base','pad','highway','dart_station') or w.get('owner') not in (None,team)]
    boxes=[]
    for w in walls:
        a=w.get('rotation',0);boxes.append((w['x']+w['w']/2,w['y']+w['h']/2,w['w']/2+margin,w['h']/2+margin,math.cos(a),math.sin(a)))
    # RMUC 2027 outposts stand on the central deck beside its kerb: route around their pedestals.
    if arena.get('season')=='2027':
        for x,y in arena['outposts'].values():boxes.append((x,y,.34+margin,.34+margin,1.0,0.0))
    # A small spatial index keeps height checks bounded during decision calls.
    tiles={}
    for t in arena['terrain']:
        for x in range(math.floor(t['x']),math.ceil(t['x']+t['w'])+1):
            for y in range(math.floor(t['y']),math.ceil(t['y']+t['h'])+1):tiles.setdefault((x,y),[]).append(t)
    # Terrain height is asked for again and again along the same and the mirrored sample
    # lines (a strict check also walks both footprint edges), so one decoded cell feeds
    # every caller. 2 cm buckets are far below the 13 cm slope threshold.
    heights={}
    def height(x,y):
        key=(round(x*50),round(y*50))
        cached=heights.get(key)
        if cached is not None:return cached
        h=0
        for t in tiles.get((math.floor(x),math.floor(y)),[]):
            if t['x']-1e-8<=x<=t['x']+t['w']+1e-8 and t['y']-1e-8<=y<=t['y']+t['h']+1e-8 and point_in_polygon(x,y,t['polygon']):h=max(h,_terrain_surface(t,x,y))
        h+=min(y,15-y)*math.tan(math.radians(1.5))
        if len(heights)>=32768:heights.clear()
        heights[key]=h
        return h
    # A lattice of box indices: a segment only ever meets the boxes in the cells its own bounding
    # box covers, which turns the per-test scan from every wall on the field into a handful. Boxes
    # are registered in every cell they touch, so the filter is exact. A 1.5 m cell keeps only a
    # couple of walls per visited cell; coarser cells forced every test to walk hundreds of
    # irrelevant boxes and were the single hottest cost of a decision.
    CELL=1.5
    lattice={}
    for k,(cx,cy,hw,hh,c,s) in enumerate(boxes):
        ex,ey=abs(hw*c)+abs(hh*s),abs(hw*s)+abs(hh*c)
        for gx in range(math.floor((cx-ex)/CELL),math.floor((cx+ex)/CELL)+1):
            for gy in range(math.floor((cy-ey)/CELL),math.floor((cy+ey)/CELL)+1):
                lattice.setdefault((gx,gy),[]).append(k)
    def near_boxes(x0,y0,x1,y1):
        """Boxes in the cells the segment actually crosses (a supercover walk, so a box that
        touches the line at a cell corner is still returned). Bounding-box cell gathering
        cost ten times more cells on the long diagonal routes."""
        found=set()
        gx,gy=math.floor(x0/CELL),math.floor(y0/CELL)
        ex,ey=math.floor(x1/CELL),math.floor(y1/CELL)
        dx,dy=x1-x0,y1-y0
        sx=1 if dx>0 else -1;sy=1 if dy>0 else -1
        if dx:
            tx=(((gx+1)*CELL if dx>0 else gx*CELL)-x0)/dx;dtx=CELL/abs(dx)
        else:
            tx=math.inf;dtx=math.inf
        if dy:
            ty=(((gy+1)*CELL if dy>0 else gy*CELL)-y0)/dy;dty=CELL/abs(dy)
        else:
            ty=math.inf;dty=math.inf
        for _ in range(4096):
            found.update(lattice.get((gx,gy),()))
            if gx==ex and gy==ey:break
            if tx<ty:
                gx+=sx;tx+=dtx
            elif ty<tx:
                gy+=sy;ty+=dty
            else:
                # Through a corner: the line touches all three cells around it.
                found.update(lattice.get((gx+sx,gy),()));found.update(lattice.get((gx,gy+sy),()))
                gx+=sx;gy+=sy;tx+=dtx;ty+=dty
        return found
    def inside(point,keep=False):
        """Inflated boxes containing a point; a robot pushed into one must still be able to leave.

        With `keep`, each box maps to the point's clearance from the bare obstacle (None
        when the point is within it), so `clear` lets the robot leave or pass that obstacle
        without closing in on it."""
        found={}
        for k in lattice.get((math.floor(point[0]/CELL),math.floor(point[1]/CELL)),()):
            cx,cy,hw,hh,c,s=boxes[k]
            ax,ay=(point[0]-cx)*c+(point[1]-cy)*s,-(point[0]-cx)*s+(point[1]-cy)*c
            if abs(ax)<hw and abs(ay)<hh:
                gap=max(abs(ax)-hw,abs(ay)-hh)+margin
                found[k]=gap-.01 if gap>.01 else None
        return found if keep else set(found)
    def clear(a,b,ignore=(),strict=True):
        dx,dy=b[0]-a[0],b[1]-a[1]
        for k in near_boxes(a[0],a[1],b[0],b[1]):
            cx,cy,hw,hh,c,s=boxes[k]
            if k in ignore:
                kept=ignore[k] if isinstance(ignore,dict) else None
                if kept is None:continue
                hw,hh=hw-margin+kept,hh-margin+kept
            ax,ay=(a[0]-cx)*c+(a[1]-cy)*s,-(a[0]-cx)*s+(a[1]-cy)*c
            vx,vy=dx*c+dy*s,-dx*s+dy*c;lo,hi=0,1
            for p,d,half in ((ax,vx,hw),(ay,vy,hh)):
                if abs(d)<1e-9:
                    if not -half<p<half:lo=2;break
                else:
                    aa,bb=sorted(((-half-p)/d,(half-p)/d));lo,hi=max(lo,aa),min(hi,bb)
            if lo<hi and hi>0 and lo<1:return False
        length=math.hypot(dx,dy);count=max(1,math.ceil(length/.2))
        # Strict checks also sample both sides of the footprint, so a route never
        # runs one side of the chassis off a ramp edge or platform cliff.
        side=(-dy/length*margin,dx/length*margin) if strict and length>1e-9 else (0,0)
        lines=((0,0),side,(-side[0],-side[1])) if strict else ((0,0),)
        old=[height(a[0]+ox,a[1]+oy) for ox,oy in lines]
        for i in range(1,count+1):
            px,py=a[0]+dx*i/count,a[1]+dy*i/count
            now=[height(px+ox,py+oy) for ox,oy in lines]
            if max(abs(h-o) for h,o in zip(now,old))>.13 or max(now)-min(now)>.13:return False
            old=now
        return True
    nodes=[tuple(p) for p in arena['navigation']['nodes']]
    # Only short wall-end detours are needed in addition to the public ramp nodes.
    for cx,cy,hw,hh,c,s in boxes:
        if hw>2:continue
        for x in (-hw-.04,hw+.04):
            for y in (-hh-.04,hh+.04):
                p=(cx+x*c-y*s,cy+x*s+y*c)
                if .37<p[0]<27.63 and .37<p[1]<14.63:nodes.append(p)
    edges=[[] for _ in nodes]
    for i,a in enumerate(nodes):
        for j in range(i):
            if math.dist(a,nodes[j])<7 and clear(a,nodes[j]):
                d=math.dist(a,nodes[j]);edges[i].append((j,d));edges[j].append((i,d))
    value=(nodes,edges,clear,inside)
    _NAV_GRAPHS[key]=value
    return value


def _physical_toward(robot,point):
    direction=toward(robot,point);distance_=math.hypot(robot['x']-point[0],robot['y']-point[1])
    speed=robot.get('speed_reference_mps')
    if not speed:return direction
    scale=min(1,distance_*2/speed)
    return (direction[0]*scale,direction[1]*scale)


def _chapter4_navigate(robot,point,arena):
    graph=_prepare_chapter4(arena,robot['team'],robot.get('transform_progress',0.0)>=.99);nodes,_,clear,inside=graph
    start=(robot['x'],robot['y']);goal=tuple(point)
    # A heavy chassis stops with its own motors, which draw on the base power budget: a powered
    # reverse near the goal is far weaker than a stopped command, and arriving at the resource
    # station flat out overran the dispenser into the corner behind it. Let the active brake do
    # the work while there is still room, then creep the last metre in.
    speed=math.hypot(robot.get('vx',0.0),robot.get('vy',0.0))
    if speed>.35 and math.dist(start,goal)<.35+speed*speed/6:return (0.0,0.0)
    # Obstacles the goal overlaps never block reaching it. A robot pushed inside an
    # obstacle's margin may leave or pass it but not close in on it (driving straight on
    # would wedge it against a ramp guard's end); ignoring that obstacle outright, and
    # relaxed checks, are fallbacks for a robot already standing at an edge.
    near_goal=inside(goal);guarded=inside(start,True)
    for strict in (True,False):
        for near_start in ((guarded,set(guarded)) if guarded else (guarded,)):
            ignore={**near_start,**dict.fromkeys(near_goal)} if isinstance(near_start,dict) else near_start|near_goal
            if clear(start,goal,ignore,strict):return _square_to_bumps(robot,goal,arena)
            step=_route(graph,start,goal,near_start,near_goal,strict,.3 if arena.get('season')=='2027' else 0)
            if step is not None:return _square_to_bumps(robot,nodes[step],arena)
    return (0.0,0.0)


class _HeldMove(tuple):
    """A `navigate` move that must not turn the chassis; `action` sends its heading."""
    def __new__(cls,move,heading):
        value=super().__new__(cls,move);value.heading=heading
        return value


def _square_to_bumps(robot,target,arena,lookahead=.75,tolerance=math.radians(12)):
    """Drive toward `target`, but turn on the spot first if the course runs onto road bumps
    within `lookahead` while the chassis does not face it, backing off the humps first if it
    is too close to turn. A chassis that strafes across the humps stalls, and it cannot turn
    once it stands on them. Robots cross the humps at full command: slowing there for a
    waypoint just past them stalls them as well."""
    move=_physical_toward(robot,target);heading=robot.get('heading')
    x,y=robot['x'],robot['y'];dx,dy=target[0]-x,target[1]-y;length=math.hypot(dx,dy)
    if length<1e-6 or not any(move):return move
    for t in arena['terrain']:
        if t['kind']!='bumps':continue
        x0,y0,x1,y1=t['x'],t['y'],t['x']+t['w'],t['y']+t['h']
        if x0<=x<=x1 and y0<=y<=y1:
            return move if x0<=target[0]<=x1 and y0<=target[1]<=y1 else (dx/length,dy/length)
        if heading is None:continue
        lo,hi=0,min(length,lookahead)
        for p,d,a,b in ((x,dx/length,x0,x1),(y,dy/length,y0,y1)):
            if abs(d)<1e-9:
                if not a<=p<=b:lo=math.inf
            else:
                s0,s1=sorted(((a-p)/d,(b-p)/d));lo,hi=max(lo,s0),min(hi,s1)
        if lo>hi:continue
        error=abs((math.atan2(dy,dx)-heading+math.pi)%math.tau-math.pi)
        # A two-wheeled chassis drives either way round; others turn to face the motion.
        if robot.get('chassis_type')=='wheel_legged':error=min(error,math.pi-error)
        if error>tolerance:
            # Turning would push a wheel sideways up the first hump, so a chassis nosed onto
            # it at an angle (a teammate rounding the kerb end beside it yaws it there) is
            # pinned: back straight out, holding its heading, until its turning circle clears.
            ax,ay=x-min(max(x,x0),x1),y-min(max(y,y0),y1);gap=math.hypot(ax,ay)
            if gap<robot.get('radius',0):return _HeldMove((ax/gap*.6,ay/gap*.6),heading)
            # A creeping command still sets the chassis yaw goal, so it turns nearly in place.
            return (dx/length*.02,dy/length*.02)
    return move


_NAV_ROUTE_COSTS={}


def _route_costs(graph,goal,near_goal,strict):
    """Distances from public waypoints to a fixed goal, reused across decisions."""
    import heapq
    nodes,edges,clear,_=graph
    key=(id(graph),clear,goal,frozenset(near_goal),strict)
    if key in _NAV_ROUTE_COSTS:return _NAV_ROUTE_COSTS[key]
    distances={i:math.dist(p,goal) for i,p in enumerate(nodes) if clear(p,goal,near_goal,strict)}
    queue=[(cost,i) for i,cost in distances.items()];heapq.heapify(queue)
    while queue:
        cost,index=heapq.heappop(queue)
        if cost!=distances[index]:continue
        for other,d in edges[index]:
            if cost+d<distances.get(other,float('inf')):
                distances[other]=cost+d;heapq.heappush(queue,(cost+d,other))
    if len(_NAV_ROUTE_COSTS)>=128:_NAV_ROUTE_COSTS.pop(next(iter(_NAV_ROUTE_COSTS)))
    _NAV_ROUTE_COSTS[key]=distances
    return distances


def _route(graph,start,goal,near_start,near_goal,strict,reached=0):
    """First visible waypoint on the cheapest complete route, or None. Waypoints closer
    than `reached` count as passed, so a robot standing on one heads for the next instead
    of creeping on the spot (on a 2027 slope it would slide back)."""
    nodes,_,clear,_=graph
    costs=_route_costs(graph,goal,near_goal,strict)
    full=sorted((math.dist(start,nodes[i])+cost,i) for i,cost in costs.items())
    candidates=full
    if reached:
        ahead=[c for c in full if math.dist(start,nodes[c[1]])>=reached]
        candidates=ahead or full
    # The cost of every complete route is already known; test visibility in
    # increasing cost order instead of scanning every start/goal pair again.
    for _,i in candidates:
        if clear(start,nodes[i],near_start,strict):return i
    # A robot pushed past its goal can stand with the only escape waypoint underfoot: the
    # `reached` skip then hides the one node that leads home and the chassis stops dead in
    # the station corner. Those nodes are still valid steering targets once nothing else is.
    for _,i in full:
        if math.dist(start,nodes[i])<.05:continue
        if clear(start,nodes[i],near_start,strict):return i
    return None


# Prepare static, public geometry during strategy initialization, before the
# decision timer begins. This creates no threads, external reads or hidden data.
for _arena in ARENAS_BY_SEASON.values():
    if _arena.get('layout_revision'):
        for _team in ('red','blue'):
            for _folded in ((False,True) if _arena.get('season')=='2027' else (True,)):_prepare_chapter4(_arena,_team,_folded)


def resource_pickup_point(robot,arena):
    """A station approach that stays inside the zone and within the chosen arm workspace."""
    station=next(f for f in arena['field_geometry']['fixtures'] if f['kind']=='resource' and f['team']==robot['team'])
    point=station['pick_point'];direction=station['pick_dir'];reach=.48 if robot.get('arm_count',1)==2 else .60
    return (point[0]-direction[0]*reach,point[1]-direction[1]*reach)


def assembly_work_point(robot,arena):
    """Dock inside the plinth, away from its ramp edge, while keeping the core in reach."""
    zone=next(z for z in arena['zones'] if z['id']==robot['team']+'_assembly')
    origin=arena['assembly_frames'][robot['team']]['origin']
    dx,dy=origin[0]-zone['x'],origin[1]-zone['y']
    length=math.hypot(dx,dy) or 1.0
    docking_distance=.25*math.sqrt(2)
    return (zone['x']+docking_distance*dx/length,zone['y']+docking_distance*dy/length)


# ---- RMUC 2027 helpers ------------------------------------------------------------------

def workbench_point(robot, arena):
    """Where a 2027 heavy robot parks to fit its modular 42 mm launcher (install_launcher=True)."""
    bench=next(f for f in arena['field_geometry']['fixtures'] if f['kind']=='workbench' and f['team']==robot['team'])
    return tuple(bench['dock_point'])


def launcher_ready(robot):
    """The heavy robot's launcher is unlocked (by level-3 assembly or the time rule) but not yet fitted."""
    return robot.get('launcher') in ('unlocked','installing')


def position(observation, name):
    """A control position: {'owner','kind','control':'full'|'temporary','controller',...}.
    Names: red_outpost, red_base, blue_outpost, blue_base."""
    return observation.get('positions',{}).get(name)


def controls(observation, name, team=None):
    """True while `team` (default: own) controls the position, fully or temporarily."""
    p=position(observation,name);team=team or observation['team']
    return bool(p) and p['controller']==team


def control_degree(observation, team=None):
    team=team or observation['team']
    return observation['teams'].get(team,{}).get('control') or 0.0


def zone_center(arena, zone_id):
    """A point on the buff card: its reference point, which lies inside even oddly shaped
    cards such as the long central-highland strips."""
    z=next(z for z in arena['zones'] if z['id']==zone_id)
    return (z['x'],z['y'])


def drones(robot):
    """The aerial robots of a 2027 aerial system (hangar), with state, pose, battery and integrity."""
    return robot.get('drones') or []


def strike_plan(drone, plate, standoff=.9, team_heights=None):
    """A ramming run at an armor plate: hover at `standoff` in front of its face, then
    fly straight into it. Returns ('approach', point) until lined up, then ('strike', direction).
    Use as drones={'1':{'goto':point}} or drones={'1':{'move':direction}}."""
    n=plate['normal'];c=plate['center']
    flat=math.hypot(n[0],n[1]) or 1.0
    ux,uy=n[0]/flat,n[1]/flat
    stand=(c[0]+ux*standoff,c[1]+uy*standoff,c[2])
    lateral=abs((drone['x']-c[0])*(-uy)+(drone['y']-c[1])*ux)
    ahead=(drone['x']-c[0])*ux+(drone['y']-c[1])*uy
    if lateral<.05 and abs(drone['z']-c[2])<.06 and ahead>.15:
        return 'strike',(-ux,-uy,(c[2]-drone['z'])*2)
    return 'approach',stand


def hostile_plates(observation, kind):
    """Armor plates a 2027 aerial robot may strike: 'outpost' (any outpost not owned by
    this team and still standing) or 'base' (the enemy base)."""
    team=observation['team'];enemy='blue' if team=='red' else 'red'
    if kind=='base':
        base=observation['bases'][enemy]
        return base.get('armor_plates',[]) if base['hp']>0 else []
    plates=[]
    for owner,outpost in observation['outposts'].items():
        if outpost['hp']>0 and outpost.get('owner',owner)!=team:plates+=outpost.get('armor_plates',[])
    return plates
