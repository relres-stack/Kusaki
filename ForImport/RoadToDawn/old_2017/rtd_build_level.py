"""Road to Dawn - blockout builder for Unreal Engine 5 (Python Editor Script).

How to run (see README_RoadToDawn.txt):
  1. Create an empty level, import the heightmap as a Landscape (settings in the README).
  2. File > Execute Python Script... > pick this file   (or Output Log: Cmd -> `py <path>`).

The script reads  <Project>/ForImport/RoadToDawn/rtd_layout.json  and spawns the blockout:
road, ring road, streets, outer fence + inner wall, gates, towers, houses, facility, river water, trees,
Redneck PlayerStart, binoculars, bear trap, nav-mesh volume.  Every spawned actor is put in the
Outliner folder "RoadToDawn_Gen/..." so a re-run first deletes the previous result.

Plan coordinates in the JSON: x = east, y = north (metres).  UE coordinates: X = north, Y = east.
"""
import json
import math
import os

import unreal

# ------------------------------------------------------------------ settings
BUILD_TREES = True
BUILD_GAMEPLAY = True          # PlayerStart + ABinocularsPickup + ABearTrap + NavMeshBoundsVolume
BUILD_LIGHTING = False         # the project has its own ADayNightManager - leave False unless you want a quick sunset
CLEAR_PREVIOUS = True
MAX_FALLBACK_ACTORS = 2500     # only used if instanced meshes cannot be created (very old/odd setups)

DATA_PATH = os.path.join(unreal.Paths.project_dir(), "ForImport", "RoadToDawn", "rtd_layout.json")
MAT_DIR = "/Game/RoadToDawn/Materials/"
ROOT_FOLDER = "RoadToDawn_Gen"

# ------------------------------------------------------------------ helpers
actor_sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
log = lambda m: unreal.log("[RTD] " + str(m))
warn = lambda m: unreal.log_warning("[RTD] " + str(m))


def vec(x_east, y_north, z):
    """plan metres -> UE cm vector (X = north, Y = east)."""
    return unreal.Vector(y_north * 100.0, x_east * 100.0, z * 100.0)


def yaw_ue(theta_math_deg):
    """plan angle (0 = east, CCW) -> UE yaw (0 = +X north, towards +Y east)."""
    return 90.0 - theta_math_deg


def tf(x, y, z, yaw_math=0.0, pitch=0.0, sx=1.0, sy=1.0, sz=1.0):
    """Transform whose local X axis runs along the plan direction `yaw_math`; scale in metres of a 1 m cube."""
    rot = unreal.Rotator(roll=0.0, pitch=pitch, yaw=yaw_ue(yaw_math))
    return unreal.Transform(location=vec(x, y, z), rotation=rot, scale=unreal.Vector(sx, sy, sz))


def load(path):
    a = unreal.load_asset(path)
    if a is None:
        warn("missing asset: " + path)
    return a


CUBE = load("/Engine/BasicShapes/Cube")
CYL = load("/Engine/BasicShapes/Cylinder")
CONE = load("/Engine/BasicShapes/Cone")
M = {k: load(MAT_DIR + "M_RTD_" + k) for k in ("Terrain", "Road", "Fence", "Forest", "River", "Buildings")}

fallback_budget = [MAX_FALLBACK_ACTORS]


class Batch(object):
    """One instanced-mesh actor per (mesh, material, folder)."""

    def __init__(self, label, folder, mesh, material):
        self.label, self.folder, self.mesh, self.material = label, folder, mesh, material
        self.comp = None
        self.fallback = False
        self.count = 0
        try:
            actor = actor_sub.spawn_actor_from_class(unreal.Actor, unreal.Vector(0, 0, 0))
            actor.set_actor_label(label)
            actor.set_folder_path(ROOT_FOLDER + "/" + folder)
            comp = actor.add_component_by_class(unreal.InstancedStaticMeshComponent, False, unreal.Transform(), False)
            comp.set_static_mesh(mesh)
            if material is not None:
                comp.set_material(0, material)
            self.actor, self.comp = actor, comp
        except Exception as ex:  # noqa
            warn("ISM unavailable for %s (%s) - using single actors" % (label, ex))
            self.fallback = True

    def add(self, transform):
        self.count += 1
        if not self.fallback:
            self.comp.add_instance(transform)
            return
        if fallback_budget[0] <= 0:
            return
        fallback_budget[0] -= 1
        loc = transform.translation
        a = actor_sub.spawn_actor_from_class(unreal.StaticMeshActor, loc, transform.rotation.rotator())
        a.set_actor_scale3d(transform.scale3d)
        a.static_mesh_component.set_static_mesh(self.mesh)
        if self.material is not None:
            a.static_mesh_component.set_material(0, self.material)
        a.set_folder_path(ROOT_FOLDER + "/" + self.folder)


