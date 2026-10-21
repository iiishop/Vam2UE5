"""Transient capture world setup: no unrelated template pawn or pawn collisions.

Does not save project settings or change the tested BP_VamCharacter.
"""
import unreal as u
u.EditorPythonScripting.set_keep_python_script_alive(True)
u.get_default_object(u.GameMapsSettings).set_editor_property('global_default_game_mode',u.SystemLibrary.get_soft_class_path(u.GameModeBase))
u.get_default_object(u.GameModeBase).set_editor_property('default_pawn_class',None)
for cls in (u.DefaultPawn,u.SpectatorPawn):
    pawn=u.get_default_object(cls)
    pawn.set_actor_enable_collision(False)
    for mesh in pawn.get_components_by_class(u.MeshComponent):
        mesh.set_visibility(False,True)
u.log('LEG_CHAIN_CAPTURE_SETUP: transient GameModeBase with no default pawn')
