"""Launch only the development executable and capture its real Windows desktop.

No Qt offscreen/rendering path: GDI copies pixels from the visible desktop after
the native application's window is exposed and stable. Close only this child.
"""

import ctypes
from ctypes import wintypes
import json
import os
import pathlib
import subprocess
import struct
import sys
import threading
import time
import zlib

sys.stdout.reconfigure(encoding="utf-8", errors="replace")
sys.stderr.reconfigure(encoding="utf-8", errors="replace")

root = pathlib.Path(sys.argv[1]).resolve()
evidence = root / "doc/svs/project/M4"
user32, gdi32 = ctypes.windll.user32, ctypes.windll.gdi32
user32.SetProcessDpiAwarenessContext(ctypes.c_void_p(-4))
user32.GetDC.restype = wintypes.HDC
user32.GetForegroundWindow.restype = wintypes.HWND
user32.IsWindowVisible.argtypes = [wintypes.HWND]
user32.GetWindowRect.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.RECT)]
user32.GetWindowTextW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
user32.ShowWindow.argtypes = [wintypes.HWND, ctypes.c_int]
user32.MoveWindow.argtypes = [
    wintypes.HWND,
    ctypes.c_int,
    ctypes.c_int,
    ctypes.c_int,
    ctypes.c_int,
    wintypes.BOOL,
]
user32.PostMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
user32.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
user32.SetForegroundWindow.argtypes = [wintypes.HWND]
user32.BringWindowToTop.argtypes = [wintypes.HWND]
user32.AttachThreadInput.argtypes = [wintypes.DWORD, wintypes.DWORD, wintypes.BOOL]
user32.GetDC.argtypes = [wintypes.HWND]
gdi32.CreateCompatibleDC.restype = wintypes.HDC
gdi32.CreateCompatibleDC.argtypes = [wintypes.HDC]
gdi32.CreateCompatibleBitmap.restype = wintypes.HBITMAP
gdi32.CreateCompatibleBitmap.argtypes = [wintypes.HDC, ctypes.c_int, ctypes.c_int]
gdi32.SelectObject.restype = wintypes.HANDLE
gdi32.SelectObject.argtypes = [wintypes.HDC, wintypes.HANDLE]
gdi32.BitBlt.argtypes = [
    wintypes.HDC,
    ctypes.c_int,
    ctypes.c_int,
    ctypes.c_int,
    ctypes.c_int,
    wintypes.HDC,
    ctypes.c_int,
    ctypes.c_int,
    wintypes.DWORD,
]
gdi32.GetDIBits.argtypes = [
    wintypes.HDC,
    wintypes.HBITMAP,
    wintypes.UINT,
    wintypes.UINT,
    ctypes.c_void_p,
    ctypes.c_void_p,
    wintypes.UINT,
]
gdi32.DeleteObject.argtypes = [wintypes.HANDLE]
gdi32.DeleteDC.argtypes = [wintypes.HDC]
user32.ReleaseDC.argtypes = [wintypes.HWND, wintypes.HDC]
configuration = root / "build/Release/.lmmsrc.xml"
original_configuration = configuration.read_bytes()
environment = dict(os.environ, QT_QPA_PLATFORM="windows")
environment.pop("SVS_EMBEDDED_GUI_TEST", None)
process = subprocess.Popen(
    [str(root / "build/Release/lmms.exe"), "--config", str(configuration)],
    cwd=root / "build/Release",
    env=environment,
    stdout=subprocess.PIPE,
    stderr=subprocess.PIPE,
)
log = []


def forward(stream):
    for line in iter(stream.readline, b""):
        text = line.decode("utf-8", "replace").rstrip()
        log.append(text)
        print(text, flush=True)


threads = [
    threading.Thread(target=forward, args=(stream,)) for stream in (process.stdout, process.stderr)
]
for thread in threads:
    thread.start()

callback_type = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
windows = []


@callback_type
def collect(window, _):
    pid = wintypes.DWORD()
    user32.GetWindowThreadProcessId(window, ctypes.byref(pid))
    if pid.value == process.pid and user32.IsWindowVisible(window):
        rect = wintypes.RECT()
        user32.GetWindowRect(window, ctypes.byref(rect))
        if rect.right - rect.left > 600 and rect.bottom - rect.top > 400:
            title = ctypes.create_unicode_buffer(512)
            user32.GetWindowTextW(window, title, 512)
            windows.append((window, rect, title.value))
    return True


