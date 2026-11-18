#!/usr/bin/env python3
"""Road to Dawn - terrain + layout generator.

Plan coordinates: metres, x = east, y = north, origin = centre of the settlement.
Heightmap: 2017 x 2017 samples, 1 m per sample, covers x,y in [-1008, 1008].
UE mapping used everywhere:  UE_X = north*100 cm,  UE_Y = east*100 cm,  UE_Z = height*100 cm.
Heightmap pixel (col, row) -> UE vertex (X = col, Y = row), so arr[row=east_idx, col=north_idx].
"""
import json, math, os, sys
import numpy as np
import cv2
from scipy import ndimage as ndi

OUT = sys.argv[1] if len(sys.argv) > 1 else "out"
os.makedirs(OUT, exist_ok=True)
rng = np.random.default_rng(20261117)

N = 2017
EXT = 1008
e = np.arange(N, dtype=np.float32) - EXT
E, NN = np.meshgrid(e, e, indexing="ij")          # arr[i=east idx, j=north idx]


def smooth(t):
    t = np.clip(t, 0.0, 1.0)
    return t * t * (3 - 2 * t)


def gauss2(cx, cy, sx, sy):
    return np.exp(-(((E - cx) / sx) ** 2 + ((NN - cy) / sy) ** 2) / 2.0)


# ----------------------------------------------------------------------------- reference -> plan
SRC = np.float32([[335, 302], [1468, 296], [925, 430], [900, 173]])   # leftmost, rightmost, gate, back of outer wall
R_OUT = 250.0
DST = np.float32([[-R_OUT, 0], [R_OUT, 0], [0, -R_OUT], [0, R_OUT]])
HM = cv2.getPerspectiveTransform(SRC, DST)


def img2plan(pts):
    a = np.float32(pts).reshape(-1, 1, 2)
    return cv2.perspectiveTransform(a, HM).reshape(-1, 2)


ROAD_IMG = [(925, 432), (937, 455), (955, 485), (965, 515), (950, 543), (905, 558), (840, 562), (760, 560),
            (690, 560), (630, 568), (598, 590), (600, 612), (625, 630), (680, 642), (760, 648), (830, 655),
            (890, 675), (922, 705), (920, 735), (895, 762), (840, 772), (760, 775), (680, 775), (600, 778),
            (520, 780), (460, 790), (420, 803)]
road_ctrl = img2plan(ROAD_IMG)
road_ctrl[0] = (0.0, -252.0)                      # start exactly at the outer gate
VIEWPOINT = np.array([-300.0, -842.0])            # where the Redneck stands (image: ledge at the left)
road_ctrl = np.vstack([road_ctrl, [-262.0, -872.0]])   # short spur to the ledge


def catmull(points, step=2.0):
    P = np.vstack([points[0], points, points[-1]])
    out = []
    for i in range(1, len(P) - 2):
        p0, p1, p2, p3 = P[i - 1], P[i], P[i + 1], P[i + 2]
        n = max(2, int(np.linalg.norm(p2 - p1) / step))
        for t in np.linspace(0, 1, n, endpoint=False):
            t2, t3 = t * t, t ** 3
            out.append(0.5 * ((2 * p1) + (-p0 + p2) * t + (2 * p0 - 5 * p1 + 4 * p2 - p3) * t2 + (-p0 + 3 * p1 - 3 * p2 + p3) * t3))
    out.append(points[-1])
    return np.array(out)


def resample(poly, step):
    seg = np.linalg.norm(np.diff(poly, axis=0), axis=1)
    s = np.concatenate([[0], np.cumsum(seg)])
    n = max(2, int(s[-1] / step))
    t = np.linspace(0, s[-1], n)
    return np.stack([np.interp(t, s, poly[:, 0]), np.interp(t, s, poly[:, 1])], axis=1), t


road_poly, road_s = resample(catmull(road_ctrl, 2.0), 2.0)
ROAD_LEN = float(road_s[-1])
Z_TOP = 52.0                                       # road end is this many metres above the gate
s_n = np.clip((road_s - 90.0) / (ROAD_LEN - 90.0), 0, 1)
road_z = Z_TOP * (0.55 * s_n + 0.45 * (s_n * s_n * (3 - 2 * s_n)))

