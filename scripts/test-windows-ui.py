"""Windows smoke test: embedded/runtime identity and watched-scene recovery.
Run with the Release dingcad_viewer.exe path. Uses only Python's standard library.
All scenes, logs and screenshots are temporary; no personal model is modified.
"""
import ctypes as c
from ctypes import wintypes as w
from pathlib import Path
import subprocess
import sys
import tempfile
import time

exe=Path(sys.argv[1]).resolve()
kernel=c.WinDLL('kernel32',use_last_error=True)
user=c.WinDLL('user32',use_last_error=True)
kernel.LoadLibraryExW.argtypes=[w.LPCWSTR,w.HANDLE,w.DWORD];kernel.LoadLibraryExW.restype=w.HMODULE
kernel.FindResourceW.argtypes=[w.HMODULE,c.c_void_p,c.c_void_p];kernel.FindResourceW.restype=w.HANDLE
kernel.LoadResource.argtypes=[w.HMODULE,w.HANDLE];kernel.LoadResource.restype=w.HANDLE
kernel.LockResource.argtypes=[w.HANDLE];kernel.LockResource.restype=c.c_void_p
kernel.FreeLibrary.argtypes=[w.HMODULE]
module=kernel.LoadLibraryExW(str(exe),None,2)
assert module, 'Cannot inspect executable resources'
resource=kernel.FindResourceW(module,1,14)
assert resource, 'Embedded RT_GROUP_ICON missing'
pointer=kernel.LockResource(kernel.LoadResource(module,resource))
assert c.c_ushort.from_address(pointer+4).value==7, 'Expected seven embedded icon sizes'
kernel.FreeLibrary(module)
user.SendMessageW.argtypes=[w.HWND,w.UINT,w.WPARAM,w.LPARAM];user.SendMessageW.restype=c.c_ssize_t
user.GetWindowTextW.argtypes=[w.HWND,w.LPWSTR,c.c_int]
callback=c.WINFUNCTYPE(w.BOOL,w.HWND,w.LPARAM)
user.EnumWindows.argtypes=[callback,w.LPARAM]
user.GetWindowThreadProcessId.argtypes=[w.HWND,c.POINTER(w.DWORD)]
def own_window(pid):
    found=[]
    @callback
    def visit(hwnd,_):
        owner=w.DWORD();user.GetWindowThreadProcessId(hwnd,c.byref(owner))
        if owner.value==pid:found.append(hwnd)
        return True
    user.EnumWindows(visit,0)
    for hwnd in found:
        title=c.create_unicode_buffer(1024);user.GetWindowTextW(hwnd,title,1024)
        if title.value.startswith('SynthCAD'):return hwnd,title.value
    return None,None

def wait_log(log,needle,proc):
    deadline=time.monotonic()+15
    while time.monotonic()<deadline:
        text=log.read_text(encoding='utf-8',errors='replace')
        if needle in text:return text
        assert proc.poll() is None, f'Viewer exited before {needle}: {text}'
        time.sleep(.05)
    raise AssertionError(f'Timed out waiting for {needle}')

temp_root=Path(sys.argv[2] if len(sys.argv)>2 else tempfile.gettempdir()).resolve()
with tempfile.TemporaryDirectory(prefix='synthcad-ui-',dir=temp_root) as directory:
    root=Path(directory).resolve();assert root.parent==temp_root
    scene=root/'assembly.js';log=root/'run.log'
    good="const part=cube({size:[20,30,4]}); export const scene=part; export const displayParts=[{id:'deck',name:'Piastra più stretta',group:['Oggetto progettato','Unità'],solid:part,color:'#628b65'}];"
    scene.write_text(good,encoding='utf-8')
    with log.open('wb') as stream:
        proc=subprocess.Popen([str(exe),'--ui-preview',str(scene),str(root/'recovered.png'),'watch'],cwd=root,stdout=stream,stderr=subprocess.STDOUT,creationflags=subprocess.CREATE_NO_WINDOW)
        try:
            wait_log(log,'Loaded ',proc)
            deadline=time.monotonic()+5
            while time.monotonic()<deadline:
                hwnd,title=own_window(proc.pid)
                if title=='SynthCAD \u2014 assembly.js':break
                time.sleep(.05)
            assert title=='SynthCAD \u2014 assembly.js',title
            assert user.SendMessageW(hwnd,0x7f,0,0), 'Small/title-bar runtime icon missing'
            assert user.SendMessageW(hwnd,0x7f,1,0), 'Large/Alt-Tab runtime icon missing'
            scene.write_text("throw Error('Misura più grande: controllare la flangia'); export const scene=cube({size:[1,1,1]});",encoding='utf-8')
            text=wait_log(log,'export disabled until corrected',proc)
            assert 'Misura più grande: controllare la flangia' in text, 'Authored diagnostic lost: '+repr(text)
            assert 'assembly.js:' in text, 'Full stack trace lost'
            scene.write_text(good,encoding='utf-8')
            proc.wait(timeout=20)
            assert proc.returncode==0
        finally:
            if proc.poll() is None:proc.terminate();proc.wait()
    text=log.read_text(encoding='utf-8',errors='replace')
    assert 'UI QA: 1 parts, export enabled, error clear' in text,text
    assert text.count('Loaded ')>=4,'Expected initial load plus recovered reload (console and trace log)'
    assert (root/'recovered.png').is_file()
print('PASS embedded 7-size ICO, runtime small/large icons, title, outside-repo launch, verbatim diagnostics, watched error and recovery')
