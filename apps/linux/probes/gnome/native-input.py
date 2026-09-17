#!/usr/bin/python3
# //! 只给独立 headless GNOME 的 GTK 输入框发送固定测试文本。
import os, json, time, subprocess as sp, re
from pathlib import Path
from gi.repository import Gio, GLib
from fixtures.settings import configure_display, start_settings
from fixtures.fault import inject_fault
base=Path(os.environ["XDG_RUNTIME_DIR"]).parent
assert os.environ.get("QINGJIAN_FIXTURE_ID") == str(base)
assert os.environ.get("DBUS_SESSION_BUS_ADDRESS") == "unix:path=" + str(base / "bus")
assert os.environ.get("DBUS_SYSTEM_BUS_ADDRESS") == os.environ["DBUS_SESSION_BUS_ADDRESS"]
assert os.environ.get("DISPLAY") is None
os.environ["QJ_TEST_QT"] = "1" if os.environ.get("QINGJIAN_TEST_NATIVE") == "qt" else ""
os.environ["QJ_TEST_BROWSER"] = "1" if os.environ.get("QINGJIAN_TEST_NATIVE") == "firefox" else ""
root=Path(__file__).resolve().parents[4]
env=os.environ.copy()
env.update(WAYLAND_DISPLAY="qingjian-test", GDK_BACKEND="wayland", GTK_IM_MODULE="fcitx", GTK_A11Y="none", GTK_USE_PORTAL="0", QINGJIAN_SOCKET=str(base/"qj.sock"), QINGJIAN_DICT=str(root/"assets/sample/dict.tsv"),QINGJIAN_RESOURCES=str(root),QINGJIAN_UI_DIAGNOSTICS="1",QINGJIAN_GNOME_PROBE="1")
for path in ("config/qingjian","config/fcitx5","data/fcitx5/addon","data/fcitx5/inputmethod"):(base/path).mkdir(parents=True,exist_ok=True)
(base/"config/qingjian/config.toml").write_text('[general]\nlearning_language="en"\n[linux_ui]\nrenderer="qingjian"\n' +
 'ui_scale_percent='+env.get('QINGJIAN_TEST_UI_SCALE','100')+'\nfollow_system_text_scale='+env.get('QINGJIAN_TEST_FOLLOW_TEXT','true')+'\n')
(base/"config/fcitx5/config").write_text("[Behavior]\nActiveByDefault=True\nPreloadInputMethod=True\nShowInputMethodInformation=False\n")
(base/"config/fcitx5/profile").write_text("[Groups/0]\nName=Test\nDefault Layout=us\nDefaultIM=qingjian\n[Groups/0/Items/0]\nName=keyboard-us\n[Groups/0/Items/1]\nName=qingjian\n[GroupOrder]\n0=Test\n")
addon=Path(os.environ["QINGJIAN_TEST_ADDON"])
metadata=(root/"apps/linux/fcitx5/data/addon/qingjian.conf").read_text()
(base/"data/fcitx5/addon/qingjian.conf").write_text(metadata.replace("Library=qingjian","Library="+str(addon.with_suffix(""))))
(base/"data/fcitx5/inputmethod/qingjian.conf").write_text((root/"apps/linux/fcitx5/data/inputmethod/qingjian.conf").read_text())
if os.environ.get("QJ_TEST_QT"):
 env.update(QT_QPA_PLATFORM="wayland",QT_IM_MODULE="fcitx",PYTHONPATH=os.environ.get("QINGJIAN_TEST_QT_PYTHONPATH", ""))
processes=[]
def start(args,name):
 with (base/(name+".log")).open("w") as log:
  p=sp.Popen(args,env=env,stdout=log,stderr=log)
 processes.append(p)
 return p
def until(pred):
 for _ in range(100):
  if pred():return
  time.sleep(.05)
 raise RuntimeError("就绪超时")
bus=Gio.bus_get_sync(Gio.BusType.SESSION,None)
def call(path,interface,method,params=None):
 return bus.call_sync("org.gnome.Mutter.RemoteDesktop",path,interface,method,params,None,Gio.DBusCallFlags.NONE,3000,None).unpack()
