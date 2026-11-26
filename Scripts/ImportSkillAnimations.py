"""Import the chosen skill and pose animations and retarget them onto the hooper (docs/systems/AUDIO_AND_FEEL.md, animation).

Run inside the editor (no rendering needed):
    UnrealEditor-Cmd.exe Lockdown.uproject -run=pythonscript -script="<repo>/Scripts/ImportSkillAnimations.py" -nullrhi -unattended

Sources: SourceData/Animation/Quaternius/UAL1_Standard.glb and UAL2_Standard.glb (Universal Animation Library 1 and 2,
Standard, CC0). Both libraries use UE Manny bone names; IK rigs with the auto-generated retarget chains map them onto
SK_Hooper, and the chosen clips are written as /Game/Lockdown/Art/Characters/hooper/Quaternius/Hooper/QN_<clip>; the
imported libraries, their rigs and the retargeters are temporary. Rerunning replaces the same assets. Add a clip by naming it in CLIPS.

Mixamo (local-only: its animations may not be redistributed, so neither the FBX nor anything retargeted from it is
committed): every Content/ThirdParty/Fab/Mixamo/Source/<Name>.fbx (the local store linked by Scripts/ThirdPartyAssets.py;
Mixamo's default character, FBX Binary with skin, 30 fps) is retargeted the same way to
/Game/ThirdParty/Fab/Mixamo/Hooper/MX_<Name>. Without the store nothing is written.
"""
import glob
import os
import unreal

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
SOURCES = {'UAL1': 'SourceData/Animation/Quaternius/UAL1_Standard.glb', 'UAL2': 'SourceData/Animation/Quaternius/UAL2_Standard.glb'}
DEST = '/Game/Lockdown/Art/Characters/hooper/Quaternius'
# Local-only (Scripts/ThirdPartyAssets.py links Content/ThirdParty/Fab to $LD_THIRDPARTY_ROOT/Fab).
MIXAMO_DEST = '/Game/ThirdParty/Fab/Mixamo'
MIXAMO_SOURCE = os.path.join(ROOT, 'Content', 'ThirdParty', 'Fab', 'Mixamo', 'Source')
# A Mixamo clip that starts standing, measured as the Mixamo rest pelvis height.
MIXAMO_STANDING = 'Standing_Taunt_Chest_Thump'
TARGET_MESH = '/Game/Lockdown/Art/Characters/hooper/SK_Hooper'
HOOPER_IDLE = '/Game/Lockdown/Art/Characters/hooper/Hooper_Idle'
# A clip standing upright at its first frame, measured as the source's rest pelvis height.
STANDING_CLIP = 'Idle_FoldArms_Loop'
CLIPS = {
    'UAL1': ['Push_Loop', 'Punch_Jab', 'Punch_Cross', 'Spell_Simple_Shoot', 'Dance_Loop', 'Sword_Idle'],
    'UAL2': ['Idle_Shield_Loop', 'Sword_Block', 'Shield_OneShot', 'Shield_Dash', 'Melee_Hook', 'OverhandThrow', 'Slide_Start',
             'Idle_FoldArms_Loop', 'Yes', 'Idle_TalkingPhone_Loop'],
}

tools = unreal.AssetToolsHelpers.get_asset_tools()
registry = unreal.AssetRegistryHelpers.get_asset_registry()


def fail(why):
    unreal.log_error('LD_QUATERNIUS_FAIL ' + why)
    raise RuntimeError(why)


def assets_under(path, cls):
    registry.scan_paths_synchronous([path], True)
    return [a for a in registry.get_assets_by_path(path, recursive=True) if str(a.asset_class_path.asset_name) == cls]


def import_library(name, filename=None, path=None):
    path = path or DEST + '/' + name
    task = unreal.AssetImportTask()
    task.filename = filename or os.path.join(ROOT, SOURCES[name])
    task.destination_path = path
    task.automated = True
    task.replace_existing = True
    task.save = True
    tools.import_asset_tasks([task])
    meshes = assets_under(path, 'SkeletalMesh')
    anims = assets_under(path, 'AnimSequence')
    if not meshes or not anims:
        fail('%s imported no skeletal mesh or no animations (%d, %d)' % (name, len(meshes), len(anims)))
    return meshes[0].get_asset(), anims


def ik_rig(asset_name, mesh):
    path = DEST + '/Rigs'
    full = path + '/' + asset_name
    existing = unreal.EditorAssetLibrary.load_asset(full) if unreal.EditorAssetLibrary.does_asset_exist(full) else None
    rig = existing or tools.create_asset(asset_name, path, unreal.IKRigDefinition, unreal.IKRigDefinitionFactory())
    controller = unreal.IKRigController.get_controller(rig)
    if not controller.set_skeletal_mesh(mesh) or not controller.apply_auto_generated_retarget_definition():
        fail('IK rig %s could not take its mesh or its auto-generated retarget chains' % asset_name)
    unreal.EditorAssetLibrary.save_loaded_asset(rig)
    return rig