# ----------------------------------------------------------------------------- road raster / distance
ri = np.clip(np.round(road_poly[:, 0] + EXT).astype(int), 0, N - 1)
rj = np.clip(np.round(road_poly[:, 1] + EXT).astype(int), 0, N - 1)
road_mask = np.zeros((N, N), bool)
road_zg = np.zeros((N, N), np.float32)
road_mask[ri, rj] = True
road_zg[ri, rj] = road_z
dist_r, (ii, jj) = ndi.distance_transform_edt(~road_mask, return_indices=True)
z_near = road_zg[ii, jj]

# ----------------------------------------------------------------------------- natural terrain
r0 = np.hypot(E, NN)                                # distance from settlement centre
g_east = 1.0 - 0.68 * smooth((E + 30.0) / 230.0)    # meadow east of the road stays low
F_road = ndi.gaussian_filter(z_near * np.exp(-(dist_r / 250.0) ** 2) * g_east, 28.0)   # blur: removes Voronoi seams between road legs
H = F_road.copy()
H += 40.0 * smooth((-E - 380.0) / 260.0) * (0.55 + 0.45 * smooth((-NN + 150.0) / 650.0))   # west ridge
H += 88.0 * gauss2(-430.0, 390.0, 190.0, 240.0)    # north-west mountain with the facility
H += 22.0 * smooth((NN - 360.0) / 520.0)            # northern forest hills
H += 12.0 * smooth((E - 430.0) / 320.0)             # far bank of the river
H += 14.0 * gauss2(VIEWPOINT[0] - 25, VIEWPOINT[1] + 20, 60.0, 55.0)   # rocky knoll under the viewpoint


def blurred_noise(sigma, amp):
    n = rng.standard_normal((N, N)).astype(np.float32)
    n = ndi.gaussian_filter(n, sigma, mode="reflect")
    n /= (n.std() + 1e-6)
    return n * amp


noise = blurred_noise(90, 7.0) + blurred_noise(35, 3.2) + blurred_noise(14, 1.2) + blurred_noise(5, 0.35)

# flat areas: village + ring + 60 m apron, meadow
flat_village = 1.0 - smooth((r0 - 262.0) / 110.0)
MEAD_C, MEAD_A = np.array([205.0, -470.0]), np.array([200.0, 230.0])
meadow = np.exp(-(((E - MEAD_C[0]) / MEAD_A[0]) ** 4 + ((NN - MEAD_C[1]) / MEAD_A[1]) ** 4))   # ~1 inside, ~0 outside
damp = np.clip(1.0 - 0.92 * flat_village - 0.8 * meadow, 0.05, 1.0)
H = H + noise * damp * smooth((dist_r - 6.0) / 75.0)        # no random relief right next to the road

# facility terrace on the hillside (north-west of the settlement, touching the wall)
FAC = np.array([-212.0, 126.0])
fac_pad = smooth(1.0 - (np.hypot(E - FAC[0], NN - FAC[1]) - 55.0) / 40.0)
H = H * (1 - fac_pad) + 7.0 * fac_pad

# village platform at 0 m
H = H * (1 - flat_village) + 0.0 * flat_village

# viewpoint platform: road end + ledge
vp_dist = np.hypot(E - VIEWPOINT[0], NN - VIEWPOINT[1])
vp_z = float(road_z[-1]) + 9.0
vp_pad = smooth(1.0 - (vp_dist - 10.0) / 18.0)
H = H * (1 - vp_pad) + vp_z * vp_pad

# road cut/fill
cut = smooth((dist_r - 5.0) / 34.0)
H = z_near * (1 - cut) + H * cut
H = np.where(vp_pad > 0.5, H, H)  # keep platform as written above (road blend must not erase it)
H = H * (1 - vp_pad) + vp_z * vp_pad

# ----------------------------------------------------------------------------- river
def river_x(y):
    return 318.0 + 32.0 * np.sin(y / 170.0) + 14.0 * np.sin(y / 70.0 + 1.3)


