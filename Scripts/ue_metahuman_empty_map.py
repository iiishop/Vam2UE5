"""Test caller's empty map; no actors, source data or solver services."""
import unreal as u
path='/Game/MH00Verification/L_Empty'
assert not u.EditorAssetLibrary.does_asset_exist(path), 'Validation map already exists'
world=u.EditorLoadingAndSavingUtils.new_blank_map(False)
world.get_world_settings().set_editor_property('default_game_mode', u.GameModeBase)
assert u.EditorLoadingAndSavingUtils.save_map(world,path)