def retargeter(asset_name, source_rig, target_rig, source_mesh, target_mesh):
    path = DEST + '/Rigs'
    full = path + '/' + asset_name
    existing = unreal.EditorAssetLibrary.load_asset(full) if unreal.EditorAssetLibrary.does_asset_exist(full) else None
    rtg = existing or tools.create_asset(asset_name, path, unreal.IKRetargeter, unreal.IKRetargetFactory())
    controller = unreal.IKRetargeterController.get_controller(rtg)
    if not existing:
        controller.add_default_ops()
    controller.set_ik_rig(unreal.RetargetSourceOrTarget.SOURCE, source_rig)
    controller.set_ik_rig(unreal.RetargetSourceOrTarget.TARGET, target_rig)
    controller.set_preview_mesh(unreal.RetargetSourceOrTarget.SOURCE, source_mesh)
    controller.set_preview_mesh(unreal.RetargetSourceOrTarget.TARGET, target_mesh)
    controller.assign_ik_rig_to_all_ops(unreal.RetargetSourceOrTarget.SOURCE, source_rig)
    controller.assign_ik_rig_to_all_ops(unreal.RetargetSourceOrTarget.TARGET, target_rig)
    controller.auto_map_chains(unreal.AutoMapChainType.FUZZY, True)
    # The two characters' rest poses differ (bind poses, bent joints): align the hooper's retarget pose to the source's
    # chain directions so rotations transfer from matching poses.
    controller.auto_align_all_bones(unreal.RetargetSourceOrTarget.TARGET)
    unreal.EditorAssetLibrary.save_loaded_asset(rtg)
    return rtg


def fit_to_hooper(anim, root, pelvis_scale):
    """The hooper rig keeps a scaled root (its bones are authored in metres under a ~182x root): the retargeter writes the
    root unscaled and the pelvis in the source's centimetres, which folds the legs. The hooper's own root transform is
    written back and the pelvis translation is scaled into the hooper's units."""
    controller = anim.controller
    frames = unreal.AnimationLibrary.get_num_frames(anim) + 1
    positions, rotations, scales = [], [], []
    for frame in range(frames):
        pose = unreal.AnimationLibrary.get_bone_pose_for_frame(anim, 'pelvis', frame, False)
        positions.append(pose.translation * pelvis_scale)
        rotations.append(pose.rotation)
        scales.append(pose.scale3d)
    controller.set_bone_track_keys('pelvis', positions, rotations, scales, False)
    controller.set_bone_track_keys('root', [root.translation] * frames, [root.rotation] * frames, [root.scale3d] * frames, False)


def main():
    target_mesh = unreal.EditorAssetLibrary.load_asset(TARGET_MESH)
    if not target_mesh:
        fail('missing ' + TARGET_MESH)
    target_rig = ik_rig('IKR_Hooper', target_mesh)
    written = []
    for name, wanted in CLIPS.items():
        mesh, anims = import_library(name)
        source_rig = ik_rig('IKR_Quaternius_' + name, mesh)
        rtg = retargeter('RTG_Quaternius_%s_To_Hooper' % name, source_rig, target_rig, mesh, target_mesh)
        by_clip = {}
        for a in anims:
            clip = str(a.asset_name)
            for w in wanted:
                # Interchange names each clip <file stem><clip>, e.g. UAL1_StandardPunch_Jab.
                if clip == w or clip.endswith(w):
                    by_clip[w] = a
        missing = [w for w in wanted if w not in by_clip]
        if missing:
            fail('%s has no clip %s (imported: %s)' % (name, missing, sorted(str(a.asset_name) for a in anims)[:60]))
        inputs = unreal.IKRetargetBatchOperationInputs()
        inputs.assets_to_retarget = [by_clip[w] for w in wanted]
        inputs.source_mesh = mesh
        inputs.target_mesh = target_mesh
        inputs.ik_retarget_asset = rtg
        inputs.prefix = 'QN_'
        inputs.search = name + '_Standard'
        inputs.replace = ''
        inputs.target_path = DEST + '/Hooper'
        inputs.include_referenced_assets = False
        inputs.overwrite_existing_files = True
        out = unreal.IKRetargetBatchOperation.run_batch_retarget(inputs)
        written += [str(a.package_name) for a in out]
    # The hooper's rest root and pelvis height (its own idle, first frame) against a standing source pose.
    idle = unreal.EditorAssetLibrary.load_asset(HOOPER_IDLE)
    root = unreal.AnimationLibrary.get_bone_pose_for_frame(idle, 'root', 0, False)
    hooper_pelvis = unreal.AnimationLibrary.get_bone_pose_for_frame(idle, 'pelvis', 0, False).translation.z
    standing = unreal.EditorAssetLibrary.load_asset(DEST + '/Hooper/QN_' + STANDING_CLIP)
    source_pelvis = unreal.AnimationLibrary.get_bone_pose_for_frame(standing, 'pelvis', 0, False).translation.z
    if not (hooper_pelvis > 0 and source_pelvis > 0):
        fail('no rest pelvis height to scale by (hooper %.3f, source %.3f)' % (hooper_pelvis, source_pelvis))
    for package in written:
        fit_to_hooper(unreal.EditorAssetLibrary.load_asset(package), root, hooper_pelvis / source_pelvis)
    # Only the retargeted clips and the hooper's IK rig are kept: the imported libraries, their rigs and the retargeters
    # are rebuilt from SourceData on every run.
    for name in CLIPS:
        unreal.EditorAssetLibrary.delete_asset(DEST + '/Rigs/RTG_Quaternius_%s_To_Hooper' % name)
        unreal.EditorAssetLibrary.delete_asset(DEST + '/Rigs/IKR_Quaternius_' + name)
        unreal.EditorAssetLibrary.delete_directory(DEST + '/' + name)
    unreal.EditorAssetLibrary.save_directory(DEST, only_if_is_dirty=False, recursive=True)
    unreal.log_warning('LD_QUATERNIUS_OK %d retargeted: %s' % (len(written), ' '.join(written)))
    import_mixamo(target_rig, target_mesh, root, hooper_pelvis)