ry = np.arange(-EXT, EXT + 1, 2.0)
river_poly = np.stack([river_x(ry), ry], axis=1)
rvi = np.clip(np.round(river_poly[:, 0] + EXT).astype(int), 0, N - 1)
rvj = np.clip(np.round(river_poly[:, 1] + EXT).astype(int), 0, N - 1)
river_mask = np.zeros((N, N), bool)
river_mask[rvi, rvj] = True
dist_v = ndi.distance_transform_edt(~river_mask)
# smooth the banks so the channel does not look like a ditch
H = ndi.gaussian_filter(H, 2.0) if False else H
carve = 3.6 * (1.0 - smooth((dist_v - 7.0) / 15.0))
H = H - carve
H = ndi.gaussian_filter(H, 1.2)                                     # remove stair-steps from the raster distances
# re-impose flat village after the blur
H = H * (1 - flat_village * 0.98) + 0.0 * flat_village * 0.98

# ----------------------------------------------------------------------------- derived
gy, gx = np.gradient(H)                    # per metre
slope = np.degrees(np.arctan(np.hypot(gx, gy)))

hm16 = np.clip(H * 128.0 + 32768.0, 0, 65535).astype(np.uint16)
# arr[row=east idx, col=north idx] is exactly what UE expects (col -> X north, row -> Y east)
cv2.imwrite(os.path.join(OUT, "RoadToDawn_heightmap_2017.png"), hm16)


def sample(x, y):
    fi, fj = x + EXT, y + EXT
    i0, j0 = int(np.floor(fi)), int(np.floor(fj))
    i0 = min(max(i0, 0), N - 2); j0 = min(max(j0, 0), N - 2)
    a, b = fi - i0, fj - j0
    return float((1 - a) * (1 - b) * H[i0, j0] + a * (1 - b) * H[i0 + 1, j0] + (1 - a) * b * H[i0, j0 + 1] + a * b * H[i0 + 1, j0 + 1])


def sample_slope(x, y):
    i, j = min(max(int(round(x + EXT)), 0), N - 1), min(max(int(round(y + EXT)), 0), N - 1)
    return float(slope[i, j])


# ----------------------------------------------------------------------------- layout
layout = {"version": 1, "units": "metres; plan x=east y=north; script converts to UE cm (X=north, Y=east)"}

# road (centre line, z follows the carved terrain)
rp = [[round(float(x), 2), round(float(y), 2), round(sample(x, y), 2)] for x, y in road_poly]
layout["road"] = {"width": 7.0, "points": rp, "length_m": round(ROAD_LEN, 1)}

# ring road + village streets
def circle_pts(cx, cy, r, step=6.0, a0=0.0, a1=360.0):
    n = max(8, int(2 * math.pi * r * (a1 - a0) / 360.0 / step))
    ang = np.linspace(math.radians(a0), math.radians(a1), n, endpoint=False)
    return np.stack([cx + r * np.cos(ang), cy + r * np.sin(ang)], axis=1)


RING_C, RING_R = (0.0, 30.0), 207.0
ring_pts = circle_pts(*RING_C, RING_R, 6.0)
ring_pts = np.vstack([ring_pts, ring_pts[:1]])
streets = []
streets.append({"name": "ring", "width": 6.0, "pts": [[round(float(x), 2), round(float(y), 2)] for x, y in ring_pts]})
streets.append({"name": "gate_approach", "width": 7.0, "pts": [[0, -252], [0, -107]]})
streets.append({"name": "main_street", "width": 7.0, "pts": [[0, -107], [0, 214]]})
streets.append({"name": "cross_street", "width": 6.0, "pts": [[-160, 40], [160, 40]]})
streets.append({"name": "north_loop", "width": 5.0, "pts": [[-120, 120], [0, 150], [120, 120]]})
for s in streets:
    pts = np.array(s["pts"], dtype=float)
    dense, _ = resample(pts, 3.0)
    s["pts"] = [[round(float(x), 2), round(float(y), 2), round(sample(x, y), 2)] for x, y in dense]
layout["streets"] = streets

