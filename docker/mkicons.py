#!/usr/bin/env python3
# AmiTCP_NG. Copyright (C) 2026 Andy Taylor (MW0MWZ). GPL v2 (see COPYING).
#
# Generate classic AmigaOS .info icon files (our own artwork) for the installer
# package, so the disk, its drawer and the Install/ReadMe files show real Workbench
# icons and can be double-clicked. Emits OS1.x-style DiskObjects (backward-compatible
# with every Workbench): magic 0xE310, one planar image, optional DefaultTool.
#
#   mkicons.py project <outfile> <default-tool>   # e.g. default-tool "Installer"
#   mkicons.py drawer  <outfile>
#   mkicons.py disk    <outfile>
#
# The image is a small 2-bitplane (4 colour) icon drawn from code; pens follow the
# standard Workbench palette (0 grey/background, 1 black, 2 white, 3 blue).
import struct, sys

W, H, DEPTH = 60, 24, 2

def blank():
    return [[0] * W for _ in range(H)]

def frame(g, pen=1):
    for x in range(W):
        g[0][x] = g[H - 1][x] = pen
    for y in range(H):
        g[y][0] = g[y][W - 1] = pen

def fill(g, x0, y0, x1, y1, pen):
    for y in range(y0, y1):
        for x in range(x0, x1):
            g[y][x] = pen

def draw_project():
    g = blank()
    fill(g, 1, 1, W - 1, H - 1, 2)          # white body
    frame(g)                                # black border
    fill(g, 1, 1, W - 1, 6, 3)              # blue title bar
    for y in range(9, H - 3, 3):            # "text" lines
        fill(g, 4, y, W - 6, y + 1, 1)
    return g

def draw_drawer():
    g = blank()
    fill(g, 1, 3, W - 1, H - 1, 2)          # white body
    for x in range(W):                      # bottom + sides
        g[H - 1][x] = 1
    for y in range(3, H):
        g[y][0] = g[y][W - 1] = 1
    fill(g, 4, 0, W // 2, 3, 2)             # tab
    for x in range(4, W // 2):
        g[0][x] = 1
    for y in range(0, 4):
        g[y][4] = g[y][W // 2 - 1] = 1
    for x in range(1, W - 1):
        g[3][x] = 1
    return g

def draw_disk():
    g = blank()
    fill(g, 1, 1, W - 1, H - 1, 2)          # white body
    frame(g)                                # black border
    fill(g, W - 14, 2, W - 4, 8, 1)         # metal shutter, top-right
    fill(g, 5, H // 2, W - 5, H - 4, 3)     # blue label area
    return g

def planes(g):
    wpr = (W + 15) // 16
    out = bytearray()
    for p in range(DEPTH):
        for y in range(H):
            row = g[y]
            for wi in range(wpr):
                word = 0
                for bit in range(16):
                    x = wi * 16 + bit
                    if x < W and (row[x] >> p) & 1:
                        word |= 1 << (15 - bit)
                out += struct.pack('>H', word)
    return bytes(out)

# do_Type: 1=disk, 2=drawer, 4=project
def make(do_type, grid, default_tool=None, drawer=False, stack=4096):
    o = bytearray()
    o += struct.pack('>HH', 0xE310, 1)                       # magic, version
    # struct Gadget (44 bytes)
    o += struct.pack('>I', 0)                                # ga_Next
    o += struct.pack('>hhhh', 0, 0, W, H)                    # left, top, width, height
    o += struct.pack('>HHH', 0x0005, 0x0001, 0x0001)        # flags, activation, type
    o += struct.pack('>I', 1)                                # ga_GadgetRender (present)
    o += struct.pack('>I', 0)                                # ga_SelectRender (none)
    o += struct.pack('>I', 0)                                # ga_GadgetText
    o += struct.pack('>i', 0)                                # ga_MutualExclude
    o += struct.pack('>I', 0)                                # ga_SpecialInfo
    o += struct.pack('>H', 0)                                # ga_GadgetID
    o += struct.pack('>I', 0)                                # ga_UserData (OS1.x icon)
    # DiskObject remainder
    o += struct.pack('>BB', do_type, 0)                     # do_Type, pad
    o += struct.pack('>I', 1 if default_tool else 0)        # do_DefaultTool ptr
    o += struct.pack('>I', 0)                                # do_ToolTypes
    o += struct.pack('>II', 0x80000000, 0x80000000)         # do_CurrentX/Y = NO_ICON_POSITION
    o += struct.pack('>I', 1 if drawer else 0)              # do_DrawerData ptr
    o += struct.pack('>I', 0)                                # do_ToolWindow
    o += struct.pack('>i', stack)                           # do_StackSize
    # body: DrawerData (disk/drawer) then Image then DefaultTool
    if drawer:
        nw = struct.pack('>hhhh', 60, 50, 320, 150)        # NewWindow: left, top, w, h
        nw += struct.pack('>BB', 255, 255)                  # detail/block pen
        nw += struct.pack('>II', 0, 0)                      # IDCMP, Flags
        nw += struct.pack('>IIII', 0, 0, 0, 0)              # gadget, checkmark, title, screen
        nw += struct.pack('>I', 0)                          # bitmap
        nw += struct.pack('>hh', 90, 40)                    # min w/h
        nw += struct.pack('>HH', 0xFFFF, 0xFFFF)            # max w/h
        nw += struct.pack('>H', 1)                          # type = WBENCHSCREEN
        o += nw
        o += struct.pack('>ii', 0, 0)                       # dd_CurrentX/Y
    # struct Image (20 bytes) + planar data
    o += struct.pack('>hhhh', 0, 0, W, H)                  # left, top, width, height
    o += struct.pack('>h', DEPTH)                           # depth
    o += struct.pack('>I', 1)                               # ImageData (present)
    o += struct.pack('>BB', (1 << DEPTH) - 1, 0)           # PlanePick, PlaneOnOff
    o += struct.pack('>I', 0)                               # NextImage
    o += planes(grid)
    if default_tool:
        s = default_tool.encode('latin-1') + b'\0'
        o += struct.pack('>I', len(s)) + s
    return bytes(o)

def main():
    kind, outfile = sys.argv[1], sys.argv[2]
    tool = sys.argv[3] if len(sys.argv) > 3 else None
    if kind == 'project':
        data = make(4, draw_project(), default_tool=tool, stack=10000)
    elif kind == 'drawer':
        data = make(2, draw_drawer(), drawer=True)
    elif kind == 'disk':
        data = make(1, draw_disk(), drawer=True)
    else:
        sys.exit("kind must be project|drawer|disk")
    with open(outfile, 'wb') as f:
        f.write(data)

if __name__ == '__main__':
    main()