try:
    deadline = time.monotonic() + 30
    while not windows and process.poll() is None and time.monotonic() < deadline:
        user32.EnumWindows(collect, 0)
        time.sleep(0.2)
    assert windows, "Native development LMMS window did not become visible"
    # Startup may initially expose a large splash window. Select again only
    # after startup settles; an abandoned splash is not the application window.
    time.sleep(5)
    windows.clear()
    user32.EnumWindows(collect, 0)
    assert windows, "No stable native development LMMS window"
    window, _, title = windows[0]
    user32.ShowWindow(window, 9)
    user32.MoveWindow(window, 60, 40, 1280, 800, True)
    user32.SetForegroundWindow(window)
    foreground_thread = user32.GetWindowThreadProcessId(user32.GetForegroundWindow(), None)
    current_thread = ctypes.windll.kernel32.GetCurrentThreadId()
    attached = foreground_thread != current_thread and user32.AttachThreadInput(
        current_thread, foreground_thread, True
    )
    try:
        user32.BringWindowToTop(window)
        user32.SetForegroundWindow(window)
    finally:
        if attached:
            user32.AttachThreadInput(current_thread, foreground_thread, False)
    time.sleep(5)
    assert process.poll() is None and user32.IsWindowVisible(window), json.dumps(
        {
            "exitCode": process.poll(),
            "window": int(window),
            "visible": bool(user32.IsWindowVisible(window)),
            "title": title,
            "log": log,
        },
        ensure_ascii=False,
    )
    foreground_pid = wintypes.DWORD()
    user32.GetWindowThreadProcessId(user32.GetForegroundWindow(), ctypes.byref(foreground_pid))
    assert (
        foreground_pid.value == process.pid
    ), "Application is covered; real visible screenshot pending"
    rect = wintypes.RECT()
    assert user32.GetWindowRect(window, ctypes.byref(rect))
    width, height = rect.right - rect.left, rect.bottom - rect.top
    screen = user32.GetDC(0)
    memory = gdi32.CreateCompatibleDC(screen)
    bitmap = gdi32.CreateCompatibleBitmap(screen, width, height)
    previous = gdi32.SelectObject(memory, bitmap)
    try:
        assert gdi32.BitBlt(
            memory, 0, 0, width, height, screen, rect.left, rect.top, 0x00CC0020 | 0x40000000
        )
        gdi32.SelectObject(memory, previous)

        class Header(ctypes.Structure):
            _fields_ = [
                ("size", wintypes.DWORD),
                ("width", wintypes.LONG),
                ("height", wintypes.LONG),
                ("planes", wintypes.WORD),
                ("bits", wintypes.WORD),
                ("compression", wintypes.DWORD),
                ("image_size", wintypes.DWORD),
                ("x", wintypes.LONG),
                ("y", wintypes.LONG),
                ("used", wintypes.DWORD),
                ("important", wintypes.DWORD),
            ]

        header = Header(ctypes.sizeof(Header), width, -height, 1, 32)
        pixels = ctypes.create_string_buffer(width * height * 4)
        assert gdi32.GetDIBits(screen, bitmap, 0, height, pixels, ctypes.byref(header), 0) == height
        rgb = bytearray(width * height * 3)
        rgb[0::3], rgb[1::3], rgb[2::3] = pixels.raw[2::4], pixels.raw[1::4], pixels.raw[0::4]

        def chunk(kind, data):
            return (
                struct.pack(">I", len(data))
                + kind
                + data
                + struct.pack(">I", zlib.crc32(kind + data))
            )

        scanlines = b"".join(
            b"\0" + rgb[y * width * 3 : (y + 1) * width * 3] for y in range(height)
        )
        png = b"\x89PNG\r\n\x1a\n" + chunk(
            b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
        )
        png += chunk(b"IDAT", zlib.compress(scanlines)) + chunk(b"IEND", b"")
        (evidence / "deployed-native-main.png").write_bytes(png)
    finally:
        gdi32.DeleteObject(bitmap)
        gdi32.DeleteDC(memory)
        user32.ReleaseDC(0, screen)
    user32.PostMessageW(window, 0x0010, 0, 0)
    process.wait(timeout=15)
    assert process.returncode == 0, f"Native application exit: {process.returncode}"
    for thread in threads:
        thread.join(timeout=3)
    assert not any(
        "could not load" in line.lower() or "cannot load library" in line.lower() for line in log
    ), "Plugin/runtime load error"
    (evidence / "native-deployment-report.json").write_text(
        json.dumps(
            {
                "executable": str(root / "build/Release/lmms.exe"),
                "platform": "windows",
                "title": title,
                "window": int(window),
                "size": [width, height],
                "exitCode": process.returncode,
                "screenshot": "deployed-native-main.png",
                "log": log,
            },
            ensure_ascii=False,
            indent=2,
        ),
        encoding="utf-8",
    )
    print(
        "Real development lmms.exe exposed, desktop screenshot captured, normal close succeeded.",
        flush=True,
    )
finally:
    if process.poll() is None:
        process.terminate()
        process.wait(timeout=10)
    for thread in threads:
        thread.join(timeout=3)
    configuration.write_bytes(original_configuration)