# walls
def wall_segments(cx, cy, r, h, thick, gap_deg_center, gap_w, seg=8.0, kind="fence"):
    out = []
    n = int(2 * math.pi * r / seg)
    for k in range(n):
        a0 = 2 * math.pi * k / n
        a1 = 2 * math.pi * (k + 1) / n
        am = 0.5 * (a0 + a1)
        px0, py0 = cx + r * math.cos(a0), cy + r * math.sin(a0)
        px1, py1 = cx + r * math.cos(a1), cy + r * math.sin(a1)
        mx, my = 0.5 * (px0 + px1), 0.5 * (py0 + py1)
        if gap_deg_center is not None:
            # gap on the south side of the ring: |x - gate_x| < gap_w/2 and y below centre
            if abs(mx - 0.0) < gap_w / 2.0 and my < cy:
                continue
        ln = math.hypot(px1 - px0, py1 - py0) + 0.4
        yaw = math.degrees(math.atan2(py1 - py0, px1 - px0))
        out.append({"x": round(mx, 2), "y": round(my, 2), "z": round(sample(mx, my), 2), "len": round(ln, 2),
                    "yaw": round(yaw, 2), "h": h, "t": thick, "kind": kind})
    return out


walls = wall_segments(0.0, 0.0, R_OUT, 6.5, 0.7, 1, 18.0, 8.0, "outer_fence")
walls += wall_segments(20.0, 60.0, 168.0, 5.0, 1.2, 1, 14.0, 8.0, "inner_wall")
layout["walls"] = walls

gates = [
    {"name": "Outer gate", "x": 0.0, "y": -250.0, "w": 18.0, "h": 7.5, "yaw": 0.0},
    {"name": "Inner gate", "x": 0.0, "y": -107.0, "w": 14.0, "h": 6.0, "yaw": 0.0},
]
for g in gates:
    g["z"] = round(sample(g["x"], g["y"]), 2)
layout["gates"] = gates

towers = []
for name, x, y in (("Outer tower W", -22.0, -249.0), ("Outer tower E", 22.0, -249.0),
                   ("Inner tower W", -17.0, -106.0), ("Inner tower E", 17.0, -106.0)):
    towers.append({"name": name, "x": x, "y": y, "z": round(sample(x, y), 2), "size": 4.0, "h": 11.0 if "Outer" in name else 8.0})
layout["towers"] = towers

# buildings: seeds from the reference + procedural fill along the streets, kept inside the inner wall
IN_C, IN_R = np.array([20.0, 60.0]), 168.0
specials = [
    {"name": "Shop", "x": -75.0, "y": 2.0, "w": 24.0, "d": 14.0, "h": 7.0, "yaw": 0.0, "kind": "shop"},
    {"name": "Gas station", "x": 108.0, "y": -2.0, "w": 22.0, "d": 14.0, "h": 5.5, "yaw": 0.0, "kind": "gas"},
    {"name": "Water tower", "x": 96.0, "y": 168.0, "w": 7.0, "d": 7.0, "h": 24.0, "yaw": 0.0, "kind": "tower"},
]
bld = list(specials)
seeds = img2plan([(780, 205), (870, 210), (1090, 250), (1000, 245), (940, 250), (800, 230), (745, 255), (640, 250),
                  (1185, 255), (1245, 235), (1160, 210), (565, 305), (985, 290), (690, 270), (1050, 220), (1220, 205)])
for (x, y) in seeds:
    d = np.hypot(x - IN_C[0], y - IN_C[1])
    if d > IN_R - 20:
        x, y = IN_C[0] + (x - IN_C[0]) * (IN_R - 20) / d, IN_C[1] + (y - IN_C[1]) * (IN_R - 20) / d
    bld.append({"name": "House", "x": float(x), "y": float(y), "w": float(rng.uniform(10, 15)), "d": float(rng.uniform(8, 11)),
                "h": float(rng.uniform(5.5, 7.5)), "yaw": float(rng.choice([0, 0, 90, 8, -8])), "kind": "house"})


