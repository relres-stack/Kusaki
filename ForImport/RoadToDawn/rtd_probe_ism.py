"""Quick test (a few seconds): can this Unreal build create an instanced mesh from Python?
Run it BEFORE rtd_build_level.py:  File > Execute Python Script...  Result is in the Output Log (filter "RTD").
It creates the helper Blueprint /Game/RoadToDawn/Gen/BP_RTD_ISM (keep it, the build script uses it) and spawns
one actor 'RTD_PROBE' with 3 cubes near the world origin. Delete that actor afterwards (Outliner folder RoadToDawn_Gen/Probe)."""
import unreal

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


sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
cube = unreal.load_asset("/Engine/BasicShapes/Cube")
try:
    cls = get_ism_class()
    unreal.log("[RTD] helper blueprint class ready: %s" % cls)
    actor = sub.spawn_actor_from_class(cls, unreal.Vector(0, 0, 200))
    actor.set_actor_label("RTD_PROBE")
    actor.set_folder_path("RoadToDawn_Gen/Probe")
    comp = actor.get_component_by_class(unreal.InstancedStaticMeshComponent)
    if comp is None:
        raise RuntimeError("spawned actor has no InstancedStaticMeshComponent")
    comp.set_static_mesh(cube)
    ts = [unreal.Transform(location=unreal.Vector(x * 300, 0, 0), rotation=unreal.Rotator(0, 0, 0), scale=unreal.Vector(1, 1, 1)) for x in range(3)]
    try:
        comp.add_instances(ts, False, False)
        unreal.log("[RTD] add_instances ok")
    except Exception as ex:  # noqa
        unreal.log("[RTD] add_instances not available (%s), trying add_instance" % ex)
        for t in ts:
            comp.add_instance(t)
    unreal.log("[RTD] PROBE RESULT: instance count = %d (3 expected)" % comp.get_instance_count())
except Exception as ex:  # noqa
    unreal.log_warning("[RTD] PROBE RESULT: FAILED: %s" % ex)
