"""Task-owned normal lobby/R0/field smoke; run through LocalPythonRunner.

Uses actual buttons, public HUD text, possessed Pawn and navigation queries.
Never writes actor positions, movement, map data, time, or collision. Owner RPC
structs are not exposed by this build's Python wrapper: route walking is NOT
claimed. On failure leaves the task-owned PIE for inspection; cancel() ends it.
"""
import json
import math
import time
import traceback
from pathlib import Path
import unreal

ROUTE = [
    ('central_arena', (0, 0, 0)),
    ('church_east_lower', (1300, -2400, 0)),
    ('church_east_upper', (-650, -2400, 500)),
    ('church_square', (-1800, -2400, 500)),
    ('main_downhill_upper', (-2700, -2400, 500)),
    ('main_downhill_lower', (-4800, -2400, 0)),
    ('main_downhill_return_upper', (-2700, -2400, 500)),
    ('school_south_route', (-3100, -3450, 500)),
    ('school_northwest_upper', (-3300, -4450, 500)),
    ('school_northwest_lower', (-4800, -4450, 0)),
    ('coast_return_mouth', (-5500, -700, 0)),
    ('pier_return_turn', (-8300, -700, 0)),
    ('pier_end_north', (-8300, 900, 0)),
    ('pier_end_east', (-7750, 1500, 0)),
    ('pier_main_shore', (-5500, 1500, 0)),
]
NOT_COVERED = [
    'live player route walking: LDMoveIdentity/LDSpatialOrderRequest not exposed to Python',
    'later rests / PK / field 2 / field 3 / final showdown',
    'remote owning connection and packet prediction',
    'physical mouse / keyboard input',
    'combat balance and reward fairness',
]


