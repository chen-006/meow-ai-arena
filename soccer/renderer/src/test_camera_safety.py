"""机位回归：倒角、门洞、实体厚度、震动、庆祝圆盘与镜头连续性。"""
import math, unittest
from camera_safety import inside, blocked, constrain_player, position, violations, safe_camera, protect_shots

def camera_at(p,t=(0,0,0),fov=28):
    dx,dy,dz=(p[k]-t[k] for k in range(3))
    return dict(tx=t[0],ty=t[1],tz=t[2],d=math.sqrt(dx*dx+dy*dy+dz*dz),
                yaw=math.atan2(dx,dz),pitch=math.atan2(dy,math.hypot(dx,dz)),fov=fov)

class SafetyTests(unittest.TestCase):
    def test_chamfer_is_not_rectangle(self):
        self.assertFalse(inside(5.7,3.7))
        self.assertTrue(inside(5.5,2.5))

    def test_wall_and_advertising_top(self):
        self.assertTrue(blocked((0,.25,5),(0,.15,3)))
        self.assertFalse(blocked((0,5,5),(0,.15,3)))
        self.assertTrue(blocked((6.4,.2,3.4),(5,.2,2)))

    def test_goal_mouth_is_open(self):
        self.assertFalse(blocked((4,.2,0),(6.5,.15,0)))
        self.assertTrue(blocked((4,.2,2),(6.5,.15,2)))

    def test_high_broadcast_allowed_low_exterior_rejected(self):
        self.assertFalse(violations(camera_at((0,10,10)),'live'))
        self.assertTrue(violations(camera_at((0,.4,5)),'live'))
        self.assertTrue(violations(camera_at((0,10,10)),'close'))

    def test_shake_checked_after_displacement(self):
        c=camera_at((5.81,3,0),(4,0,0))
        f={'shake':1,'wall':math.pi/2/37.1}
        self.assertTrue(inside(*[position(c)[i] for i in (0,2)]))
        self.assertTrue(violations(c,'close',f))

    def test_post_goal_discs_fit_all_corners(self):
        for x in (-8,-5.7,0,5.7,8):
            for z in (-6,-3.7,0,3.7,6):
                q=constrain_player(x,z)
                self.assertTrue(inside(*q,margin=.3-1e-9),q)
        self.assertEqual(constrain_player(2,1),(2,1))

    def test_fallback_all_edges_and_goals(self):
        for x,z in [(-6.55,0),(6.55,0),(-5.7,-3.7),(5.7,-3.7),(-5.7,3.7),(5.7,3.7),(0,3.7),(0,0)]:
            for kind in ('close','replay','live'):
                old=camera_at((x+3,1,z+2),(x,.32,z),32)
                safe=safe_camera(old,kind)
                self.assertEqual(violations(safe,kind),[],(x,z,kind))

    def test_entire_shot_changes_not_single_frame(self):
        good=camera_at((0,3,2));bad=camera_at((0,1,5),(0,0,3))
        frames=[dict(i=k,wall=k/60,shot=0,kind='close',cam=dict(c)) for k,c in enumerate((good,bad,good))]
        protect_shots(frames,lambda f:[])
        self.assertTrue(all('cameraSafety' in f for f in frames))
        self.assertTrue(all(not violations(f['cam'],'close',f) for f in frames))

if __name__=='__main__':unittest.main()
