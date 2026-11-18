"""Deletes everything the Road to Dawn scripts created: all actors in the Outliner folder RoadToDawn_Gen (and subfolders)
plus any actor labelled RTD_*.  Landscape, lights and your own actors are not touched.
Run: File > Execute Python Script... > rtd_clear.py   (result in Output Log, filter RTD)"""
import unreal

sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
victims = []
for a in sub.get_all_level_actors():
    try:
        folder = str(a.get_folder_path())
        label = str(a.get_actor_label())
    except Exception:
        continue
    if folder.startswith("RoadToDawn_Gen") or label.startswith("RTD_"):
        victims.append(a)
unreal.log("[RTD] clear: %d actors to delete" % len(victims))
if victims:
    try:
        sub.destroy_actors(victims)
    except Exception as ex:  # noqa
        unreal.log_warning("[RTD] bulk delete failed (%s), deleting one by one" % ex)
        for a in victims:
            sub.destroy_actor(a)
left = [a for a in sub.get_all_level_actors() if str(a.get_folder_path()).startswith("RoadToDawn_Gen")]
unreal.log("[RTD] clear done, %d actors left in RoadToDawn_Gen" % len(left))