session=None
try:
 scale=int(env.get('QINGJIAN_TEST_SCALE','100'))
 if scale != 100 or env.get('QINGJIAN_TEST_MOVE_OUTPUT'):
  configure_display(bus,base,1.5 if scale==150 else 5/3,dual=bool(env.get('QINGJIAN_TEST_MOVE_OUTPUT')))
 if env.get('QINGJIAN_TEST_TEXT_SCALE'):start_settings(bus,base,env,start,float(env['QINGJIAN_TEST_TEXT_SCALE']))
 start([os.environ["QINGJIAN_TEST_SERVER"]],"server")
 fallback=bool(env.get('QINGJIAN_TEST_FALLBACK'))
 if env.get('QINGJIAN_TEST_KIMPANEL'):
  metadata=Path('/usr/share/fcitx5/addon/kimpanel.conf').read_text()
  (base/'data/fcitx5/addon/kimpanel.conf').write_text(re.sub(r'^Library=.*$',
   'Library='+str(Path(env['QINGJIAN_TEST_KIMPANEL']).with_suffix('')),metadata,flags=re.M))
 fcitx_process=start(["fcitx5","--disable=all","--enable=keyboard,dbus,dbusfrontend,wayland,qingjian"+(',kimpanel' if fallback else ''),"-u","kimpanel" if fallback else "none"],"fcitx")
 until(lambda:(base/"qj.sock").exists() and "Loaded addon qingjian" in (base/"fcitx.log").read_text())
 if os.environ.get("QJ_TEST_BROWSER"):
  env.update(MOZ_ENABLE_WAYLAND="1")
  page=base/"input.html"
  page.write_text("<!doctype html><meta charset=utf-8><title>QJ:</title><input autofocus style='font-size:24px;margin:100px' oninput=\"document.title='QJ:'+this.value+':QJ-END'\">")
  profile=base/"firefox-profile"
  profile.mkdir()
  (profile/"user.js").write_text('user_pref("browser.shell.checkDefaultBrowser",false);\nuser_pref("browser.startup.firstrunSkipsHomepage",true);\nuser_pref("browser.startup.homepage_override.mstone","ignore");\nuser_pref("datareporting.policy.dataSubmissionPolicyBypassNotification",true);\n')
  start([os.environ["QINGJIAN_TEST_FIREFOX"],"--no-remote","--new-instance","--profile",str(profile),"--kiosk",page.as_uri()],"firefox")
  time.sleep(5)
 else:
  start(["/usr/bin/python3",str(root/("apps/linux/probes/gnome/fixtures/qt.py" if os.environ.get("QJ_TEST_QT") else "apps/linux/probes/gnome/fixtures/gtk.py")),str(base/"entry.json")],"gtk")
  until(lambda:(base/"entry.json").exists())
 time.sleep(2)
 sp.run(["fcitx5-remote","-s","qingjian"],env=env,check=True,timeout=3)
 sp.run(["fcitx5-remote","-o"],env=env,check=True,timeout=3)
 session=call("/org/gnome/Mutter/RemoteDesktop","org.gnome.Mutter.RemoteDesktop","CreateSession")[0]
 call(session,"org.gnome.Mutter.RemoteDesktop.Session","Start")
 if os.environ.get("QJ_TEST_MOUSE"):
  for dx,dy in [(400.,300.),(1.,1.)]:
   call(session,"org.gnome.Mutter.RemoteDesktop.Session","NotifyPointerMotionRelative",GLib.Variant("(dd)",(dx,dy)))
   time.sleep(.2)
  sp.run(["gdbus","call","--session","--dest","org.gnome.Shell","--object-path","/org/gnome/Shell","--method","org.freedesktop.DBus.Properties.Set","org.gnome.Shell","OverviewActive","<false>"],env=env,check=True,capture_output=True,timeout=3)
  time.sleep(.3)
 for char in "x\x1b\b":
  key = {"\x1b":65307,"\b":65288}.get(char,ord(char))
  for pressed in (True,False):
   call(session,"org.gnome.Mutter.RemoteDesktop.Session","NotifyKeyboardKeysym",GLib.Variant("(ub)",(key,pressed)))
  time.sleep(.5)
 time.sleep(.5)
 def state():
  return json.loads(bus.call_sync("org.qingjian.PanelProbe1","/org/qingjian/PanelProbe1","org.qingjian.PanelProbe1","TestState",None,None,Gio.DBusCallFlags.NONE,3000,None).unpack()[0])
 def key(sym):
  for pressed in (True,False):call(session,"org.gnome.Mutter.RemoteDesktop.Session","NotifyKeyboardKeysym",GLib.Variant("(ub)",(sym,pressed)))
 def click(x,y,pointer):
  call(session,"org.gnome.Mutter.RemoteDesktop.Session","NotifyPointerMotionRelative",GLib.Variant("(dd)",(x-pointer[0],y-pointer[1])))
  time.sleep(.2)
  for pressed in (True,False):call(session,"org.gnome.Mutter.RemoteDesktop.Session","NotifyPointerButton",GLib.Variant("(ib)",(272,pressed)))
  time.sleep(.5)
 def input_cycle():
  for char in ("nihao" if (os.environ.get("QJ_TEST_MOUSE") or os.environ.get("QJ_TEST_FAULT")) else "nihao "):
   key(ord(char));time.sleep(.15)
  time.sleep(.5)
  if not os.environ.get("QJ_TEST_MOUSE"):return None
  before=state();print('before',before)
  assert before['visible'] and before['mapped'],before
  if env.get('QINGJIAN_TEST_MOVE_OUTPUT'):
   original=before
   for output in (1,0):
    began=time.monotonic()
    bus.call_sync('org.qingjian.PanelProbe1','/org/qingjian/PanelProbe1','org.qingjian.PanelProbe1','TestMove',
     GLib.Variant('(u)',(output,)),None,Gio.DBusCallFlags.NONE,3000,None)
    time.sleep(.4)
    moved=state();print('OUTPUT_MOVED',json.dumps({'monitor':output,'elapsed_ms':(time.monotonic()-began)*1000,'state':moved}))
    assert moved['visible'] and moved['mapped'] and moved['window']==original['window'],moved
    assert abs(moved['actor'][2]-original['actor'][2])<1 and abs(moved['actor'][3]-original['actor'][3])<1,moved
    before=moved
  if env.get('QINGJIAN_TEST_SCREENSHOT'):
   path=bus.call_sync('org.qingjian.PanelProbe1','/org/qingjian/PanelProbe1','org.qingjian.PanelProbe1','TestCapture',
    None,None,Gio.DBusCallFlags.NONE,3000,None).unpack()[0]
   print('SCREENSHOT',path)
  if fallback:
   def default_state():
    return json.loads(bus.call_sync('org.kde.impanel','/org/kde/impanel','org.kde.impanel','TestState',None,None,Gio.DBusCallFlags.NONE,3000,None).unpack()[0])
   assert not default_state()['visible'],default_state()
   began=time.monotonic()
   bus.call_sync('org.gnome.Shell','/org/gnome/Shell','org.gnome.Shell.Extensions','DisableExtension',GLib.Variant('(s)',('qingjian-probe@qingjian.local',)),None,Gio.DBusCallFlags.NONE,3000,None)
   def recovered():
    try:
     value=default_state();return value['visible'] and value['mapped']
    except GLib.Error:return False
   until(recovered)
   current=default_state();elapsed=(time.monotonic()-began)*1000
   print('DEFAULT_RECOVERED',json.dumps({'elapsed_ms':elapsed,'state':current}))
   assert current['window']==before['window'],current
   if env.get('QINGJIAN_TEST_KIMPANEL'):
    assert current['spot']['relative'] and current['spot']['height']>0,current
    assert abs(current['items'][0]['rect'][0]-before['actor'][0])<100,current
   rect=current['items'][0]['rect'];click(rect[0]+rect[2]/2,rect[1]+rect[3]/2,current['pointer'])
   after=default_state();assert not after['visible'] and after['window']==before['window'],after
   return before
  # 选框中心按固定第一候选的已知测试位置；UI/文字倍率同时改变该偏移。
  ui=float(env.get('QINGJIAN_TEST_UI_SCALE','100'))/100
  text=float(env.get('QINGJIAN_TEST_TEXT_SCALE','1')) if env.get('QINGJIAN_TEST_FOLLOW_TEXT','true')=='true' else 1
  x,y=before['actor'][0]+70*ui,before['actor'][1]+74*ui*text
  call(session,"org.gnome.Mutter.RemoteDesktop.Session","NotifyPointerMotionRelative",GLib.Variant("(dd)",(x-before['pointer'][0],y-before['pointer'][1])))
  time.sleep(.2)
  moved=state();print('moved',moved)
  assert moved['visible'] and moved['mapped'] and moved['window']==before['window'],moved
  for pressed in (True,False):call(session,"org.gnome.Mutter.RemoteDesktop.Session","NotifyPointerButton",GLib.Variant("(ib)",(272,pressed)))
  time.sleep(.5)
  after=state();print('clicked',[x,y],'after',after)
  assert not after['visible'] and not after['mapped'] and after['window']==before['window'],after
  return before
 before=input_cycle()
 if fallback:
  bus.call_sync('org.gnome.Shell','/org/gnome/Shell','org.gnome.Shell.Extensions','EnableExtension',
   GLib.Variant('(s)',('qingjian-probe@qingjian.local',)),None,Gio.DBusCallFlags.NONE,3000,None)
  def restored():
   try:state();return True
   except GLib.Error:return False
  until(restored);time.sleep(.5)
  fallback=False
  input_cycle()
  print('SELF_DRAW_RESTORED',json.loads((base/'entry.json').read_text()))
 scenario=env.get('QINGJIAN_TEST_SCENARIO','single')
 if scenario!='single':
  windows=[before['window']]
  for turn in (1,2):
   if scenario=='fields':key(65289);key(65363)
   else:
    for sym,pressed in [(65513,True),(65307,True),(65307,False),(65513,False)]:
     call(session,"org.gnome.Mutter.RemoteDesktop.Session","NotifyKeyboardKeysym",GLib.Variant("(ub)",(sym,pressed)))
   time.sleep(.5)
   switched=state();assert not switched['visible'],switched
   assert (switched['window']==windows[-1]) == (scenario=='fields'),(windows,switched)
   windows.append(switched['window'])
   input_cycle()
   current=json.loads((base/'entry.json').read_text())
   assert sorted(current['texts'])==sorted(['你好','你好'] if turn==1 else ['你好你好','你好']),current
  assert windows[0]==windows[2],windows
  print('WINDOW_CYCLE',windows)
 if env.get('QJ_TEST_FAULT'):
  inject_fault(bus,fcitx_process,state,until,env['QJ_TEST_FAULT'])
  raise SystemExit(0)
 if os.environ.get("QJ_TEST_BROWSER"):
  state=json.loads(bus.call_sync("org.qingjian.PanelProbe1","/org/qingjian/PanelProbe1","org.qingjian.PanelProbe1","TestState",None,None,Gio.DBusCallFlags.NONE,3000,None).unpack()[0])
  print(json.dumps(state,ensure_ascii=False))
  assert state["title"].startswith("QJ:你好:QJ-END"),state
  raise SystemExit(0)
 state=json.loads((base/"entry.json").read_text())
 print(json.dumps(state,ensure_ascii=False))
 assert (state["text"]==("你好你好" if env.get("QINGJIAN_TEST_FALLBACK") else "你好") if scenario=="single" else sorted(state["texts"])==["你好","你好你好"]),state
finally:
 try:
  if session:call(session,"org.gnome.Mutter.RemoteDesktop.Session","Stop")
 except GLib.Error:
  pass  # 桌面服务断开不能跳过子进程清理。
 for p in reversed(processes):
  if p.poll() is None:
   p.send_signal(18)
   p.terminate()
 for p in processes:
  try:p.wait(timeout=3)
  except sp.TimeoutExpired:p.kill();p.wait()
