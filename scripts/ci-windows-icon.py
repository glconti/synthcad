"""Inspect embedded Windows icon resources without requiring an OpenGL desktop."""
import ctypes as c
from ctypes import wintypes as w
from pathlib import Path
import sys

kernel = c.WinDLL("kernel32", use_last_error=True)
kernel.LoadLibraryExW.argtypes = [w.LPCWSTR, w.HANDLE, w.DWORD]
kernel.LoadLibraryExW.restype = w.HMODULE
kernel.FindResourceW.argtypes = [w.HMODULE, c.c_void_p, c.c_void_p]
kernel.FindResourceW.restype = w.HANDLE
kernel.LoadResource.argtypes = [w.HMODULE, w.HANDLE]
kernel.LoadResource.restype = w.HANDLE
kernel.LockResource.argtypes = [w.HANDLE]
kernel.LockResource.restype = c.c_void_p
kernel.FreeLibrary.argtypes = [w.HMODULE]
module = kernel.LoadLibraryExW(str(Path(sys.argv[1]).resolve(strict=True)), None, 2)
assert module, "Cannot inspect executable resources"
try:
    resource = kernel.FindResourceW(module, 1, 14)
    assert resource, "Embedded RT_GROUP_ICON missing"
    pointer = kernel.LockResource(kernel.LoadResource(module, resource))
    assert pointer and c.c_ushort.from_address(pointer + 4).value == 7, "Expected seven icon sizes"
finally:
    kernel.FreeLibrary(module)
print("PASS embedded Windows seven-size icon resource")