class CityFlowSmoke:
    def __init__(self):
        expected = (Path(__file__).resolve().parent.parent / 'Lockdown.uproject').resolve()
        actual = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.get_project_file_path())).resolve()
        assert str(actual).casefold() == str(expected).casefold(), 'Wrong project: ' + str(actual)
        assert not self.world(), 'Refusing to join or stop another PIE session'
        prior = getattr(unreal, '_city_flow_smoke', None)
        assert not prior or prior.finished, 'A city smoke is already running'
        editor_world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
        assert editor_world.get_name() == 'L_Demo'
        self.output = Path('D:/MonkeyAI/output/quick-run-repair/flow-smoke.json')
        self.output.parent.mkdir(parents=True, exist_ok=True)
        self.project = str(actual)
        self.started = time.monotonic()
        self.due = self.started
        self.events = []
        self.actions = ['AICount3', 'Ready', 'StartMatch', 'HUDRestReady']
        self.action_index = 0
        self.field_at = None
        self.finished = False
        self.ending = False
        self.handle = unreal.register_slate_post_tick_callback(self.tick)
        self.write('running')
        unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_begin_play()

    @staticmethod
    def world():
        worlds = [w for w in unreal.ObjectIterator(unreal.World) if '/UEDPIE_' in w.get_path_name()]
        assert len(worlds) <= 1, 'Expected one standalone PIE world'
        return worlds[0] if worlds else None

    def write(self, status, **extra):
        self.output.write_text(json.dumps(dict(status=status, project=self.project,
            elapsed_wall_seconds=round(time.monotonic() - self.started, 2), events=self.events,
            not_covered=NOT_COVERED, real_route_walking_verified=False, **extra),
            ensure_ascii=False, indent=2), encoding='utf-8')

    def snapshot(self, world, marker, texts):
        pc = unreal.GameplayStatics.get_player_controller(world, 0)
        assert pc and pc.is_local_controller() and pc.has_authority(), 'Expected local authoritative host'
        pawn = pc.get_controlled_pawn()
        assert pawn and isinstance(pawn, unreal.LDCharacter), 'No possessed LDCharacter'
        assert pc.get_view_target() == pawn, 'Camera is not following possessed Pawn'
        hud = unreal.WidgetLibrary.get_all_widgets_of_class(world, unreal.LDHUD, False)
        assert len(hud) == 1 and hud[0].is_visible(), 'Missing/hidden HUD'
        cap = pawn.get_component_by_class(unreal.CapsuleComponent)
        p = pawn.get_actor_location()
        foot = [p.x, p.y, p.z - cap.get_scaled_capsule_half_height()]
        self.events.append(dict(event=marker, world_seconds=unreal.GameplayStatics.get_time_seconds(world),
            pawn=pawn.get_path_name(), view_target=pc.get_view_target().get_path_name(), foot=foot,
            capsule_radius_cm=cap.get_scaled_capsule_radius(), capsule_half_height_cm=cap.get_scaled_capsule_half_height(),
            hud_visible=True, hud_phase_texts=[s for s in texts if '野外' in s or '准备' in s or '休息' in s]))
        self.write('running')
        return pawn, foot

    def check_paths(self, world, pawn, foot):
        capture = next(a for a in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.LDLayoutPoint) if 'LDSceneAnchor:capture' in [str(t) for t in a.tags])
        goal = capture.get_actor_location()
        start = unreal.NavigationSystemV1.project_point_to_navigation(world, unreal.Vector(*foot), None, None, unreal.Vector(40,40,100))
        pairs = [('field_spawn_to_center', [start.x,start.y,start.z] if start else foot, [goal.x,goal.y,goal.z])]
        results = []
        for name, start, end in pairs:
            path = unreal.NavigationSystemV1.find_path_to_location_synchronously(world,
                unreal.Vector(*start), unreal.Vector(*end), pawn)
            valid = bool(path and path.is_valid() and not path.is_partial())
            points = [[p.x, p.y, p.z] for p in path.path_points] if path else []
            results.append(dict(name=name, complete=valid, points=points,
                length_cm=round(sum(math.dist(a, b) for a, b in zip(points, points[1:])), 1)))
        return results

    def tick(self, delta):
        if time.monotonic() < self.due:
            return
        self.due = time.monotonic() + .25
        try:
            world = self.world()
            if self.ending:
                if not world:
                    unreal.unregister_slate_post_tick_callback(self.handle)
                return
            assert time.monotonic() - self.started < 150, 'Timed out waiting for stable field 1'
            if not world:
                return
            roots = unreal.WidgetLibrary.get_all_widgets_of_class(world, unreal.UserWidget, False)
            prefixes = tuple(w.get_path_name() + '.' for w in roots)
            texts = [str(t.get_text()) for t in unreal.ObjectIterator(unreal.TextBlock)
                     if t.get_path_name().startswith(prefixes)]
            if self.action_index < len(self.actions):
                name = self.actions[self.action_index]
                buttons = [b for b in unreal.ObjectIterator(unreal.Button) if b.get_name() == name
                           and b.get_path_name().startswith(prefixes) and b.is_visible() and b.get_is_enabled()]
                assert len(buttons) <= 1, 'Ambiguous button ' + name
                if buttons:
                    if name == 'HUDRestReady':
                        self.snapshot(world, 'opening rest: possessed Pawn/camera/HUD', texts)
                    buttons[0].call_method('Execute')
                    self.action_index += 1
                    self.events.append(dict(event='normal UI action', button=name,
                        world_seconds=unreal.GameplayStatics.get_time_seconds(world)))
                    self.write('running')
                return
            phase = next((s for s in texts if '野外' in s and '1/3' in s), None)
            if not phase:
                return
            if self.field_at is None:
                self.field_at = time.monotonic()
            if time.monotonic() - self.field_at < 3:
                return
            pawn, foot = self.snapshot(world, 'field 1 stable: possessed Pawn/camera/HUD', texts)
            paths = self.check_paths(world, pawn, foot)
            monsters=unreal.GameplayStatics.get_all_actors_of_class(world, unreal.LDCampMemberCharacter)
            assert len(monsters)>0, 'No authored monsters spawned'
            self.events.append(dict(event='authored monster instances spawned', count=len(monsters)))
            self.finished = True
            self.ending = True
            self.write('passed' if all(p['complete'] for p in paths) else 'flow_passed_path_query_failed',
                verified=['normal lobby -> opening rest -> field 1', 'real possessed LDCharacter',
                          'following ViewTarget', 'visible HUD', 'runtime capsule dimensions'],
                phase=phase, navigation_queries=paths,
                path_query_scope='read-only baked path query in real PIE; not live-body traversal')
            unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_end_play()
        except Exception as exc:
            self.finished = True
            unreal.unregister_slate_post_tick_callback(self.handle)
            self.write('failed', error=str(exc), traceback=traceback.format_exc(), pie_left_open=True)
            unreal.log_error('City flow smoke failed: ' + str(exc))

    def cancel(self):
        if not self.finished:
            self.finished = True
            unreal.unregister_slate_post_tick_callback(self.handle)
            self.write('cancelled')
        if self.world():
            unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_end_play()


unreal._city_flow_smoke = CityFlowSmoke()
print('Normal city flow smoke registered: ' + str(unreal._city_flow_smoke.output))

