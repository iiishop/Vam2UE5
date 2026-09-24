"""Fixed-time trajectory comparison through the actual Stage06 component API."""
import json,math,traceback
from pathlib import Path
import unreal as u

OUT=Path(u.Paths.project_dir(),'Plugins/VamResourceBrowser/Saved/NativeBuild/stage06-motion-check.json')

def smooth(x):
    x=max(0,min(1,x));return x*x*(3-2*x)

def trajectory(name,t):
    if name=='accelerate_cruise_brake':
        if t<1:return 30*t*t,0,0,0
        if t<2:return 30+60*(t-1),0,0,0
        if t<3:
            q=t-2;return 90+60*q-30*q*q,0,0,0
        return 120,0,0,0
    if name=='left_right_turn':return 0,22*math.sin(math.pi*t),0,0
    if name=='lift_drop':return 0,0,35*(smooth(t/2) if t<2 else 1-smooth((t-2)/2)),0
    return 0,0,0,75*(smooth(t/2) if t<2 else 1-smooth((t-2)/2))

try:
    u.EditorLoadingAndSavingUtils.new_blank_map(False)
    sub=u.get_editor_subsystem(u.EditorActorSubsystem)
    actor=sub.spawn_actor_from_class(u.VamCharacterActor,u.Vector())
    motion=actor.get_editor_property('motion')
    motion.set_preview_paused(True)
    start=actor.get_actor_transform()
    runs={}
    for name in ('accelerate_cruise_brake','left_right_turn','lift_drop','pure_rotation'):
        results={}
        for fps in (30,60,120):
            initial=u.Transform(location=start.translation,rotation=start.rotation.rotator())
            base=1000+len(runs)*100+fps
            motion.teleport_to(initial,base)
            peak=0
            for frame in range(1,7*fps+1):
                t=frame/fps
                x,y,z,yaw=trajectory(name,min(t,4))
                transform=u.Transform(location=start.translation+u.Vector(x,y,z),rotation=u.Rotator(0,yaw,0))
                motion.move_continuously(transform,base+t)
                for _ in range(120//fps):motion.step_preview()
                peak=max(peak,max(r.local_displacement.length() for r in motion.get_inertia_regions()))
            rest=max(r.local_displacement.length() for r in motion.get_inertia_regions())
            results[str(fps)]={'peak_cm':peak,'rest_cm':rest}
        reference=results['120']['peak_cm']
        for fps in ('30','60'):
            assert abs(results[fps]['peak_cm']-reference)<=max(2.,reference*.2),(name,results)
        assert all(v['rest_cm']<.5 for v in results.values()),(name,results)
        runs[name]=results
    motion.reset_preview()
    motion.set_preview_paused(False)
    motion.advance_solver_clock(.25)
    clock=motion.get_clock()
    assert clock.last_steps==8 and clock.dropped_steps>=20,clock
    assert clock.time_seconds<=u.SystemLibrary.get_game_time_in_seconds(actor)+8/120+.01
    OUT.write_text(json.dumps({'status':'passed','tolerance':'max(2cm, 20% of 120fps peak); rest<0.5cm after 3s',
                               'trajectories':runs,'stall':{'delta_s':.25,'executed_steps':clock.last_steps,
                                                              'dropped_steps':clock.dropped_steps}},indent=2),encoding='utf8')
except Exception:
    OUT.write_text(json.dumps({'status':'failed','error':traceback.format_exc()},indent=2),encoding='utf8')
    raise
