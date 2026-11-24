"""Road to Dawn - prologue places (task 04, stage 4) for Unreal Engine 5 (Python Editor Script).

Places the prologue director and its markers on Lvl_RoadToDawn_Terrain from rtd_prologue.json:
  - RTD_PrologueDirector (APrologueDirector) with its scenario asset,
  - TargetPoint markers with tags (PL_...), which the scenario steps find by tag,
  - extra NavMeshBoundsVolumes (the Rider's river bank is outside the main one).

Runs on its own in a few seconds (File > Execute Python Script... > this file), and rtd_build_level.py runs it at the
end of a full rebuild. Everything goes into the Outliner folder "RoadToDawn_Gen/Prologue"; a re-run deletes that
folder first, so move markers by editing rtd_prologue.json, not in the level (or ask Claude to copy the positions from
the level back into the json).

Plan coordinates: x = east, y = north (metres). UE: X = north, Y = east.
"""
import json
import math
import os

import unreal

DATA_PATH = os.path.join(unreal.Paths.project_dir(), "ForImport", "RoadToDawn", "rtd_prologue.json")
FOLDER = "RoadToDawn_Gen/Prologue"

actor_sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
log = lambda m: unreal.log("[RTD prologue] " + str(m))
warn = lambda m: unreal.log_warning("[RTD prologue] " + str(m))


def vec(x_east, y_north, z):
    return unreal.Vector(y_north * 100.0, x_east * 100.0, z * 100.0)


_TRACE_BROKEN = []   # set after the first failure: the rest of the markers use their json z without retrying


def _hit_point(hit):
    """(blocking, impact point) of a HitResult. In UE 5.8 Python neither 'blocking_hit' nor 'impact_point' can be read
    from a HitResult, and GameplayStatics.break_hit_result failed too (24.11.2026: every marker fell back to its json
    height). Tried in order; when all fail the caller logs the fields the HitResult does have, once, so the right
    access can be found on the next run."""
    try:
        parts = unreal.GameplayStatics.break_hit_result(hit)
        return bool(parts[0]), parts[5]
    except Exception:  # noqa
        pass
    try:
        parts = hit.to_tuple()
        return bool(parts[0]), parts[5]
    except Exception:  # noqa
        pass
    point = hit.get_editor_property("impact_point")
    return (point.x != 0.0 or point.y != 0.0 or point.z != 0.0), point


def ground_z(x_east, y_north):
    """Height (m) of the first thing under a plan point, or None."""
    if _TRACE_BROKEN:
        return None
    hit = None
    try:
        world = unreal.EditorLevelLibrary.get_editor_world()
        res = unreal.SystemLibrary.line_trace_single(world, vec(x_east, y_north, 400.0), vec(x_east, y_north, -200.0),
                                                     unreal.TraceTypeQuery.TRACE_TYPE_QUERY1, False, [], unreal.DrawDebugTrace.NONE, True)
        if isinstance(res, tuple):
            if not res[0]:
                return None
            hit = res[-1]
        else:
            hit = res
        blocking, point = _hit_point(hit)
        if not blocking:
            return None
        return point.z / 100.0
    except Exception as ex:  # noqa
        _TRACE_BROKEN.append(True)
        fields = [a for a in dir(hit) if not a.startswith("_")] if hit is not None else []
        warn("ground trace does not work here (%s) - every marker uses its z from rtd_prologue.json. HitResult has: %s"
             % (ex, ", ".join(fields)))
        return None


def yaw_towards(x, y, face):
    """UE yaw from plan point (x, y) towards plan point face = [x, y]."""
    dx_east, dy_north = face[0] - x, face[1] - y
    return math.degrees(math.atan2(dx_east, dy_north))   # UE: X = north, Y = east


def build():
    with open(DATA_PATH, encoding="utf-8") as f:
        P = json.load(f)

    old = []
    for a in actor_sub.get_all_level_actors():
        try:
            if str(a.get_folder_path()).startswith(FOLDER):
                old.append(a)
        except Exception:
            pass
    if old:
        actor_sub.destroy_actors(old)
    log("previous prologue actors removed: %d" % len(old))

    # director
    d = P.get("director")
    if d:
        cls = unreal.load_class(None, "/Script/MyCharacters.PrologueDirector")
        if cls is None:
            warn("class APrologueDirector not found - build the project first")
        else:
            director = actor_sub.spawn_actor_from_class(cls, vec(d["x"], d["y"], d["z"]))
            director.set_actor_label(d.get("label", "RTD_PrologueDirector"))
            director.set_folder_path(FOLDER)
            scenario = unreal.load_asset(d["scenario"]) if d.get("scenario") else None
            if scenario is None:
                warn("scenario asset not found: %s - the director is placed without it" % d.get("scenario"))
            else:
                director.set_editor_property("scenario", scenario)
            for name, value in d.get("properties", {}).items():
                try:
                    director.set_editor_property(name, value)
                except Exception as ex:  # noqa
                    warn("director property %s not set: %s" % (name, ex))
            log("director placed, scenario %s" % d.get("scenario"))

    # markers
    placed = 0
    for m in P.get("markers", []):
        x, y = m["x"], m["y"]
        g = ground_z(x, y)
        z = (g if g is not None else m.get("z", 0.0)) + 0.05
        yaw = yaw_towards(x, y, m["face"]) if m.get("face") else 0.0
        a = actor_sub.spawn_actor_from_class(unreal.TargetPoint, vec(x, y, z), unreal.Rotator(roll=0.0, pitch=0.0, yaw=yaw))
        a.set_actor_label(m["tag"])
        a.set_folder_path(FOLDER)
        a.set_editor_property("tags", [unreal.Name(m["tag"])])
        if g is None and not _TRACE_BROKEN:
            warn("marker %s: no ground under (%.1f, %.1f), using z = %.1f" % (m["tag"], x, y, z))
        placed += 1
    log("markers placed: %d" % placed)

    # extra nav-mesh bounds
    for nv in P.get("navmesh_extra", []):
        try:
            vol = actor_sub.spawn_actor_from_class(unreal.NavMeshBoundsVolume, vec(nv["x"], nv["y"], 30.0))
            vol.set_actor_label(nv["label"])
            vol.set_actor_scale3d(unreal.Vector(nv["d"] * 100.0 / 200.0, nv["w"] * 100.0 / 200.0, 200.0 * 100.0 / 200.0))
            vol.set_folder_path(FOLDER)
        except Exception as ex:  # noqa
            warn("NavMeshBoundsVolume %s not created: %s" % (nv.get("label"), ex))
    # The nav-mesh is not rebuilt by itself for the new volumes (found 24.11.2026: the Rider had no path along the river).
    warn("DONE - now rebuild the nav-mesh: Output Log > Cmd > RebuildNavigation (check with P in the viewport), then save the level")


build()
