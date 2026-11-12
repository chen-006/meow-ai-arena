# -*- coding: utf-8 -*-
"""渲染设置、灯光、相机（Blender）。"""
import math

import bpy
from mathutils import Vector

import scene_lib as SL


def setup_render(samples=12, pct=100, png=True, fast=True, w=1920, h=1080):
    sc = bpy.context.scene
    sc.render.engine = 'BLENDER_EEVEE'
    sc.render.resolution_x = w
    sc.render.resolution_y = h
    sc.render.resolution_percentage = pct
    sc.render.film_transparent = False
    if png:
        sc.render.image_settings.file_format = 'PNG'
        sc.render.image_settings.color_mode = 'RGB'
        sc.render.image_settings.compression = 20
    else:
        sc.render.image_settings.file_format = 'JPEG'
        sc.render.image_settings.quality = 95
    sc.view_settings.view_transform = 'Standard'
    sc.view_settings.look = 'None'
    sc.render.threads_mode = 'FIXED'
    sc.render.threads = 4
    sc.render.use_persistent_data = True
    ee = sc.eevee
    ee.taa_render_samples = samples
    ee.use_shadows = True
    ee.shadow_ray_count = 1 if fast else 2
    ee.shadow_step_count = 6 if fast else 8
    try:
        ee.shadow_resolution_scale = 1.0
    except Exception:
        pass
    ee.use_raytracing = True
    ee.ray_tracing_method = 'SCREEN'
    try:
        ee.ray_tracing_options.resolution_scale = '2' if fast else '1'
    except Exception:
        pass
    ee.use_fast_gi = True
    try:
        ee.fast_gi_method = 'GLOBAL_ILLUMINATION'
        ee.fast_gi_distance = 2.5
        ee.fast_gi_ray_count = 2
        ee.fast_gi_step_count = 6 if fast else 8
        ee.fast_gi_resolution = '4' if fast else '2'
    except Exception:
        pass


def sun(az=150, el=52, energy=4.4, color=(1.0, 0.93, 0.82), angle=4.0):
    return SL.add_sun(az, el, energy, color, angle)


def camera(target, pitch=55.0, yaw=0.0, dist=60.0, lens=50.0, name="cam"):
    """target：Blender 坐标（X 东、Y 北、Z 上）。pitch：俯角（度）。yaw：0 = 从南往北看。"""
    cam = bpy.data.objects.get(name)
    if cam is None:
        cd = bpy.data.cameras.new(name)
        cd.sensor_width = 36.0
        cd.clip_start = 0.5
        cd.clip_end = 1200.0
        cam = bpy.data.objects.new(name, cd)
        bpy.context.scene.collection.objects.link(cam)
    cam.data.lens = lens
    p = math.radians(pitch)
    yw = math.radians(yaw)
    back = Vector((math.sin(yw) * -math.cos(p), -math.cos(yw) * math.cos(p), math.sin(p)))
    cam.location = Vector(target) + back * dist
    cam.rotation_euler = (Vector(target) - cam.location).to_track_quat('-Z', 'Y').to_euler()
    bpy.context.scene.camera = cam
    return cam


def fit_dist(width_m, lens=50.0, sensor=36.0, margin=1.0):
    """装下 width_m 米宽（水平方向）要退多远（近似，画面中心处）。"""
    fov = 2 * math.atan(sensor / 2 / lens)
    return width_m * margin / 2 / math.tan(fov / 2)


def render_to(path):
    sc = bpy.context.scene
    sc.render.filepath = path
    bpy.ops.render.render(write_still=True)
    print("STILL", path, flush=True)
