"""RMUC 2026 V1.0.0 baseline: purchases, respawn, engineer, air support."""
import math
from sdk import action, aim_at, aim_pitch, ballistic_reachable, can_fire, intercept, navigate, nearest_enemy, instant_revive_cost, rescue_point, tipped_teammates, resource_pickup_point, assembly_work_point

DEFENSIVE = False


def point_distance(robot, point):
    return math.hypot(robot['x']-point[0],robot['y']-point[1])


def unstick(r,move,memory,now):
    """Action fields for `move`; once a robot pushing on has neither moved nor turned for
    1.5 s, it backs off at an angle for 0.7 s so teammates that meet head-on in a narrow
    lane get past each other. It keeps its heading then: a chassis cannot turn on bumps."""
    m=memory.setdefault(str(r['slot']),{'trail':[],'tries':0})
    if now<m.get('until',-1):return {'move':m['escape'],'heading':m['heading']}
    trail=m['trail']
    if math.hypot(*move)<.01:
        trail.clear();return {'move':move}
    trail.append([now,r['x'],r['y'],r['heading']])
    while now-trail[0][0]>1.5:trail.pop(0)
    turned=abs((r['heading']-trail[0][3]+math.pi)%math.tau-math.pi)
    if now-trail[0][0]<1.4 or point_distance(r,trail[0][1:3])>.1 or turned>.15:return {'move':move}
    m['tries']+=1;angle=math.atan2(move[1],move[0])+math.pi+(.9 if (r['slot']+m['tries'])%2 else -.9)
    m.update(escape=[.7*math.cos(angle),.7*math.sin(angle)],until=now+.7,heading=r['heading']);trail.clear()
    return {'move':m['escape'],'heading':m['heading']}


def act(obs, state):
    commands={};team=obs['team'];sign=1 if team=='red' else -1;enemy='blue' if sign==1 else 'red'
    coins=obs['teams'][team]['resources'];base=obs['bases'][team];enemy_base=obs['bases'][enemy];outpost=obs['outposts'][enemy]
    zone={z['id']:z for z in obs['arena']['zones']};home=(base['x']+sign*1.5,base['y'])
    memory=(state or {}).get('stuck',{})
    for r in obs['robots']:
        slot=str(r['slot'])
        if slot not in obs['controlled_ids']:continue
        if r['role']=='aerial':
            lane=obs['arena']['flight_lanes'][team];goal=(lane['max_x']-1 if team=='red' else lane['min_x']+1,lane['center_y'])
            target=nearest_enemy(r,obs)
            if target:target=intercept(r,target)
            commands[slot]=action(navigate(r,goal,obs['arena']),aim_at(r,target) if target else None,
                                  can_fire(r) and target is not None,pitch=aim_pitch(r,target) if target else 0.0,
                                  support=True,altitude=1.9)
            continue
        if not r['alive']:
            commands[slot]=action(revive=coins>=instant_revive_cost(r,obs['time']) and obs['time']<300)
            continue
        if r['role']=='engineer':
            # Right a tipped teammate first: it cannot drive, and respawning keeps it tipped.
            tipped=[t for t in tipped_teammates(obs) if t['role']!='aerial' and point_distance(r,(t['x'],t['y']))<8]
            if tipped and not r['weak']:
                target=min(tipped,key=lambda t:point_distance(r,(t['x'],t['y'])))
                spot=rescue_point(r,target)
                far=point_distance(r,(target['x'],target['y']))>.8
                commands[slot]=action(rescue=target['slot'],**unstick(r,navigate(r,spot,obs['arena']) if far else (0,0),memory,obs['time']))
                continue
            core=obs['assemblies'][team];completed=obs['teams'][team]['completed_difficulties']
            active=core['stage'] in ('moving','inserting','buffered')
            possible=[d for d in range(1,5) if obs['time']>=(d-1)*60 and (d==1 or d-1 in completed) and not (d==4 and (4 in completed or core['permanent_lock'] or obs['time']<core['retry_at']))]
            difficulty=core['difficulty'] if active else max(possible,default=1)
            needed=1 if difficulty<4 or core['inserted_units']==1 else 2
            if not active and r['carried_units']<needed:
                goal=resource_pickup_point(r,obs['arena']);near=point_distance(r,goal)<=.08
                drive=unstick(r,(0,0) if near else navigate(r,goal,obs['arena']),memory,obs['time'])
                # Face the station there: the arm cannot slew round to a unit behind the chassis.
                station=next(f for f in obs['arena']['field_geometry']['fixtures'] if f['kind']=='resource' and f['team']==team)
                if near:drive['heading']=math.atan2(station['pick_dir'][1],station['pick_dir'][0])
                commands[slot]=action(pick_unit=True,**drive)
            else:
                goal=assembly_work_point(r,obs['arena']);gap=point_distance(r,goal);docked=gap<=.1
                move=(0,0) if docked else navigate(r,goal,obs['arena']);pace=math.hypot(*move)
                # The work point lies past the plinth's bevelled edge: a slowing approach stalls
                # on that slope, tilted too far for the exact grades. Dock level, facing the core.
                if gap<1 and 0<pace<.2:move=(move[0]/pace*.2,move[1]/pace*.2)
                origin=obs['arena']['assembly_frames'][team]['origin'];plinth=zone[team+'_assembly']
                drive=unstick(r,move,memory,obs['time']) if gap>.6 else {'move':move}
                if docked:drive['heading']=math.atan2(origin[1]-plinth['y'],origin[0]-plinth['x'])
                commands[slot]=action(brake=docked,assembly=difficulty,assembly_insert=True,assembly_translate=True,
                    assembly_rotate=core['target_rotation'],assembly_confirm=True,**drive)
            continue
        if r['weak'] or r['hp']<r['max_hp']*.25:
            supply=zone[team+'_supply'];goal=(supply['x'],supply['y'])
        elif r['ammo']==0:
            goal=home
        elif DEFENSIVE and r['role']=='infantry':
            goal=(base['x']+sign*4,base['y']+(r['slot']%3-1)*1.2)
        else:
            structure=outpost if outpost['hp']>0 else enemy_base
            goal=(structure['x']-sign*(4 if r['role']=='hero' else 3),structure['y']+(r['slot']%3-1)*.7)
        target=nearest_enemy(r,obs)
        if target and ballistic_reachable(r,target):target=intercept(r,target)
        else:
            structure=outpost if outpost['hp']>0 else enemy_base
            target=dict(structure,kind='outpost' if outpost['hp']>0 else 'base')
            if not ballistic_reachable(r,target):target=None
        aim=aim_at(r,target) if target else None
        aligned=aim is not None and abs((aim-r['aim']+math.pi)%(math.tau)-math.pi)<.15
        operations={}
        if obs['tick']==0:operations['profile']='ranged' if r['role']=='hero' else {'chassis':r['chassis_profile'],'weapon':r['weapon_profile']}
        if r['ammo']<(2 if r['role']=='hero' else 20):
            amount=5 if r['role']=='hero' else 50
            operations['buy_ammo']={'amount':amount,'remote':False}
        fire=aligned and target is not None and can_fire(r)
        if r['role']=='infantry' and r['heat']+20>r['max_heat']:fire=False
        move=navigate(r,goal,obs['arena']) if point_distance(r,goal)>.5 else (0,0)
        commands[slot]=action(aim=aim,fire=fire,pitch=aim_pitch(r,target) if target else 0.04,**operations,**unstick(r,move,memory,obs['time']))
    return commands,{'ticks':(state or {}).get('ticks',0)+1,'stuck':memory}
