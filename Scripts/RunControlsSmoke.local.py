import unreal,time,json,traceback
from pathlib import Path

class RunControlsSmoke:
    def __init__(self):
        assert not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor()
        self.out=Path('D:/MonkeyAI/output/run-controls-20261115/live-controls.json')
        self.started=time.monotonic();self.due=0;self.stage=0;self.events=[];self.actions=['AICount3','Ready','StartMatch','HUDRestReady'];self.action=0
        self.field=None;self.pause=None;self.resume=None;self.finished=False
        self.handle=unreal.register_slate_post_tick_callback(self.tick)
        self.write('running')
        unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_begin_play()
    def write(self,status,**extra):
        self.out.write_text(json.dumps({'status':status,'elapsed':time.monotonic()-self.started,'stage':self.stage,'events':self.events,**extra},ensure_ascii=False,indent=2),encoding='utf-8')
    def world(self):return unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
    def click(self,name):
        b=[b for b in unreal.ObjectIterator(unreal.Button) if b.get_name()==name and b.get_path_name().startswith(self.prefix)]
        if not b:return False
        assert len(b)==1,name
        b[0].call_method('Execute');self.events.append({'button':name,'world_seconds':unreal.GameplayStatics.get_time_seconds(self.w)})
        self.due=time.monotonic()+.65;return True
    def snapshot(self):
        pc=unreal.GameplayStatics.get_player_controller(self.w,0);pawn=pc.get_controlled_pawn()
        assert pawn and pc.get_view_target()==pawn and not pc.is_move_input_ignored(),'Player/camera/input unavailable'
        move=pawn.get_component_by_class(unreal.CharacterMovementComponent)
        assert move.get_editor_property('movement_mode')==unreal.MovementMode.MOVE_WALKING
        return {'world_seconds':unreal.GameplayStatics.get_time_seconds(self.w),'pawn':pawn.get_name(),'camera':pc.get_view_target().get_name(),
          'phase':next((t for t in self.texts if '野外 1/3' in t),''),
          'positions':{a.get_name():[a.get_actor_location().x,a.get_actor_location().y,a.get_actor_location().z] for a in unreal.GameplayStatics.get_all_actors_of_class(self.w,unreal.LDCharacter)}}
    def tick(self,dt):
        if self.finished or time.monotonic()<self.due:return
        self.due=time.monotonic()+.25
        try:
            assert time.monotonic()-self.started<120,'Timed out entering field or exercising controls'
            self.w=self.world()
            if not self.w:return
            roots=unreal.WidgetLibrary.get_all_widgets_of_class(self.w,unreal.UserWidget,False)
            self.prefix=tuple(r.get_path_name()+'.' for r in roots)
            self.texts=[str(t.get_text()) for t in unreal.ObjectIterator(unreal.TextBlock) if t.get_path_name().startswith(self.prefix)]
            if self.action<len(self.actions):
                name=self.actions[self.action]
                if name=='HUDRestReady':
                    pc=unreal.GameplayStatics.get_player_controller(self.w,0)
                    if not pc.get_controlled_pawn():return
                    pc.call_method('ServerEnableDevTest',args=(1,))
                    if self.click('DevTestSkipPreparation'):self.action+=1
                elif self.click(name):self.action+=1
                return
            phase=next((t for t in self.texts if '野外 1/3' in t),'')
            if not phase:return
            if self.stage==0:
                self.field=self.snapshot();self.events.append({'entered_field':self.field})
                if self.click('DevTestInvulnerableOn'):self.stage=1
            elif self.stage==1:
                if not any('生命不减 已生效' in t for t in self.texts):return
                if self.click('DevTestPauseCountdown'):self.stage=2
            elif self.stage==2:
                if '计时已暂停' not in phase:return
                self.pause=self.snapshot();self.stage=3;self.due=time.monotonic()+7
            elif self.stage==3:
                after=self.snapshot()
                assert after['phase']==self.pause['phase'],'Countdown changed while paused'
                assert after['world_seconds']-self.pause['world_seconds']>=5,'Native world was paused'
                moved=sum(1 for k,p in after['positions'].items() if k in self.pause['positions'] and sum((a-b)**2 for a,b in zip(p,self.pause['positions'][k]))>25)
                assert moved>0,'No moving actors while countdown paused'
                self.events.append({'pause_verified':True,'before':self.pause,'after':after,'moving_actors':moved})
                if self.click('DevTestResumeCountdown'):self.stage=4
            elif self.stage==4:
                if '计时已暂停' in phase:return
                self.resume=self.snapshot();self.stage=5;self.due=time.monotonic()+4
            elif self.stage==5:
                after=self.snapshot();assert after['phase']!=self.resume['phase'],'Countdown did not resume'
                self.events.append({'resume_verified':True,'before':self.resume['phase'],'after':after['phase']})
                if self.click('DevTestPauseCountdown'):self.stage=6
            elif self.stage==6:
                if '计时已暂停' not in phase:return
                self.snapshot();self.finished=True;unreal.unregister_slate_post_tick_callback(self.handle)
                self.write('passed',in_field=True,health_protection_enabled=True,countdown_paused=True,world_keeps_running=True,pie_left_running=True)
                unreal.log('RUN_CONTROLS_READY: field active, health protection on, countdown paused, native world running')
            self.write('passed' if self.finished else 'running',pie_left_running=True)
        except Exception as e:
            self.finished=True;unreal.unregister_slate_post_tick_callback(self.handle)
            self.write('failed',error=str(e),traceback=traceback.format_exc(),pie_left_running=True)
            unreal.log_error('Run controls smoke failed: '+str(e))
unreal._run_controls_smoke=RunControlsSmoke()
print('Run-map controls check registered; successful check leaves Play running with frozen countdown.')