def box(batch, x, y, z_base, w, d, h, yaw=0.0, pitch=0.0):
    """Cube sitting on z_base: w along local X, d along local Y, h up."""
    batch.add(tf(x, y, z_base + h / 2.0, yaw, pitch, w, d, h))


# ------------------------------------------------------------------ load data
with open(DATA_PATH, "r") as f:
    L = json.load(f)
log("layout loaded: %s" % json.dumps(L["stats"]))

# ------------------------------------------------------------------ clear previous
if CLEAR_PREVIOUS:
    removed = 0
    for a in actor_sub.get_all_level_actors():
        try:
            if str(a.get_folder_path()).startswith(ROOT_FOLDER):
                actor_sub.destroy_actor(a)
                removed += 1
        except Exception:
            pass
    log("removed %d actors from the previous run" % removed)

try:
    trans = unreal.ScopedEditorTransaction("Road to Dawn blockout")
except Exception:
    trans = None

# ------------------------------------------------------------------ roads / streets
roads = Batch("RTD_Roads", "Roads", CUBE, M["Road"])


def ribbon(batch, pts, width, thick=0.25, step=2, lift=0.05):
    for k in range(0, len(pts) - step, step):
        x0, y0, z0 = pts[k]
        x1, y1, z1 = pts[min(k + step, len(pts) - 1)]
        dx, dy = x1 - x0, y1 - y0
        ln = math.hypot(dx, dy)
        if ln < 0.2:
            continue
        yaw = math.degrees(math.atan2(dy, dx))
        pitch = math.degrees(math.atan2(z1 - z0, ln))
        batch.add(tf((x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2 + lift + thick / 2, yaw, pitch, ln + 0.35, width, thick))


ribbon(roads, L["road"]["points"], L["road"]["width"])
for s in L["streets"]:
    ribbon(roads, s["pts"], s["width"], step=1)
log("roads: %d segments" % roads.count)

# ------------------------------------------------------------------ river
water = Batch("RTD_River", "River", CUBE, M["River"])
ribbon(water, L["river"]["points"], L["river"]["width"], thick=0.3, step=1, lift=-0.1)
log("river: %d segments" % water.count)

# ------------------------------------------------------------------ walls, gates, towers
fence = Batch("RTD_Walls", "Walls", CUBE, M["Fence"])
for w in L["walls"]:
    th = w["t"]
    box(fence, w["x"], w["y"], w["z"], w["len"], th, w["h"], w["yaw"])
for g in L["gates"]:
    box(fence, g["x"], g["y"], g["z"], g["w"], 0.9, g["h"], 0.0)                      # closed gate leaf
    box(fence, g["x"] - g["w"] / 2 - 0.6, g["y"], g["z"], 1.2, 1.6, g["h"] + 1.5, 0.0)  # gate posts
    box(fence, g["x"] + g["w"] / 2 + 0.6, g["y"], g["z"], 1.2, 1.6, g["h"] + 1.5, 0.0)
bld = Batch("RTD_Buildings", "Buildings", CUBE, M["Buildings"])
for t in L["towers"]:
    box(bld, t["x"], t["y"], t["z"], t["size"], t["size"], t["h"])
    box(bld, t["x"], t["y"], t["z"] + t["h"], t["size"] + 2.0, t["size"] + 2.0, 3.0)  # watch cabin
log("walls: %d segments" % fence.count)

# ------------------------------------------------------------------ village
tank = Batch("RTD_WaterTank", "Buildings", CYL, M["Buildings"])
for b in L["buildings"]:
    z = b["z"]
    if b["kind"] == "tower":                      # water tower: four legs + tank
        for sx in (-1, 1):
            for sy in (-1, 1):
                box(bld, b["x"] + sx * 2.5, b["y"] + sy * 2.5, z, 0.7, 0.7, 14.0)
        tank.add(tf(b["x"], b["y"], z + 14.0 + 3.0, 0.0, 0.0, 8.0, 8.0, 6.0))
    elif b["kind"] == "gas":                      # small shop + canopy on four posts
        box(bld, b["x"] - 6.0, b["y"] + 3.0, z, 10.0, 8.0, 4.0, b["yaw"])
        box(bld, b["x"] + 4.0, b["y"] - 3.0, z + 5.0, 14.0, 9.0, 0.6, b["yaw"])
        for sx in (-1, 1):
            for sy in (-1, 1):
                box(bld, b["x"] + 4.0 + sx * 6.0, b["y"] - 3.0 + sy * 3.5, z, 0.5, 0.5, 5.0)
    else:
        box(bld, b["x"], b["y"], z, b["w"], b["d"], b["h"], b["yaw"])
        box(bld, b["x"], b["y"], z + b["h"], b["w"] + 1.2, b["d"] + 1.2, 0.6, b["yaw"])   # roof slab
for c in L["containers"]:
    box(bld, c["x"], c["y"], c["z"], c["w"], c["d"], c["h"], c["yaw"])
fa = L["facility"]
box(bld, fa["x"], fa["y"], fa["z"], fa["w"], fa["d"], fa["h"], fa["yaw"])
box(bld, fa["x"], fa["y"], fa["z"] + fa["h"], fa["w"] * 0.4, fa["d"] * 0.5, 6.0, fa["yaw"])   # roof block
lamps = Batch("RTD_Lamps", "Props", CYL, M["Fence"])
for p in L["lamps"]:
    lamps.add(tf(p["x"], p["y"], p["z"] + 2.5, 0.0, 0.0, 0.3, 0.3, 5.0))
log("buildings: %d" % bld.count)

# ------------------------------------------------------------------ trees
if BUILD_TREES:
    trunks = Batch("RTD_TreeTrunks", "Forest", CYL, M["Forest"])
    crowns = Batch("RTD_TreeCrowns", "Forest", CONE, M["Forest"])
    for (x, y, z, s) in L["trees"]:
        trunks.add(tf(x, y, z + 1.6 * s, 0.0, 0.0, 0.55 * s, 0.55 * s, 3.2 * s))
        crowns.add(tf(x, y, z + (3.0 + 6.0) * s, 0.0, 0.0, 4.2 * s, 4.2 * s, 12.0 * s))
    log("trees: %d" % trunks.count)

# ------------------------------------------------------------------ gameplay actors
if BUILD_GAMEPLAY:
    ps = L["player_start"]
    start = actor_sub.spawn_actor_from_class(unreal.PlayerStart, vec(ps["x"], ps["y"], ps["z"] + 1.0),
                                             unreal.Rotator(roll=0.0, pitch=0.0, yaw=yaw_ue(ps["yaw_math_deg"])))
    start.set_actor_label("RTD_RedneckStart")
    start.set_folder_path(ROOT_FOLDER + "/Gameplay")

    def try_spawn(class_path, data, label):
        try:
            cls = unreal.load_class(None, class_path)
            if cls is None:
                warn("class not found: " + class_path)
                return None
            a = actor_sub.spawn_actor_from_class(cls, vec(data["x"], data["y"], data["z"]))
            a.set_actor_label(label)
            a.set_folder_path(ROOT_FOLDER + "/Gameplay")
            return a
        except Exception as ex:  # noqa
            warn("could not spawn %s: %s" % (label, ex))
            return None

    try_spawn("/Script/MyCharacters.BinocularsPickup", L["binoculars"], "RTD_Binoculars")
    try_spawn("/Script/MyCharacters.BearTrap", L["bear_trap"], "RTD_BearTrap_Gate")

    nv = L["navmesh"]
    try:
        vol = actor_sub.spawn_actor_from_class(unreal.NavMeshBoundsVolume, vec(nv["x"], nv["y"], 3000.0 / 100.0))
        vol.set_actor_label("RTD_NavMeshBounds")
        vol.set_actor_scale3d(unreal.Vector(nv["d"] * 100.0 / 200.0, nv["w"] * 100.0 / 200.0, 200.0 * 100.0 / 200.0))
        vol.set_folder_path(ROOT_FOLDER + "/Gameplay")
    except Exception as ex:  # noqa
        warn("NavMeshBoundsVolume not created: %s" % ex)

# ------------------------------------------------------------------ optional quick lighting
if BUILD_LIGHTING:
    try:
        sun = actor_sub.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(0, 0, 50000))
        sun.set_actor_rotation(unreal.Rotator(roll=0.0, pitch=-9.0, yaw=200.0), False)
        sun.set_actor_label("RTD_Sun")
        sun.set_folder_path(ROOT_FOLDER + "/Lighting")
        for c in (unreal.SkyAtmosphere, unreal.SkyLight, unreal.ExponentialHeightFog):
            a = actor_sub.spawn_actor_from_class(c, unreal.Vector(0, 0, 40000))
            a.set_folder_path(ROOT_FOLDER + "/Lighting")
    except Exception as ex:  # noqa
        warn("lighting not created: %s" % ex)

del trans
log("DONE. Terrain feature check: PlayerStart should stand on the rocky ledge; gate is 660 m north-east of it.")