def import_mixamo(target_rig, target_mesh, root, hooper_pelvis):
    files = sorted(glob.glob(os.path.join(MIXAMO_SOURCE, '*.fbx')))
    if not files:
        unreal.log_warning('LD_MIXAMO_SKIPPED no Content/ThirdParty/Fab/Mixamo/Source/*.fbx (local store not linked)')
        return
    written = {}
    for fbx in files:
        stem = os.path.splitext(os.path.basename(fbx))[0]
        mesh, anims = import_library(stem, fbx, MIXAMO_DEST + '/Source/' + stem)
        source_rig = ik_rig('IKR_Mixamo_' + stem, mesh)
        rtg = retargeter('RTG_Mixamo_%s_To_Hooper' % stem, source_rig, target_rig, mesh, target_mesh)
        inputs = unreal.IKRetargetBatchOperationInputs()
        inputs.assets_to_retarget = anims[:1]
        inputs.source_mesh = mesh
        inputs.target_mesh = target_mesh
        inputs.ik_retarget_asset = rtg
        inputs.target_path = MIXAMO_DEST + '/Hooper'
        inputs.include_referenced_assets = False
        inputs.overwrite_existing_files = True
        out = unreal.IKRetargetBatchOperation.run_batch_retarget(inputs)
        if not out:
            fail('Mixamo %s did not retarget' % stem)
        final = MIXAMO_DEST + '/Hooper/MX_' + stem
        unreal.EditorAssetLibrary.delete_asset(final) if unreal.EditorAssetLibrary.does_asset_exist(final) else None
        unreal.EditorAssetLibrary.rename_asset(str(out[0].package_name), final)
        written[stem] = final
        unreal.EditorAssetLibrary.delete_asset(DEST + '/Rigs/RTG_Mixamo_%s_To_Hooper' % stem)
        unreal.EditorAssetLibrary.delete_asset(DEST + '/Rigs/IKR_Mixamo_' + stem)
    if MIXAMO_STANDING not in written:
        fail('the Mixamo store has no %s.fbx to measure its rest pelvis by' % MIXAMO_STANDING)
    standing = unreal.EditorAssetLibrary.load_asset(written[MIXAMO_STANDING])
    source_pelvis = unreal.AnimationLibrary.get_bone_pose_for_frame(standing, 'pelvis', 0, False).translation.z
    if not source_pelvis > 0:
        fail('no Mixamo rest pelvis height')
    for final in written.values():
        fit_to_hooper(unreal.EditorAssetLibrary.load_asset(final), root, hooper_pelvis / source_pelvis)
    unreal.EditorAssetLibrary.delete_directory(MIXAMO_DEST + '/Source')
    unreal.EditorAssetLibrary.save_directory(MIXAMO_DEST, only_if_is_dirty=False, recursive=True)
    unreal.log_warning('LD_MIXAMO_OK %d retargeted: %s' % (len(written), ' '.join(written.values())))


main()
