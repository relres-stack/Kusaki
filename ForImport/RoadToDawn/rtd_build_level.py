"""Road to Dawn - blockout builder for Unreal Engine 5 (Python Editor Script).

How to run (see README_RoadToDawn.txt):
  1. Create an empty level, import the heightmap as a Landscape (settings in the README).
  2. File > Execute Python Script... > pick this file   (or Output Log: Cmd -> `py <path>`).

The script reads  <Project>/ForImport/RoadToDawn/rtd_layout.json  and spawns the blockout:
road, ring road, streets, outer fence + inner wall, gates, towers, houses, facility, river water, trees,
Redneck PlayerStart, binoculars, bear trap, nav-mesh volume, old mine in the hill.  Every spawned actor is put in the
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
MAX_FALLBACK_ACTORS = 1500     # only used if instanced meshes cannot be created (very old/odd setups)

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


def tf(x, y, z, yaw_math=0.0, pitch=0.0, sx=1.0, sy=1.0, sz=1.0, roll=0.0):
    """Transform whose local X axis runs along the plan direction `yaw_math`; scale in metres of a 1 m cube."""
    rot = unreal.Rotator(roll=roll, pitch=pitch, yaw=yaw_ue(yaw_math))
    return unreal.Transform(location=vec(x, y, z), rotation=rot, scale=unreal.Vector(sx, sy, sz))


def load(path):
    a = unreal.load_asset(path)
    if a is None:
        warn("missing asset: " + path)
    return a


CUBE = load("/Engine/BasicShapes/Cube")
CYL = load("/Engine/BasicShapes/Cylinder")
CONE = load("/Engine/BasicShapes/Cone")
SPHERE = load("/Engine/BasicShapes/Sphere")
M = {k: load(MAT_DIR + "M_RTD_" + k) for k in ("Terrain", "Road", "Fence", "Forest", "River", "Buildings")}

fallback_budget = [MAX_FALLBACK_ACTORS]
ALL_BATCHES = []


_ISM_CLASS = []


def get_ism_class():
    """A Blueprint actor class that owns one InstancedStaticMeshComponent (created once, saved as /Game/RoadToDawn/Gen/BP_RTD_ISM).
    Needed because this Unreal build does not expose AddComponentByClass / RegisterComponent to Python."""
    if _ISM_CLASS:
        return _ISM_CLASS[0]
    path = "/Game/RoadToDawn/Gen/BP_RTD_ISM"
    bp = None
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        bp = unreal.EditorAssetLibrary.load_asset(path)
    if bp is None:
        tools = unreal.AssetToolsHelpers.get_asset_tools()
        factory = unreal.BlueprintFactory()
        factory.set_editor_property("parent_class", unreal.Actor)
        bp = tools.create_asset("BP_RTD_ISM", "/Game/RoadToDawn/Gen", unreal.Blueprint, factory)
        sub = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
        handles = sub.k2_gather_subobject_data_for_blueprint(bp)
        params = unreal.AddNewSubobjectParams(parent_handle=handles[0], new_class=unreal.InstancedStaticMeshComponent, blueprint_context=bp)
        handle, fail_reason = sub.add_new_subobject(params)
        unreal.log("[RTD] add_new_subobject: %s" % fail_reason)   # empty text = no error
        unreal.BlueprintEditorLibrary.compile_blueprint(bp)
        unreal.EditorAssetLibrary.save_loaded_asset(bp)
    cls = bp.generated_class()
    _ISM_CLASS.append(cls)
    return cls


def make_ism_actor(label, folder, mesh, material):
    """Returns (actor, ism_component) or raises."""
    actor = actor_sub.spawn_actor_from_class(get_ism_class(), unreal.Vector(0, 0, 0))
    actor.set_actor_label(label)
    actor.set_folder_path(ROOT_FOLDER + "/" + folder)
    comp = actor.get_component_by_class(unreal.InstancedStaticMeshComponent)
    if comp is None:
        actor_sub.destroy_actor(actor)
        raise RuntimeError("BP_RTD_ISM has no InstancedStaticMeshComponent (delete /Game/RoadToDawn/Gen/BP_RTD_ISM and rerun)")
    comp.set_static_mesh(mesh)
    if material is not None:
        comp.set_material(0, material)
    return actor, comp


class Batch(object):
    """Collects transforms; flush() pushes them into ONE instanced-mesh actor with a single bulk call.
    If instanced meshes cannot be created in this engine build, falls back to single StaticMeshActors (limited by MAX_FALLBACK_ACTORS)."""

    def __init__(self, label, folder, mesh, material):
        self.label, self.folder, self.mesh, self.material = label, folder, mesh, material
        self.items = []
        self.count = 0
        self.flushed = False
        ALL_BATCHES.append(self)

    def add(self, transform):
        self.count += 1
        self.items.append(transform)

    def flush(self):
        if self.flushed or not self.items:
            return
        self.flushed = True
        ok = False
        try:
            actor, comp = make_ism_actor(self.label, self.folder, self.mesh, self.material)
            try:
                comp.add_instances(self.items, False, False)
            except Exception:
                for t in self.items:
                    comp.add_instance(t)
            ok = comp.get_instance_count() >= len(self.items)
            if not ok:
                warn("%s: ISM holds %d of %d instances - using fallback" % (self.label, comp.get_instance_count(), len(self.items)))
                actor_sub.destroy_actor(actor)
        except Exception as ex:  # noqa
            warn("ISM unavailable for %s (%s) - using single actors" % (self.label, ex))
        if ok:
            log("%s: %d instances in one actor" % (self.label, len(self.items)))
            return
        spawned = 0
        for transform in self.items:
            if fallback_budget[0] <= 0:
                break
            fallback_budget[0] -= 1
            spawned += 1
            loc = transform.translation
            a = actor_sub.spawn_actor_from_class(unreal.StaticMeshActor, loc, transform.rotation.rotator())
            a.set_actor_scale3d(transform.scale3d)
            a.static_mesh_component.set_static_mesh(self.mesh)
            if self.material is not None:
                a.static_mesh_component.set_material(0, self.material)
            a.set_folder_path(ROOT_FOLDER + "/" + self.folder)
        warn("%s: fallback spawned %d of %d actors" % (self.label, spawned, len(self.items)))


def flush_all():
    for b in list(ALL_BATCHES):
        b.flush()


def box(batch, x, y, z_base, w, d, h, yaw=0.0, pitch=0.0):
    """Cube sitting on z_base: w along local X, d along local Y, h up."""
    batch.add(tf(x, y, z_base + h / 2.0, yaw, pitch, w, d, h))


# ------------------------------------------------------------------ load data
with open(DATA_PATH, "r") as f:
    L = json.load(f)
log("layout loaded: %s" % json.dumps(L["stats"]))

# ------------------------------------------------------------------ progress dialog + clear previous
try:
    slow = unreal.ScopedSlowTask(9, "Road to Dawn: building the level")
    slow.make_dialog(False)
    slow.__enter__()
except Exception:
    slow = None


def step(msg):
    log(msg)
    if slow is not None:
        try:
            slow.enter_progress_frame(1, msg)
        except Exception:
            pass


step("1/9 looking for the previous run")
if CLEAR_PREVIOUS:
    old = []
    for a in actor_sub.get_all_level_actors():
        try:
            if str(a.get_folder_path()).startswith(ROOT_FOLDER):
                old.append(a)
        except Exception:
            pass
    log("previous run: %d actors to remove" % len(old))
    if old:
        try:
            actor_sub.destroy_actors(old)        # ONE bulk call; deleting actors one by one is extremely slow in a big level
        except Exception as ex:  # noqa
            warn("bulk delete failed (%s) - deleting one by one" % ex)
            for a in old:
                actor_sub.destroy_actor(a)
    log("previous run removed")

# ------------------------------------------------------------------ does the Landscape match the layout?
def ground_z(x_east, y_north):
    """Landscape height (m) under a plan point via a line trace, or None if the trace API is not available."""
    try:
        world = unreal.EditorLevelLibrary.get_editor_world()
        res = unreal.SystemLibrary.line_trace_single(world, vec(x_east, y_north, 400.0), vec(x_east, y_north, -200.0),
                                                     unreal.TraceTypeQuery.TRACE_TYPE_QUERY1, False, [], unreal.DrawDebugTrace.NONE, True)
        if isinstance(res, tuple):
            blocking, hit = res[0], res[-1]
        else:
            hit = res
            blocking = hit.get_editor_property("blocking_hit")
        if not blocking:
            return None
        return hit.get_editor_property("impact_point").z / 100.0
    except Exception as ex:  # noqa
        warn("ground trace failed: %s" % ex)
        return None


LAND = {}
_ps = L["player_start"]
_rp = L["road"]["points"][-1]
for _name, _x, _y, _z in (("start", _ps["x"], _ps["y"], _ps["z"]), ("road end", _rp[0], _rp[1], _rp[2]), ("village centre", 0.0, 0.0, 0.0)):
    _g = ground_z(_x, _y)
    LAND[_name] = _g
    if _g is None:
        warn("CHECK %s: no Landscape hit under (%.0f, %.0f) - Landscape missing, wrong Location, or no collision yet" % (_name, _x, _y))
    elif abs(_g - _z) > 1.5:
        warn("CHECK %s: layout height %.1f m, Landscape height %.1f m - they DO NOT match. Re-import the heightmap (Location 0,0,0, Scale 100) and run this script again" % (_name, _z, _g))
    else:
        log("CHECK %s: layout %.1f m, Landscape %.1f m - OK" % (_name, _z, _g))

# ------------------------------------------------------------------ roads / streets
step("2/9 roads")
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
if L.get("trail"):                               # footpath from the end of the road up to the start ledge
    ribbon(roads, L["trail"]["points"], L["trail"]["width"], step=1)
oldr = L.get("old_road")
if oldr:                                         # abandoned road behind the closed north gate: gets narrower and patchy, then is lost in the forest
    op = oldr["points"]
    n_op = len(op)
    i_fade = int(n_op * 0.4)
    for k in range(0, n_op - 1):
        frac = max(0.0, (k - i_fade) / float(max(1, n_op - 1 - i_fade)))
        if frac > 0.0 and ((k * 7919) % 100) / 100.0 < frac * 0.9:
            continue
        ribbon(roads, op[k:k + 2], oldr["width"] * (1.0 - 0.4 * frac), step=1)
log("roads: %d segments" % roads.count)

# ------------------------------------------------------------------ river
step("3/9 river")
water = Batch("RTD_River", "River", CUBE, M["River"])
ribbon(water, L["river"]["points"], L["river"]["width"], thick=0.3, step=1, lift=-0.1)
if L.get("creek"):
    ribbon(water, L["creek"]["points"], L["creek"]["width"], thick=0.3, step=1, lift=-0.1)
log("river: %d segments" % water.count)

# ------------------------------------------------------------------ walls, gates, towers
step("4/9 walls and gates")
fence = Batch("RTD_Walls", "Walls", CUBE, M["Fence"])
for w in L["walls"]:
    th = w["t"]
    box(fence, w["x"], w["y"], w["z"], w["len"], th, w["h"], w["yaw"])
for g in L["gates"]:
    if g.get("open"):
        # gate stands open: two leaves swung 90 degrees inwards (towards the settlement), the passage stays free
        for sx in (-1, 1):
            box(fence, g["x"] + sx * (g["w"] / 2 - 0.3), g["y"] + g["w"] / 4.0, g["z"], g["w"] / 2.0, 0.6, g["h"], 90.0)
    else:
        box(fence, g["x"], g["y"], g["z"], g["w"], 0.9, g["h"], 0.0)                  # closed gate leaf
    box(fence, g["x"] - g["w"] / 2 - 0.6, g["y"], g["z"], 1.2, 1.6, g["h"] + 1.5, 0.0)  # gate posts
    box(fence, g["x"] + g["w"] / 2 + 0.6, g["y"], g["z"], 1.2, 1.6, g["h"] + 1.5, 0.0)
bld = Batch("RTD_Buildings", "Buildings", CUBE, M["Buildings"])
for t in L["towers"]:
    box(bld, t["x"], t["y"], t["z"], t["size"], t["size"], t["h"])
    box(bld, t["x"], t["y"], t["z"] + t["h"], t["size"] + 2.0, t["size"] + 2.0, 3.0)  # watch cabin
log("walls: %d segments" % fence.count)

# ------------------------------------------------------------------ village
step("5/9 village, facility, mine")
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
fa = L.get("facility")
if fa:
    box(bld, fa["x"], fa["y"], fa["z"], fa["w"], fa["d"], fa["h"], fa["yaw"])
    box(bld, fa["x"], fa["y"], fa["z"] + fa["h"], fa["w"] * 0.4, fa["d"] * 0.5, 6.0, fa["yaw"])   # roof block
if "mine" in L:                                  # old mine: portal, tunnel shell, rails, cart, shed
    mine = Batch("RTD_Mine", "Mine", CUBE, M["Buildings"])
    for m in L["mine"]["boxes"]:
        box(mine, m["x"], m["y"], m["z"], m["w"], m["d"], m["h"], m["yaw"])
    log("mine: %d boxes" % mine.count)
if L.get("tunnel_portal"):                       # collapsed tunnel under the start cliff: portal frame + rockfall
    portal = Batch("RTD_TunnelPortal", "Start", CUBE, M["Buildings"])
    for m in L["tunnel_portal"]["boxes"]:
        box(portal, m["x"], m["y"], m["z"], m["w"], m["d"], m["h"], m["yaw"])
    rocks_c = Batch("RTD_Rockfall_Cubes", "Start", CUBE, M["Buildings"])
    rocks_s = Batch("RTD_Rockfall_Spheres", "Start", SPHERE, M["Buildings"])
    for r in L.get("rockfall", []):
        t = tf(r["x"], r["y"], r["z"], r["yaw"], r["pitch"], r["sx"], r["sy"], r["sz"], roll=r["roll"])
        (rocks_c if r["shape"] == "cube" else rocks_s).add(t)
    log("rockfall: %d boulders" % (rocks_c.count + rocks_s.count))
lamps = Batch("RTD_Lamps", "Props", CYL, M["Fence"])
for p in L["lamps"]:
    lamps.add(tf(p["x"], p["y"], p["z"] + 2.5, 0.0, 0.0, 0.3, 0.3, 5.0))
log("buildings: %d" % bld.count)

# ------------------------------------------------------------------ trees
step("6/9 putting structures into the level")
flush_all()
step("7/9 trees")
if BUILD_TREES:
    trunks = Batch("RTD_TreeTrunks", "Forest", CYL, M["Forest"])
    crowns = Batch("RTD_TreeCrowns", "Forest", CONE, M["Forest"])
    for (x, y, z, s) in L["trees"]:
        trunks.add(tf(x, y, z + 1.6 * s, 0.0, 0.0, 0.55 * s, 0.55 * s, 3.2 * s))
        crowns.add(tf(x, y, z + (3.0 + 6.0) * s, 0.0, 0.0, 4.2 * s, 4.2 * s, 12.0 * s))
    log("trees: %d" % trunks.count)
    flush_all()

# ------------------------------------------------------------------ gameplay actors
step("8/9 PlayerStart, binoculars, trap, navmesh")
if BUILD_GAMEPLAY:
    ps = L["player_start"]
    _base = LAND.get("start")
    if _base is None:
        _base = ps["z"]
    log("PlayerStart height: %.1f m (layout %.1f m)" % (_base + 1.0, ps["z"]))
    start = actor_sub.spawn_actor_from_class(unreal.PlayerStart, vec(ps["x"], ps["y"], _base + 1.0),
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

    # opening scene: the Redneck walks out of the forest onto the slab (C++ class AIntroSceneDirector, waypoints from the layout)
    _intro = L.get("intro")
    if _intro and _intro.get("waypoints"):
        _w = _intro["waypoints"]
        _dir = try_spawn("/Script/MyCharacters.IntroSceneDirector", {"x": _w[-1][0], "y": _w[-1][1], "z": _w[-1][2]}, "RTD_IntroScene")
        if _dir is not None:
            try:
                _dir.set_editor_property("waypoints", [vec(p[0], p[1], p[2]) for p in _w])
                log("intro scene: %d waypoints" % len(_w))
            except Exception as ex:  # noqa
                warn("intro waypoints not set: %s" % ex)

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

if slow is not None:
    try:
        slow.__exit__(None, None, None)
    except Exception:
        pass
log("DONE. PlayerStart stands on the cliff above the collapsed tunnel; a footpath leads down to the road, the south gate is open, the north gate is closed.")