def free(x, y, r):
    if np.hypot(x - IN_C[0], y - IN_C[1]) > IN_R - 16:
        return False
    if abs(x) < 8 + r * 0.4 and -112 < y < 218:       # main street
        return False
    if abs(y - 40) < 7 + r * 0.4 and abs(x) < 165:    # cross street
        return False
    for b in bld:
        if math.hypot(x - b["x"], y - b["y"]) < max(r, 0.6 * max(b["w"], b["d"])) + 7.0:
            return False
    return True


for _ in range(900):
    if len(bld) >= 46:
        break
    x, y = rng.uniform(-170, 170), rng.uniform(-100, 225)
    if free(x, y, 8.0):
        bld.append({"name": "House", "x": float(x), "y": float(y), "w": float(rng.uniform(9, 14)), "d": float(rng.uniform(8, 11)),
                    "h": float(rng.uniform(5.0, 7.0)), "yaw": float(rng.choice([0, 90, 0, 90, 10, -10])), "kind": "house"})
for b in bld:
    for k in ("x", "y", "w", "d", "h", "yaw"):
        b[k] = round(float(b[k]), 2)
    b["z"] = round(sample(b["x"], b["y"]), 2)
layout["buildings"] = bld

# outside the walls: facility on the hillside + containers by the inner wall
layout["facility"] = {"x": float(FAC[0]), "y": float(FAC[1]), "z": round(sample(*FAC), 2), "w": 64.0, "d": 30.0, "h": 15.0, "yaw": 35.0}
layout["containers"] = [
    {"x": -126.0, "y": -118.0, "z": round(sample(-126.0, -118.0), 2), "w": 6.0, "d": 2.5, "h": 2.6, "yaw": 0.0},
    {"x": -116.0, "y": -118.0, "z": round(sample(-116.0, -118.0), 2), "w": 6.0, "d": 2.5, "h": 2.6, "yaw": 0.0},
]

# lamp posts along the ring road
lamps = []
for k in range(0, len(ring_pts) - 1, 7):
    x, y = ring_pts[k]
    lamps.append({"x": round(float(x), 2), "y": round(float(y), 2), "z": round(sample(x, y), 2)})
layout["lamps"] = lamps

# river (water ribbon) - water level = bed + 1.5
wl = []
for x, y in river_poly[::2]:
    wl.append([round(float(x), 2), round(float(y), 2), round(sample(x, y) + 1.5, 2)])
# make the water level monotonic downstream (north -> south)
for k in range(len(wl) - 2, -1, -1):
    wl[k][2] = max(wl[k][2], wl[k + 1][2])
layout["river"] = {"width": 20.0, "points": wl}

# player start + beacons
vp = [float(VIEWPOINT[0]), float(VIEWPOINT[1])]
face = math.degrees(math.atan2(0.0 - vp[1], 0.0 - vp[0]))
layout["player_start"] = {"x": vp[0], "y": vp[1], "z": round(sample(*vp) + 0.2, 2), "yaw_math_deg": round(face, 2)}
layout["binoculars"] = {"x": vp[0] + 4.0, "y": vp[1] + 3.0, "z": round(sample(vp[0] + 4.0, vp[1] + 3.0) + 0.1, 2)}
trap_xy = (4.0, -300.0)
layout["bear_trap"] = {"x": trap_xy[0], "y": trap_xy[1], "z": round(sample(*trap_xy) + 0.1, 2)}
layout["navmesh"] = {"x": 0.0, "y": -420.0, "w": 700.0, "d": 1200.0, "z": 0.0}

# trees
STEP = 12.0
trees = []
for gx in np.arange(-EXT + 6, EXT - 6, STEP):
    for gy_ in np.arange(-EXT + 6, EXT - 6, STEP):
        x = gx + rng.uniform(-4.5, 4.5)
        y = gy_ + rng.uniform(-4.5, 4.5)
        i, j = int(round(x + EXT)), int(round(y + EXT))
        if not (0 <= i < N and 0 <= j < N):
            continue
        if r0[i, j] < 292.0:
            continue
        if dist_r[i, j] < 9.0 or dist_v[i, j] < 24.0:
            continue
        if math.hypot(x - FAC[0], y - FAC[1]) < 62.0 and r0[i, j] < 330:
            continue
        if math.hypot(x - vp[0], y - vp[1]) < 20.0:
            continue
        if slope[i, j] > 36.0:
            continue
        pm = 0.04 if meadow[i, j] > 0.35 else 0.9
        pm *= 1.0 - 0.45 * smooth((H[i, j] - 50.0) / 40.0)
        if rng.random() > pm:
            continue
        z = float(H[i, j])
        trees.append([round(float(x), 1), round(float(y), 1), round(z, 2), round(float(rng.uniform(0.75, 1.45)), 2)])
