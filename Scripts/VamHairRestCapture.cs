using System;
using System.IO;
using System.Linq;
using System.Collections;
using System.Reflection;
using UnityEngine;
using SimpleJSON;
using GPUTools.Hair.Scripts;

public partial class C1Capture {
 JSONNode HairValue(object value,int depth){
  if(value==null)return J("null");
  if(value is Color){var c=(Color)value;var a=new JSONArray();a.Add(J(c.r));a.Add(J(c.g));a.Add(J(c.b));a.Add(J(c.a));return a;}
  var curve=value as AnimationCurve;if(curve!=null){var a=new JSONArray();for(int i=0;i<=64;i++)a.Add(J(curve.Evaluate(i/64f)));return a;}
  if(value is bool||value is float||value is int||value is string||value is Vector3||value.GetType().IsEnum)return J(value);
  if(depth<=0||value is UnityEngine.Object)return J(value.ToString());
  var result=new JSONClass();foreach(var f in value.GetType().GetFields(BindingFlags.Instance|BindingFlags.Public))result[f.Name]=HairValue(f.GetValue(value),depth-1);return result;
 }
 IEnumerator CaptureHairRest(){
  Log("hair restoration baseline; physics disabled before capture");
  foreach(var ctrl in person.GetComponentsInChildren<FreeControllerV3>(true))ctrl.enabled=false;
  SuperController.singleton.SetFreezeAnimation(true);person.SetFreezePhysics(true);
  foreach(var rb in person.GetComponentsInChildren<Rigidbody>(true)){rb.isKinematic=true;rb.useGravity=false;}
  foreach(var b in bones){pos[b]=b.startingLocalPosition;rot[b]=b.startingLocalRotation;scale[b]=b.transform.localScale;}holding=true;
  var hairs=person.GetComponentsInChildren<HairSettings>(true).Where(h=>h.isActiveAndEnabled&&h.RuntimeData!=null).ToArray();
  if(hairs.Length==0)throw new Exception("No active source hair");
  foreach(var h in hairs){h.PhysicsSettings.IsEnabled=false;h.UpdateSettings();}
  for(int i=0;i<120;i++)yield return null;
  yield return new WaitForEndOfFrame();Capture("hair-rest-body");
  var manifest=new JSONClass();manifest["preset"]=J(Preset);manifest["physics"]=J(false);manifest["scope"]=J("Actual source renderer; appearance untouched; pose frozen; original render settings retained");var entries=new JSONArray();
  for(int i=0;i<hairs.Length;i++){
   var h=hairs[i];var row=new JSONClass();string key="hair-"+i;row["path"]=J(PathOf(h.transform));row["local_to_world"]=J(h.transform.localToWorldMatrix);row["render_settings"]=HairValue(h.RenderSettings,2);row["lod_settings"]=HairValue(h.LODSettings,2);row["strand_settings"]=HairValue(h.StandsSettings,1);row["physics_settings"]=HairValue(h.PhysicsSettings,1);
   var guides=h.StandsSettings.Provider.GetVertices().ToArray();VecFile(key+"-provider.f32",guides,guides.Length);row["provider_count"]=J(guides.Length);
   row["provider_type"]=J(h.StandsSettings.Provider.GetType().FullName);
   row["source_segments"]=J(h.StandsSettings.Provider.GetSegmentsNum());
   var transforms=h.StandsSettings.Provider.GetTransformsBuffer();if(transforms!=null){transforms.Data=new Matrix4x4[transforms.ComputeBuffer.count];transforms.PullData();var a=new JSONArray();foreach(var m in transforms.Data)a.Add(J(m));row["root_transforms"]=a;}
   var roots=new JSONArray();foreach(var v in h.StandsSettings.Provider.GetHairRootToScalpMap())roots.Add(J(v));row["root_to_scalp"]=roots;
   var normalBuffer=h.StandsSettings.Provider.GetNormalsBuffer();if(normalBuffer!=null){normalBuffer.Data=new Vector3[normalBuffer.ComputeBuffer.count];normalBuffer.PullData();VecFile(key+"-normals.f32",normalBuffer.Data,normalBuffer.Data.Length);}
   var bary=h.RuntimeData.Barycentrics;if(bary!=null){bary.PullData();VecFile(key+"-barycentrics.f32",bary.Data,bary.Data.Length);}
   var renderer=h.GetComponentsInChildren<MeshRenderer>(true).FirstOrDefault(r=>r.sharedMaterial!=null&&r.sharedMaterial.shader.name.Contains("Hair"));
   if(renderer!=null){row["shader"]=J(renderer.sharedMaterial.shader.name);row["tess_factor"]=J((Vector3)renderer.sharedMaterial.GetVector("_TessFactor"));}

   if(h.RuntimeData.Particles!=null){h.RuntimeData.Particles.PullData();var points=h.RuntimeData.Particles.Data.Select(p=>p.Position).ToArray();VecFile(key+"-particles.f32",points,points.Length);row["particle_count"]=J(points.Length);}
   if(h.RuntimeData.TessRenderParticles!=null){h.RuntimeData.TessRenderParticles.PullData();var points=h.RuntimeData.TessRenderParticles.Data.Select(p=>p.Position).ToArray();VecFile(key+"-tess.f32",points,points.Length);row["tess_count"]=J(points.Length);}
   row["prefix"]=J(key);entries.Add(row);
  }
  manifest["hairs"]=entries;
  SuperController.singleton.HideMainHUD();SuperController.singleton.HideMonitorUI();foreach(var canvas in UnityEngine.Object.FindObjectsOfType<Canvas>())canvas.enabled=false;
  foreach(var light in UnityEngine.Object.FindObjectsOfType<Light>())light.enabled=false;RenderSettings.ambientLight=Color.black;RenderSettings.ambientIntensity=0;RenderSettings.skybox=null;
  var cam=new GameObject("HairRestEvidenceCamera").AddComponent<Camera>();cam.clearFlags=CameraClearFlags.SolidColor;cam.backgroundColor=new Color(.12f,.14f,.18f);cam.nearClipPlane=.01f;cam.farClipPlane=100;cam.fieldOfView=40;cam.allowHDR=false;cam.allowMSAA=false;
  var lamp=new GameObject("HairRestEvidenceKey").AddComponent<Light>();lamp.type=LightType.Directional;lamp.intensity=.8f;lamp.shadows=LightShadows.Soft;
  var fill=new GameObject("HairRestEvidenceFill").AddComponent<Light>();fill.type=LightType.Directional;fill.intensity=.25f;fill.shadows=LightShadows.None;
  var rt=new RenderTexture(960,960,24,RenderTextureFormat.ARGB32);cam.targetTexture=rt;var center=bones.First(b=>b.id=="head").transform.position+Vector3.up*.08f;
  string[] names={"front","right","rear","left","oblique"};Vector3[] offsets={new Vector3(0,.04f,.7f),new Vector3(.7f,.04f,0),new Vector3(0,.04f,-.7f),new Vector3(-.7f,.04f,0),new Vector3(.5f,.15f,-.5f)};
  var views=new JSONArray();
  for(int i=0;i<names.Length;i++){
   cam.transform.position=center+offsets[i];cam.transform.LookAt(center);lamp.transform.rotation=Quaternion.LookRotation(-offsets[i]+Vector3.down*.5f+Vector3.right*.2f);fill.transform.rotation=Quaternion.LookRotation(offsets[i]+Vector3.down*.2f);
   for(int f=0;f<5;f++)yield return null;yield return new WaitForEndOfFrame();cam.Render();var previous=RenderTexture.active;RenderTexture.active=rt;var texture=new Texture2D(960,960,TextureFormat.RGB24,false);texture.ReadPixels(new Rect(0,0,960,960),0,0);texture.Apply();File.WriteAllBytes(Path.Combine(Out,names[i]+".png"),texture.EncodeToPNG());RenderTexture.active=previous;UnityEngine.Object.Destroy(texture);
   var v=new JSONClass();v["name"]=J(names[i]);v["position"]=J(cam.transform.position);v["forward"]=J(cam.transform.forward);v["up"]=J(cam.transform.up);v["vertical_fov"]=J(cam.fieldOfView);v["key_forward"]=J(lamp.transform.forward);v["fill_forward"]=J(fill.transform.forward);views.Add(v);
  }
  manifest["views"]=views;Save("hair-rest.json",manifest);File.WriteAllText(Path.Combine(Out,"complete.txt"),"Actual VaM hair renderer captured with physics off; UE restoration comparison pending");Log("hair baseline complete");Application.Quit();
 }
}
