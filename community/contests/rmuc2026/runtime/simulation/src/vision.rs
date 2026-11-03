//! Camera apertures are public simulator parameters; range and occlusion remain separate.
use super::*;
use rapier3d::na::{Quaternion, UnitQuaternion, Vector3};

impl Engine {
    pub(super) fn camera_rotation(r: &Robot) -> UnitQuaternion<f64> {
        let q=r.quaternion;
        UnitQuaternion::new_normalize(Quaternion::new(q[3],q[0],q[1],q[2]))
    }
    fn aperture(&self,r:&Robot)->(f64,f64){
        if r.role=="aerial" {return (360.0,180.0);}
        let v=&self.sim["ground_camera_fov_deg"];
        (v["horizontal"].as_f64().unwrap_or(100.0).clamp(1.0,179.0),
         v["vertical"].as_f64().unwrap_or(70.0).clamp(1.0,179.0))
    }
    pub(super) fn camera_metadata(&self,r:&Robot)->Value{
        let (horizontal,vertical)=self.aperture(r);
        json!({"origin":Self::eye(r),"direction":physics::muzzle(r).1,
            "horizontal_fov_deg":horizontal,"vertical_fov_deg":vertical})
    }
    pub(super) fn point_in_camera(&self,r:&Robot,point:[f64;3])->bool{
        if r.role=="aerial" {return true;}
        let forward=Vector3::from(physics::muzzle(r).1).normalize();
        let body_up=Self::camera_rotation(r)*Vector3::z();
        let mut right=forward.cross(&body_up);
        if right.norm_squared()<1e-12 {
            let reference=if forward.z.abs()<0.9 {Vector3::z()}else{Vector3::y()};
            right=forward.cross(&reference);
        }
        let right=right.normalize();let up=right.cross(&forward);
        let offset=Vector3::from(point)-Vector3::from(Self::eye(r));
        let depth=offset.dot(&forward);
        if depth<=1e-9 {return false;}
        let (horizontal,vertical)=self.aperture(r);
        offset.dot(&right).abs()<=depth*(horizontal.to_radians()/2.0).tan()+1e-9
            && offset.dot(&up).abs()<=depth*(vertical.to_radians()/2.0).tan()+1e-9
    }
}
