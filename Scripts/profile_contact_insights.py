from pathlib import Path
import subprocess,sys,json,re
r=Path(__file__).resolve().parents[1]/'Saved/ContactInsights'
for name in sys.argv[1:]:
 d=r/name;d.mkdir(exist_ok=True,parents=True)
 workers={'workers4':4,'workers8':8,'reference':100,'serial':100,'adapter5':100,'adapter1':100,'quiet':100,'fixed5':100,'fixed1':100,'metrics':100,'polar':100,'combined':100,'production':100,'geometry':100,'memo':100,'cache-final':100}[name]
 args=['I:/Program/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe','I:/Document/UE5/SmartNPC/Saved/VamBrowserBuild-7a653870e8e9470da44a4fa2a8dbffdc/HostProject/HostProject.uproject','-unattended','-nosplash','-VamBreastTestConfig=/Game/VamRuntime/R_619448199d803edf1ab83af9/RC_Runtime','-ExecCmds=p.Chaos.MaxNumWorkers '+str(workers)+',Automation RunTests Vam.Breast.ContactRuntime','-TestExit=Automation Test Queue Empty','-abslog='+str(d/'runtime.log'),'-VamContactGPU','-RenderOffscreen','-trace=cpu,frame,region,task','-tracefile='+str(d/'capture.utrace'),'-VamContactSnapshots='+str(d),'-statnamedevents']
 if name in ('reference','quiet','metrics','workers4','workers8','serial'):
  args=[a.replace(',Automation RunTests',',vam.Contact.NativeGSBatch 0,Automation RunTests') for a in args]
 if name in ('adapter5','adapter1','fixed5','fixed1'):
  args=[a.replace('p.Chaos.MaxNumWorkers 100,','vam.Contact.NativeGSBatch '+('5' if name in ('adapter5','fixed5') else '1')+',') for a in args]
 if name not in ('geometry','memo','cache-final'):args=[a.replace(',Automation RunTests',',vam.Contact.MaterialCache 0,Automation RunTests') for a in args]
 if name=='cache-final':args=[a.replace('p.Chaos.MaxNumWorkers 100,','vam.Contact.MaterialCache 2,') for a in args]
 if name in ('geometry','memo'):args=[a.replace('p.Chaos.MaxNumWorkers 100,','vam.Contact.MaterialCache '+('1' if name=='geometry' else '2')+',') for a in args]
 if name=='polar':args=[a.replace('p.Chaos.MaxNumWorkers 100,','vam.Contact.NativeGSBatch 1,vam.Contact.ExperimentalPolar 1,') for a in args]
 if name=='combined':args=[a.replace('p.Chaos.MaxNumWorkers 100,','vam.Contact.NativeGSBatch 1,vam.Contact.RestMetrics 1,') for a in args]
 if name=='metrics':args=[a.replace('p.Chaos.MaxNumWorkers 100,','vam.Contact.RestMetrics 1,') for a in args]
 if name=='quiet':args.append('-LogCmds=LogFleshDeformerBuffers Warning')
 if name=='serial':
  args=[a.replace('p.Chaos.MaxNumWorkers 100,','p.Chaos.DisablePhysicsParallelFor 1,') for a in args]
 with (d/'console.log').open('w') as f:code=subprocess.call(args,stdout=f,stderr=subprocess.STDOUT)
 s=(d/'runtime.log').read_text(errors='replace');result={'exit':code,'tests':re.findall(r'Test Completed.*',s),'errors':re.findall(r'LogAutomationController: Error: .*',s)}
 (d/'summary.json').write_text(json.dumps(result,indent=2));print(name,result,flush=True)
 commands=[]
 for phase in ['press_left','world_moving','world_held','disabled_after']:
  for thread in ['GameThread','all']:
   cmd='TimingInsights.ExportTimerStatistics '+str(d/(phase+'-'+thread+'.csv')).replace('\\','/')+' -region=Contact_'+phase+' -sortBy=TotalInclusiveTime'
   if thread!='all':cmd+=' -threads=GameThread'
   commands.append(cmd)
 (d/'export.txt').write_text('\n'.join(commands))
 args=['I:/Program/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealInsights.exe','-OpenTraceFile='+str(d/'capture.utrace'),'-ABSLOG='+str(d/'analysis.log'),'-AutoQuit','-NoUI','-ExecOnAnalysisCompleteCmd=@='+str(d/'export.txt')]
 with (d/'analysis-console.log').open('w') as f:print(name,'Insights',subprocess.call(args,stdout=f,stderr=subprocess.STDOUT,creationflags=subprocess.CREATE_NO_WINDOW),flush=True)