if len(trees) > 24000:
    idx = rng.choice(len(trees), 24000, replace=False)
    trees = [trees[k] for k in sorted(idx)]
layout["trees"] = trees

# stats
grade = np.abs(np.diff(road_z)) / 2.0
layout["stats"] = {
    "road_length_m": round(ROAD_LEN, 1), "road_max_grade_pct": round(float(grade.max() * 100), 1),
    "road_avg_grade_pct": round(float(road_z[-1] / ROAD_LEN * 100), 1), "viewpoint_height_m": round(vp_z, 1),
    "height_min_m": round(float(H.min()), 1), "height_max_m": round(float(H.max()), 1), "trees": len(trees),
    "buildings": len(bld), "wall_segments": len(walls),
    "gate_to_viewpoint_straight_m": round(math.hypot(vp[0], vp[1] + 250.0), 1),
}
with open(os.path.join(OUT, "rtd_layout.json"), "w") as f:
    json.dump(layout, f, separators=(",", ":"))

# ----------------------------------------------------------------------------- masks (8 bit, same orientation as heightmap)
def save_mask(name, a):
    cv2.imwrite(os.path.join(OUT, name), np.clip(a * 255.0, 0, 255).astype(np.uint8))


road_band = np.clip(1.0 - (dist_r - 3.2) / 2.5, 0, 1)
streets_mask = np.zeros((N, N), np.float32)
save_mask("mask_road.png", road_band)
rock = smooth((slope - 24.0) / 12.0)
save_mask("mask_rock.png", rock)
save_mask("mask_meadow.png", np.clip(meadow * (1 - rock), 0, 1))
mud = np.clip(1.0 - (dist_v - 9.0) / 10.0, 0, 1)
save_mask("mask_riverbank.png", mud)

# ----------------------------------------------------------------------------- preview (north up, east right)
def plan_image(scale=0.5):
    Hs = H[::2, ::2].T[::-1]                      # rows = north (top = north), cols = east
    hh = (Hs - Hs.min()) / (Hs.max() - Hs.min())
    ls = ndi.gaussian_filter(Hs, 1.0)
    gy2, gx2 = np.gradient(ls)
    shade = np.clip(0.55 + (-gx2 * 0.8 + gy2 * 0.6) * 0.9, 0.2, 1.1)   # light from the west/south-west
    base = np.stack([0.20 + 0.45 * hh, 0.34 + 0.40 * hh, 0.18 + 0.25 * hh], axis=2)
    mead = meadow[::2, ::2].T[::-1]
    base = base * (1 - mead[..., None] * 0.5) + np.array([0.62, 0.55, 0.28]) * (mead[..., None] * 0.5)
    rk = rock[::2, ::2].T[::-1]
    base = base * (1 - rk[..., None] * 0.6) + np.array([0.5, 0.48, 0.46]) * (rk[..., None] * 0.6)
    img = np.clip(base * shade[..., None], 0, 1)
    return (img * 255).astype(np.uint8)


prev = plan_image()
h2, w2 = prev.shape[:2]


def to_px(x, y):
    return int(round((x + EXT) / 2.0)), int(round((EXT - y) / 2.0))


def draw_poly(img, pts, color, th):
    p = np.array([to_px(x, y) for x, y in pts], np.int32).reshape(-1, 1, 2)
    cv2.polylines(img, [p], False, color, th, cv2.LINE_AA)


bgr = cv2.cvtColor(prev, cv2.COLOR_RGB2BGR)
draw_poly(bgr, [(p[0], p[1]) for p in layout["river"]["points"]], (200, 140, 60), 7)
draw_poly(bgr, [(p[0], p[1]) for p in rp], (150, 190, 215), 4)
for s in streets:
    draw_poly(bgr, [(p[0], p[1]) for p in s["pts"]], (150, 190, 215), 2)
