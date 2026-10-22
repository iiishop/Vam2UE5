using System;
using System.IO;
using System.Linq;
using System.Collections;
using UnityEngine;
using SimpleJSON;
using GPUTools.Hair.Scripts;

public partial class C1Capture {
 IEnumerator CaptureHairDynamics(){
  Log("Dynamic hair audit: original hair parameters, kinematic body driver, live hair physics");
  foreach(var ctrl in person.GetComponentsInChildren<FreeControllerV3>(true))ctrl.enabled=false;
  SuperController.singleton.SetFreezeAnimation(false);person.SetFreezePhysics(false);
  foreach(var rb in person.GetComponentsInChildren<Rigidbody>(true)){rb.isKinematic=true;rb.useGravity=false;}
  foreach(var b in bones){pos[b]=b.startingLocalPosition;rot[b]=b.startingLocalRotation;scale[b]=b.transform.localScale;}
  holding=true;Hold();
  var hairs=person.GetComponentsInChildren<HairSettings>(true).Where(h=>h.isActiveAndEnabled&&h.RuntimeData!=null).ToArray();
  if(hairs.Length==0)throw new Exception("No active source hair");
  var manifest=new JSONClass();manifest["preset"]=J(Preset);manifest["hair_preset"]=J(Environment.GetEnvironmentVariable("VAM_HAIR_PRESET"));
  manifest["scope"]=J("Native VaM hair simulation; kinematic head driver; body dynamics disabled; original hair material/constraints preserved; timestamps measured, not assumed 60fps");
  var entries=new JSONArray();
  for(int i=0;i<hairs.Length;i++){
   var h=hairs[i];var row=new JSONClass();row["path"]=J(PathOf(h.transform));row["physics_before"]=HairValue(h.PhysicsSettings,3);
   h.PhysicsSettings.IsEnabled=true;h.UpdateSettings();row["physics_enabled"]=HairValue(h.PhysicsSettings,3);row["render"]=HairValue(h.RenderSettings,2);
   row["segments"]=J(h.StandsSettings.Provider.GetSegmentsNum());row["provider_type"]=J(h.StandsSettings.Provider.GetType().FullName);
   entries.Add(row);
  }
  manifest["hairs"]=entries;Save("dynamic-manifest.json",manifest);
  SuperController.singleton.HideMainHUD();SuperController.singleton.HideMonitorUI();foreach(var canvas in UnityEngine.Object.FindObjectsOfType<Canvas>())canvas.enabled=false;
  foreach(var light in UnityEngine.Object.FindObjectsOfType<Light>())light.enabled=false;
  RenderSettings.ambientLight=Color.gray;RenderSettings.ambientIntensity=.35f;RenderSettings.skybox=null;
  var cam=new GameObject("HairDynamicEvidenceCamera").AddComponent<Camera>();cam.clearFlags=CameraClearFlags.SolidColor;cam.backgroundColor=new Color(.12f,.14f,.18f);cam.nearClipPlane=.01f;cam.farClipPlane=100;cam.fieldOfView=40;cam.allowHDR=false;
  var lamp=new GameObject("HairDynamicKey").AddComponent<Light>();lamp.type=LightType.Directional;lamp.intensity=.8f;lamp.transform.rotation=Quaternion.Euler(35,130,0);
  var fill=new GameObject("HairDynamicFill").AddComponent<Light>();fill.type=LightType.Directional;fill.intensity=.35f;fill.transform.rotation=Quaternion.Euler(20,-40,0);
  var rt=new RenderTexture(640,640,24,RenderTextureFormat.ARGB32);cam.targetTexture=rt;
  var head=bones.First(b=>b.id=="head");var center=head.transform.position-Vector3.up*.12f;
  var samples=new JSONArray();float start=Time.time;float next=0;int frame=0;
  while(Time.time-start<24){
   float t=Time.time-start;
   // 0..8 settle, 8..16 oscillatory head motion, 16..24 sudden stop/recovery.
   float yaw=t>=8&&t<16?25*Mathf.Sin((t-8)*Mathf.PI):0;
   float pitch=t>=8&&t<16?12*Mathf.Sin((t-8)*Mathf.PI*2):0;
   head.transform.localRotation=rot[head]*Quaternion.Euler(pitch,yaw,0);
   // Hold() uses explicit local reference matrices; preserve exactly this driver.
   referencePose=new System.Collections.Generic.Dictionary<string,Matrix4x4>();
   referencePose[head.id]=Matrix4x4.TRS(pos[head],head.transform.localRotation,scale[head]);Hold();
   yield return new WaitForEndOfFrame();
   if(t<next)continue;next=t+1f/15;
   var row=new JSONClass();row["time"]=J(Time.time-start);row["delta_time"]=J(Time.deltaTime);row["frame"]=J(Time.frameCount);row["head_world"]=J(head.transform.localToWorldMatrix);row["head_local"]=J(Matrix4x4.TRS(head.transform.localPosition,head.transform.localRotation,head.transform.localScale));row["phase"]=J(t<8?"settle":t<16?"motion":"recovery");
   var groups=new JSONArray();
   for(int i=0;i<hairs.Length;i++){
    var h=hairs[i];var g=new JSONClass();g["enabled"]=J(h.PhysicsSettings.IsEnabled);g["local_to_world"]=J(h.transform.localToWorldMatrix);
    if(h.RuntimeData.Particles==null)throw new Exception("Missing live particle buffer");
    h.RuntimeData.Particles.PullData();var pts=h.RuntimeData.Particles.Data.Select(p=>p.Position).ToArray();
    string file=String.Format("f{0:D4}-h{1}-particles.f32",frame,i);VecFile(file,pts,pts.Length);g["particles"]=J(file);g["count"]=J(pts.Length);groups.Add(g);
   }
   row["groups"]=groups;
   float orbit=frame%3*120*Mathf.Deg2Rad;cam.transform.position=center+new Vector3(Mathf.Sin(orbit)*1.25f,.08f,Mathf.Cos(orbit)*1.25f);cam.transform.LookAt(center);row["camera_world"]=J(cam.transform.localToWorldMatrix);
   cam.Render();var prev=RenderTexture.active;RenderTexture.active=rt;var tex=new Texture2D(640,640,TextureFormat.RGB24,false);tex.ReadPixels(new Rect(0,0,640,640),0,0);tex.Apply();File.WriteAllBytes(Path.Combine(Out,String.Format("frame-{0:D4}.jpg",frame)),tex.EncodeToJPG(90));RenderTexture.active=prev;UnityEngine.Object.Destroy(tex);
   samples.Add(row);frame++;if(frame%30==0){Save("dynamic-samples.json",samples);Log("dynamic samples "+frame+" t="+t);}
  }
  Save("dynamic-samples.json",samples);File.WriteAllText(Path.Combine(Out,"complete.txt"),"Native hair enabled: settle, head motion, recovery; quantitative audit pending");Application.Quit();
 }
}