cv2.circle(bgr, to_px(0, 0), int(R_OUT / 2), (60, 60, 200), 2, cv2.LINE_AA)
cv2.circle(bgr, to_px(20, 60), int(168 / 2), (60, 60, 200), 2, cv2.LINE_AA)
for b in bld:
    cv2.circle(bgr, to_px(b["x"], b["y"]), 2, (40, 40, 40), -1)
cv2.circle(bgr, to_px(*vp), 6, (0, 255, 255), -1)
cv2.putText(bgr, "Redneck start", (to_px(*vp)[0] + 10, to_px(*vp)[1] + 4), cv2.FONT_HERSHEY_SIMPLEX, 0.5, (0, 255, 255), 1, cv2.LINE_AA)
cv2.putText(bgr, "Settlement", (to_px(-40, 10)[0], to_px(-40, 10)[1]), cv2.FONT_HERSHEY_SIMPLEX, 0.5, (255, 255, 255), 1, cv2.LINE_AA)
cv2.putText(bgr, "Facility", (to_px(FAC[0] - 50, FAC[1] + 25)[0], to_px(FAC[0] - 50, FAC[1] + 25)[1]), cv2.FONT_HERSHEY_SIMPLEX, 0.45, (255, 255, 255), 1, cv2.LINE_AA)
cv2.putText(bgr, "River", (to_px(river_x(500) + 20, 500)[0], to_px(0, 500)[1]), cv2.FONT_HERSHEY_SIMPLEX, 0.5, (255, 200, 120), 1, cv2.LINE_AA)
cv2.putText(bgr, "N", (10, 22), cv2.FONT_HERSHEY_SIMPLEX, 0.7, (255, 255, 255), 2, cv2.LINE_AA)
for k in range(-1000, 1001, 250):
    px, py = to_px(k, -1000)
    cv2.putText(bgr, "%dm" % k, (px - 12, h2 - 4), cv2.FONT_HERSHEY_SIMPLEX, 0.35, (255, 255, 255), 1, cv2.LINE_AA)
cv2.imwrite(os.path.join(OUT, "plan_preview.png"), bgr)

# ----------------------------------------------------------------------------- overlay on the reference
ref = cv2.imread(sys.argv[2]) if len(sys.argv) > 2 else None
if ref is not None:
    Hinv = np.linalg.inv(HM)

    def plan2img(pts):
        a = np.float32(pts).reshape(-1, 1, 2)
        return cv2.perspectiveTransform(a, Hinv).reshape(-1, 2)

    ov = ref.copy()

    def poly_img(pts, color, th):
        q = plan2img(np.array(pts)[:, :2]).astype(np.int32).reshape(-1, 1, 2)
        cv2.polylines(ov, [q], False, color, th, cv2.LINE_AA)

    poly_img([(p[0], p[1]) for p in rp], (0, 255, 255), 3)
    poly_img(circle_pts(0, 0, R_OUT, 4.0).tolist() + [circle_pts(0, 0, R_OUT, 4.0)[0].tolist()], (0, 0, 255), 3)
    poly_img(circle_pts(20, 60, 168, 4.0).tolist() + [circle_pts(20, 60, 168, 4.0)[0].tolist()], (0, 128, 255), 3)
    poly_img(circle_pts(*RING_C, RING_R, 4.0).tolist() + [circle_pts(*RING_C, RING_R, 4.0)[0].tolist()], (255, 0, 255), 2)
    poly_img([(p[0], p[1]) for p in layout["river"]["points"] if -330 < p[1] < 420], (255, 128, 0), 4)
    for b in bld:
        q = plan2img([[b["x"], b["y"]]])[0]
        cv2.circle(ov, (int(q[0]), int(q[1])), 5, (0, 255, 0), -1)
    q = plan2img([list(vp)])[0]
    cv2.circle(ov, (int(q[0]), int(q[1])), 8, (255, 255, 255), 2)
    cv2.imwrite(os.path.join(OUT, "overlay_on_reference.png"), ov)

print(json.dumps(layout["stats"], indent=1))
