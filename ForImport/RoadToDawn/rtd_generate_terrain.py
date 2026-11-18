#!/usr/bin/env python3
"""Road to Dawn - terrain + layout generator.

Plan coordinates: metres, x = east, y = north, origin = centre of the settlement.
Heightmap: 1009 x 1009 samples, 1 m per sample, covers x,y in [-504, 504].  (v2: whole level scaled by SC=0.5)
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

SC = 0.5      # plan scale vs the first version (settlement R 250 -> 125 m)
HS = 0.5      # height scale (keeps road grades and slopes the same)
N = 1009
EXT = 504
e = np.arange(N, dtype=np.float32) - EXT
E, NN = np.meshgrid(e, e, indexing="ij")          # arr[i=east idx, j=north idx]


def smooth(t):
    t = np.clip(t, 0.0, 1.0)
    return t * t * (3 - 2 * t)


def gauss2(cx, cy, sx, sy):
    return np.exp(-(((E - cx) / sx) ** 2 + ((NN - cy) / sy) ** 2) / 2.0)


# ----------------------------------------------------------------------------- reference -> plan
# Calibrated on the NORTH-SIDE reference (image top = plan south).  Four corners of the outer wall <-> plan (+-125, +-125).
IMG_REF = np.float32([[172, 485], [1052, 485], [1106, 950], [84, 950]])
PLAN_REF = np.float32([[125, -125], [-125, -125], [-125, 125], [125, 125]])
R_OUT = 250.0 * SC
HM = cv2.getPerspectiveTransform(IMG_REF, PLAN_REF)


def img2plan(pts):
    a = np.float32(pts).reshape(-1, 1, 2)
    return cv2.perspectiveTransform(a, HM).reshape(-1, 2)


# serpentine centre line traced on the reference (full-resolution pixels): gate -> straight -> lower leg -> U-turn -> middle leg -> U-turn -> upper leg -> tunnel
ROAD_IMG = [(612, 470), (612, 430), (613, 385), (620, 355), (640, 340), (690, 328), (760, 324), (830, 318), (880, 312),
            (905, 300), (908, 282), (880, 266), (830, 255), (780, 245), (735, 236), (708, 225), (703, 208), (715, 195),
            (760, 190), (830, 188), (900, 188), (970, 188), (1025, 188)]
road_ctrl = img2plan(ROAD_IMG)
road_ctrl[0] = (0.0, -(R_OUT + 2.0))                      # start exactly at the outer gate
road_ctrl[1] = (0.0, road_ctrl[1][1])


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
E_END = road_poly[-1].copy()                                  # road end = foot of the rock face with the collapsed tunnel
_d = road_poly[-1] - road_poly[-6]
R_DIR = _d / np.linalg.norm(_d)                               # road heading at its end (into the rock)
R_LAT = np.array([-R_DIR[1], R_DIR[0]])                       # left of the heading


def rloc(u, v):
    return E_END + R_DIR * u + R_LAT * v


VIEWPOINT = rloc(12.0, 2.0)                                    # the hero stands on the rock slab above the portal
Z_TOP = 52.0                                       # road end is this many metres above the gate
s_n = np.clip((road_s - 90.0 * SC) / (ROAD_LEN - 90.0 * SC), 0, 1)
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

# ----------------------------------------------------------------------------- gorge geometry (carved later, plateau raised now)
GORGE = np.array([(-122.0, -370.0), (-112.0, -330.0), (-108.0, -290.0), (-106.0, -250.0), (-104.0, -210.0)])
g_dense, g_s = resample(catmull(GORGE, 2.0), 1.0)
g_t = g_s / g_s[-1]
g_mask = np.zeros((N, N), bool)
gi = np.clip(np.round(g_dense[:, 0] + EXT).astype(int), 0, N - 1)
gj = np.clip(np.round(g_dense[:, 1] + EXT).astype(int), 0, N - 1)
g_mask[gi, gj] = True
t_map = np.zeros((N, N), np.float32)
t_map[gi, gj] = g_t
_tan = np.gradient(g_dense, axis=0)
_tan /= (np.linalg.norm(_tan, axis=1, keepdims=True) + 1e-9)
tx_map = np.zeros((N, N), np.float32); ty_map = np.zeros((N, N), np.float32)
tx_map[gi, gj] = _tan[:, 0]; ty_map[gi, gj] = _tan[:, 1]
dist_g, (gii, gjj) = ndi.distance_transform_edt(~g_mask, return_indices=True)
t_pix = t_map[gii, gjj]
# which side of the gorge: +1 = left of the flow direction = the viewpoint (west) side, -1 = the road side
S_LAT = tx_map[gii, gjj] * (NN - (gjj - EXT)) - ty_map[gii, gjj] * (E - (gii - EXT))      # signed lateral offset from the gorge axis (m), + = viewpoint side
G_SIDE = np.sign(S_LAT)
PROJ = tx_map[gii, gjj] * (E - (gii - EXT)) + ty_map[gii, gjj] * (NN - (gjj - EXT))      # along-axis offset from the nearest axis point, < 0 behind the gorge head
USE_GORGE = False                                                      # the north-side reference shows no ravine
G_FW = 14.0                                                           # half-width of the flat gorge floor
G_FLOOR = lambda t: 4.0 - 10.0 * t + 22.0 * smooth((t - 0.78) / 0.22)          # floor height along the gorge, rises at the far end so the gorge dies out
G_TOP = lambda t: 34.0 - 24.0 * t                                             # height of the plateau around the gorge (viewpoint 30 m -> 10 m at the far end)

# ----------------------------------------------------------------------------- natural terrain
def sdf_rr(x, y, cx, cy, hx, hy, r):
    """signed distance (m) to a rounded rectangle, > 0 outside"""
    qx = np.abs(x - cx) - (hx - r)
    qy = np.abs(y - cy) - (hy - r)
    return np.hypot(np.maximum(qx, 0.0), np.maximum(qy, 0.0)) + np.minimum(np.maximum(qx, qy), 0.0) - r


OUT_X0, OUT_X1, OUT_Y0, OUT_Y1, OUT_RAD = -113.0, 125.0, -125.0, 125.0, 58.0     # outer wall: rounded rectangle read off the reference (the west side stands closer: the mine hill)
r0 = R_OUT + sdf_rr(E, NN, 0.5 * (OUT_X0 + OUT_X1), 0.0, 0.5 * (OUT_X1 - OUT_X0), 0.5 * (OUT_Y1 - OUT_Y0), OUT_RAD)   # "village radius": 125 on the outer wall, grows outwards
g_east = 1.0 - 0.68 * smooth((E + 30.0 * SC) / (230.0 * SC))    # meadow east of the road stays low
F_road = ndi.gaussian_filter(z_near * np.exp(-(dist_r / (250.0 * SC)) ** 2) * g_east, 28.0 * SC)   # blur: removes Voronoi seams between road legs
H = F_road.copy()
H += 40.0 * HS * smooth((-E - 380.0 * SC) / (260.0 * SC)) * (0.55 + 0.45 * smooth((-NN + 150.0 * SC) / (650.0 * SC)))   # west ridge
H += 16.0 * gauss2(-430.0 * SC, 390.0 * SC, 190.0 * SC, 240.0 * SC)    # far north-west mountain
# the hill behind the settlement: the concrete facility with the mine portal is built into its foot, right at the outer fence
FAC_ANG = math.radians(180.0)                                       # the building front faces the village along +x, long side along the fence
HILL_C = np.array([-155.0, -28.0])
H += 42.0 * gauss2(HILL_C[0], HILL_C[1], 55.0, 60.0) * smooth((r0 - 118.0) / 30.0)   # steep rock face right behind the building
H += 22.0 * HS * smooth((NN - 360.0 * SC) / (520.0 * SC))            # northern forest hills
H += 12.0 * HS * smooth((E - 430.0 * SC) / (320.0 * SC))             # far bank of the river
# the village lies in a basin; the hero comes from the hilly side: a highland that grows along the way to the tunnel and rises steeply on the west (uphill) side of the road
MEAD_C0, MEAD_A0 = np.array([205.0, -470.0]) * SC, np.array([200.0, 230.0]) * SC
meadow_pre = np.exp(-(((E - MEAD_C0[0]) / MEAD_A0[0]) ** 4 + ((NN - MEAD_C0[1]) / MEAD_A0[1]) ** 4))
# the serpentine climbs a HILLSIDE: one big slope rising from the village basin towards the tunnel, the switchbacks are cut across it
_ry = -road_poly[:, 1]                                                 # how far south each road point is
_order = np.argsort(_ry)
_prof_y = _ry[_order]; _prof_z = np.maximum.accumulate(road_z[_order])
_prof_y = np.concatenate([[0.0, _prof_y[0]], _prof_y, [_prof_y[-1] + 120.0]])
_prof_z = np.concatenate([[0.0, _prof_z[0]], _prof_z, [_prof_z[-1] + 0.45 * 120.0]])
_ramp = ndi.gaussian_filter1d(np.interp(-NN[0, :], _prof_y, _prof_z), 12.0)          # height of the slope by distance south (same for every x)
_ramp = np.broadcast_to(_ramp[None, :], (N, N))
_Mw = smooth((110.0 - E) / 150.0)                                      # the east side (meadow, river) stays low
U_high = _ramp * _Mw * (0.55 + 0.45 * smooth((-NN - 150.0) / 120.0))
def _bn(sigma, amp):
    n_ = np.random.default_rng(int(sigma) + 99).standard_normal((N, N)).astype(np.float32)
    n_ = ndi.gaussian_filter(n_, sigma, mode="reflect"); n_ /= (n_.std() + 1e-6)
    return n_ * amp


H += U_high + (_bn(30, 9.0) + _bn(12, 3.5)) * smooth(U_high / 25.0) - F_road          # the slope replaces the old road-following ridge
H += 20.0 * smooth((r0 - 160.0) / 140.0) * (1.0 - np.clip(meadow_pre, 0, 1))              # gentle bowl around the village
# (no extra knoll: the rock block at the road end is built explicitly below)


def blurred_noise(sigma, amp):
    n = rng.standard_normal((N, N)).astype(np.float32)
    n = ndi.gaussian_filter(n, sigma, mode="reflect")
    n /= (n.std() + 1e-6)
    return n * amp


noise = blurred_noise(45, 7.0 * HS) + blurred_noise(17, 3.2 * HS) + blurred_noise(7, 1.2 * HS) + blurred_noise(2.5, 0.35 * HS)

# flat areas: village + ring + 60 m apron, meadow
flat_village = 1.0 - smooth((r0 - 262.0 * SC) / (110.0 * SC))
MEAD_C, MEAD_A = np.array([205.0, -470.0]) * SC, np.array([200.0, 230.0]) * SC
meadow = np.exp(-(((E - MEAD_C[0]) / MEAD_A[0]) ** 4 + ((NN - MEAD_C[1]) / MEAD_A[1]) ** 4))   # ~1 inside, ~0 outside
damp = np.clip(1.0 - 0.92 * flat_village - 0.8 * meadow, 0.05, 1.0)
H = H + noise * damp * smooth((dist_r - 6.0) / (75.0 * SC))        # no random relief right next to the road

# plateau around the gorge: the viewpoint ledge and the rim on the road side stand well above the gorge floor
shoulder = float(USE_GORGE) * smooth(1.0 - (dist_g - 14.0) / 50.0) * smooth((S_LAT + 2.0) / 12.0) * (1.0 - smooth(-PROJ / 6.0))      # nothing is raised behind the gorge head (that is where the start hill is)      # only the viewpoint side is raised: the road side stays lower
H = H + shoulder * np.maximum(G_TOP(t_pix) - H, 0.0)

# facility: centre 120 m from the settlement centre, long side along the fence, back wall dug into the hill
FAC = np.array([-120.0, -28.0])                                      # reference: the concrete mine building on the west side, south of the centre line
hillmask = gauss2(HILL_C[0], HILL_C[1], 75.0, 80.0)
flat_eff = flat_village * (1.0 - hillmask * smooth((r0 - 126.0) / 8.0))   # the hill may rise right behind the fence

# village platform at 0 m
H = H * (1 - flat_eff) + 0.0 * flat_eff

# viewpoint platform: road end + ledge
vp_dist = np.hypot(E - VIEWPOINT[0], NN - VIEWPOINT[1])
vp_z = float(road_z[-1]) + 36.0                         # the slab stands 30 m above the road end: a jump off the edge is fatal
vp_pad = smooth(1.0 - (vp_dist - 8.0) / 10.0) * 0.0           # (platform is the rock block now)
H = H * (1 - vp_pad) + vp_z * vp_pad

# road cut/fill
cut = smooth((dist_r - 5.0) / (34.0 * SC))
H = z_near * (1 - cut) + H * cut
H = np.where(vp_pad > 0.5, H, H)  # keep platform as written above (road blend must not erase it)
H = H * (1 - vp_pad) + vp_z * vp_pad

# ----------------------------------------------------------------------------- footpath from the road end up to the ledge, abandoned north road
def raster_line(poly, zs):
    a = np.clip(np.round(poly[:, 0] + EXT).astype(int), 0, N - 1)
    b = np.clip(np.round(poly[:, 1] + EXT).astype(int), 0, N - 1)
    m = np.zeros((N, N), bool); zg = np.zeros((N, N), np.float32); tg = np.zeros((N, N), np.float32)
    m[a, b] = True; zg[a, b] = zs; tg[a, b] = np.linspace(0.0, 1.0, len(poly))
    d, (ia, ib) = ndi.distance_transform_edt(~m, return_indices=True)
    return d, zg[ia, ib], tg[ia, ib]


_rn = np.random.default_rng(4242).standard_normal((N, N)).astype(np.float32)
_rn = ndi.gaussian_filter(_rn, 4.0); _rn /= _rn.std()
_bu = (E - E_END[0]) * R_DIR[0] + (NN - E_END[1]) * R_DIR[1] + 0.9 * _rn      # u: into the rock, v: sideways
_bv = (E - E_END[0]) * R_LAT[0] + (NN - E_END[1]) * R_LAT[1]
block = smooth(_bu / 1.5) * (1.0 - smooth((_bu - 28.0) / 4.0)) * (1.0 - smooth((np.abs(_bv) - 14.0) / 4.0))
H = H + block * np.maximum(vp_z + 0.5 * _rn - H, 0.0)
# behind the slab the rock keeps climbing (a mountain, steeper than anyone can walk), so the slab is a ledge on its front and the only way down is the boulder stairs
back_hill = smooth((_bu - 27.0) / 2.0) * np.exp(-(((_bu - 28.0) / 90.0) ** 2 + (_bv / 50.0) ** 2))
H = H + back_hill * np.maximum(vp_z + 26.0 * smooth((_bu - 28.0) / 20.0) + 0.5 * _rn - H, 0.0)
TRAIL_CTRL = np.array([rloc(-14.0, 22.0), rloc(-4.0, 19.0), rloc(6.0, 16.5), rloc(14.0, 14.0), rloc(19.0, 10.0), rloc(19.0, 5.0)])      # short ramp up the south side of the slab (boulder stairs sit on it)
trail_poly, trail_s = resample(catmull(TRAIL_CTRL, 1.0), 1.0)
trail_z = float(road_z[-1]) + (vp_z - float(road_z[-1])) * (trail_s / trail_s[-1])
dist_t, z_trail, _t_trail = raster_line(trail_poly, trail_z)
ct = smooth((dist_t - 2.4) / 5.0)
H = z_trail * (1 - ct) + H * ct

# intro walk-in: a gentle track from the forest on the hillside south-west of the slab, down onto the slab (the opening scene walks the Redneck along it)
ENTRY_CTRL = np.array([rloc(54.0, 46.0), rloc(42.0, 36.0), rloc(31.0, 24.0), rloc(21.0, 13.0), rloc(12.0, 4.0)])
entry_poly, entry_s = resample(catmull(ENTRY_CTRL, 1.0), 1.0)
_ei = np.clip(np.round(entry_poly[:, 0] + EXT).astype(int), 0, N - 1); _ej = np.clip(np.round(entry_poly[:, 1] + EXT).astype(int), 0, N - 1)
_h_start = float(ndi.gaussian_filter1d(H[_ei, _ej], 6.0, mode="nearest")[0])
entry_z = _h_start + (vp_z - _h_start) * smooth(entry_s / entry_s[-1])
dist_e, z_entry, _t_entry = raster_line(entry_poly, entry_z)
ce = smooth((dist_e - 2.8) / 6.0)
H = z_entry * (1 - ce) + H * ce

OLD_CTRL = np.array([[0.0, R_OUT + 2.0], [2.0, 145.0], [-6.0, 168.0], [-14.0, 195.0], [-12.0, 225.0], [-2.0, 250.0], [8.0, 280.0], [14.0, 310.0]])
old_poly, old_s = resample(catmull(OLD_CTRL, 2.0), 1.0)
_oi = np.clip(np.round(old_poly[:, 0] + EXT).astype(int), 0, N - 1); _oj = np.clip(np.round(old_poly[:, 1] + EXT).astype(int), 0, N - 1)
old_z = ndi.gaussian_filter1d(H[_oi, _oj], 10.0, mode="nearest")
dist_o, z_old, t_old = raster_line(old_poly, old_z)
co = smooth((dist_o - 3.0) / 9.0)
H = z_old * (1 - co) + H * co
H = H * (1 - vp_pad) + vp_z * vp_pad

# ----------------------------------------------------------------------------- river
def river_x(y):
    return 205.0 + 16.0 * np.sin(y / 85.0) + 7.0 * np.sin(y / 35.0 + 1.3)


ry = np.arange(-EXT, EXT + 1, 2.0)
river_poly = np.stack([river_x(ry), ry], axis=1)
rvi = np.clip(np.round(river_poly[:, 0] + EXT).astype(int), 0, N - 1)
rvj = np.clip(np.round(river_poly[:, 1] + EXT).astype(int), 0, N - 1)
river_mask = np.zeros((N, N), bool)
river_mask[rvi, rvj] = True
dist_v = ndi.distance_transform_edt(~river_mask)
# river bed: smoothed terrain profile along the river minus 2 m, the water follows the SAME profile (no floating water)
_, (vii, vjj) = ndi.distance_transform_edt(~river_mask, return_indices=True)
bed_by_j = np.zeros(N, np.float32)
_h_on_river = np.array([H[rvi[k], rvj[k]] for k in range(len(ry))], np.float32)
_bed = ndi.gaussian_filter1d(_h_on_river, 15.0, mode="nearest") - 2.0
bed_by_j[:] = np.interp(np.arange(N) - EXT, ry, _bed)
bed_grid = bed_by_j[vjj]
wch = 1.0 - smooth((dist_v - 4.0) / 8.0)
H = H * (1 - wch) + bed_grid * wch
H = ndi.gaussian_filter(H, 1.2)                                     # remove stair-steps from the raster distances
# re-impose flat village after the blur
H = H * (1 - flat_eff * 0.98) + 0.0 * flat_eff * 0.98

# ----------------------------------------------------------------------------- gorge under the viewpoint cliff
rock_n = blurred_noise(3.0, 1.0)
dg_eff = dist_g + 2.6 * rock_n
wall_w = 40.0 * smooth((dg_eff - G_FW) / 5.0)                      # viewpoint side: sheer cliff (about 75 degrees)
wall_e = 40.0 * smooth((dg_eff - G_FW) / 30.0)                     # road side: steep rocky slope
behind_f = smooth(-PROJ / 6.0) * (1.0 - smooth((t_pix - 0.03) / 0.04))                            # behind the head: sheer on both sides (portal wall)
wall_side = np.where(G_SIDE > 0, wall_w, wall_e)
wall = wall_side * (1.0 - behind_f) + wall_w * behind_f + 40.0 * (1.0 - smooth((dist_r - 12.0) / 10.0))   # keeps away from the road
carved = np.minimum(H, G_FLOOR(t_pix) + wall)
H = np.where((dist_g < 50.0) & USE_GORGE, carved, H)
H = ndi.gaussian_filter(H, 0.5)

# ----------------------------------------------------------------------------- derived
gy, gx = np.gradient(H)                    # per metre
slope = np.degrees(np.arctan(np.hypot(gx, gy)))

hm16 = np.clip(H * 128.0 + 32768.0, 0, 65535).astype(np.uint16)
# arr[row=east idx, col=north idx] is exactly what UE expects (col -> X north, row -> Y east)
cv2.imwrite(os.path.join(OUT, "RoadToDawn_heightmap_1009.png"), hm16)


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
for _k in range(len(rp)):                                      # the last metres touch the foot of the rock face: use the designed road height there
    if ROAD_LEN - float(road_s[_k]) < 8.0:
        rp[_k][2] = round(float(road_z[_k]), 2)
layout["road"] = {"width": 7.0, "points": rp, "length_m": round(ROAD_LEN, 1)}

# ring road + village streets
def circle_pts(cx, cy, r, step=6.0, a0=0.0, a1=360.0):
    n = max(8, int(2 * math.pi * r * (a1 - a0) / 360.0 / step))
    ang = np.linspace(math.radians(a0), math.radians(a1), n, endpoint=False)
    return np.stack([cx + r * np.cos(ang), cy + r * np.sin(ang)], axis=1)


def rrect_poly(x0, x1, y0, y1, r, step=1.0):
    """closed counter-clockwise polyline of a rounded rectangle, starting at the middle of the south side"""
    cx0, cx1, cy0, cy1 = x0 + r, x1 - r, y0 + r, y1 - r
    pts = [(0.5 * (x0 + x1), y0)]
    for (ccx, ccy, a0) in ((cx1, cy0, -90.0), (cx1, cy1, 0.0), (cx0, cy1, 90.0), (cx0, cy0, 180.0)):
        n = max(6, int(math.pi * r / 2.0 / step))
        for a in np.linspace(a0, a0 + 90.0, n):
            pts.append((ccx + r * math.cos(math.radians(a)), ccy + r * math.sin(math.radians(a))))
    pts.append((0.5 * (x0 + x1), y0))
    return np.array(pts)


# inner wall and ring lane, measured on the reference (calibrated image -> plan)
IN_X0, IN_X1, IN_Y0, IN_Y1, IN_RAD = -93.0, 99.0, -88.0, 94.0, 38.0
IN_Y_GATE, IN_N_GATE = IN_Y0, IN_Y1
RG_X0, RG_X1, RG_Y0, RG_Y1, RG_RAD = -103.0, 112.0, -106.5, 109.5, 46.0     # centre line of the lane between the two walls
ring_pts = rrect_poly(RG_X0, RG_X1, RG_Y0, RG_Y1, RG_RAD, 4.0)
CROSS_Y = float(img2plan([(603, 790)])[0][1])
streets = []
streets.append({"name": "ring", "width": 8.0, "pts": [[round(float(x), 2), round(float(y), 2)] for x, y in ring_pts]})
streets.append({"name": "gate_approach", "width": 7.0, "pts": [[0, -(R_OUT + 2.0)], [0, IN_Y_GATE]]})
streets.append({"name": "main_street", "width": 7.0, "pts": [[0, IN_Y_GATE], [0, IN_Y1 - 4.0]]})
streets.append({"name": "north_gate_link", "width": 6.0, "pts": [[0, RG_Y1], [0, R_OUT + 2.0]]})
streets.append({"name": "cross_street", "width": 6.0, "pts": [[IN_X0 + 6.0, CROSS_Y], [IN_X1 - 6.0, CROSS_Y]]})
for s in streets:
    pts = np.array(s["pts"], dtype=float)
    dense, _ = resample(pts, 3.0)
    s["pts"] = [[round(float(x), 2), round(float(y), 2), round(sample(x, y), 2)] for x, y in dense]
layout["streets"] = streets

# walls: rounded rectangles, ~8 m pieces
def wall_from_poly(poly, h, thick, kind, skip):
    poly, _ = resample(poly, 8.0)
    out = []
    for k in range(len(poly) - 1):
        p0, p1 = poly[k], poly[k + 1]
        mx, my = 0.5 * (p0[0] + p1[0]), 0.5 * (p0[1] + p1[1])
        if skip(mx, my):
            continue
        ln = math.hypot(p1[0] - p0[0], p1[1] - p0[1]) + 0.4
        yaw = math.degrees(math.atan2(p1[1] - p0[1], p1[0] - p0[0]))
        out.append({"x": round(float(mx), 2), "y": round(float(my), 2), "z": round(sample(mx, my), 2), "len": round(ln, 2),
                    "yaw": round(yaw, 2), "h": h, "t": thick, "kind": kind})
    return out


MINE_HALF = 34.0                                          # the mine building closes the outer fence over this half-length (along y)
outer_poly = rrect_poly(OUT_X0, OUT_X1, OUT_Y0, OUT_Y1, OUT_RAD, 1.0)
inner_poly = rrect_poly(IN_X0, IN_X1, IN_Y0, IN_Y1, IN_RAD, 1.0)
walls = wall_from_poly(outer_poly, 6.5, 0.7, "outer_fence",
                       lambda mx, my: abs(mx) < 7.0 and abs(my) > 100.0 or (mx < -95.0 and abs(my - FAC[1]) < MINE_HALF))   # both outer gates, mine
walls += wall_from_poly(inner_poly, 5.0, 1.2, "inner_wall", lambda mx, my: abs(mx) < 6.0 and my < 0.0)                       # south gate only: the north inner wall is closed
layout["walls"] = walls

gates = [
    {"name": "Outer gate", "x": 0.0, "y": -R_OUT, "w": 14.0, "h": 7.5, "yaw": 0.0, "open": True},
    {"name": "Inner gate", "x": 0.0, "y": IN_Y_GATE, "w": 12.0, "h": 6.0, "yaw": 0.0, "open": True},
    {"name": "Outer gate N (closed)", "x": 0.0, "y": R_OUT, "w": 14.0, "h": 7.5, "yaw": 0.0, "open": False},
]
for g in gates:
    g["z"] = round(sample(g["x"], g["y"]), 2)
layout["gates"] = gates

towers = []
for name, x, y in (("Outer tower W", -12.0, -R_OUT + 1.0), ("Outer tower E", 12.0, -R_OUT + 1.0),
                   ("Outer tower N W", -12.0, R_OUT - 1.0), ("Outer tower N E", 12.0, R_OUT - 1.0),
                   ("Inner tower W", -10.0, IN_Y_GATE + 1.0), ("Inner tower E", 10.0, IN_Y_GATE + 1.0)):
    towers.append({"name": name, "x": x, "y": y, "z": round(sample(x, y), 2), "size": 4.0, "h": 11.0 if "Outer" in name else 8.0})
layout["towers"] = towers

# buildings: every position is read off the reference (no procedural fill)
SPECIAL_IMG = {"gas": (400, 597), "shop": (745, 592), "tower": (395, 822), "kiosk": (303, 578)}
sp = {k: img2plan([v])[0] for k, v in SPECIAL_IMG.items()}
specials = [
    {"name": "Shop", "x": sp["shop"][0], "y": sp["shop"][1], "w": 26.0, "d": 16.0, "h": 6.5, "yaw": 0.0, "kind": "shop"},
    {"name": "Gas station", "x": sp["gas"][0], "y": sp["gas"][1], "w": 24.0, "d": 14.0, "h": 5.5, "yaw": 0.0, "kind": "gas"},
    {"name": "Water tower", "x": sp["tower"][0], "y": sp["tower"][1], "w": 7.0, "d": 7.0, "h": 24.0, "yaw": 0.0, "kind": "tower"},
    {"name": "Kiosk", "x": sp["kiosk"][0], "y": sp["kiosk"][1], "w": 6.0, "d": 5.0, "h": 3.5, "yaw": 0.0, "kind": "house"},
]
bld = list(specials)
HOUSES_IMG = [(286, 637), (271, 682), (333, 684), (389, 718), (468, 668), (540, 610), (590, 693), (270, 742), (523, 733),
              (878, 605), (850, 643), (876, 717), (920, 670), (760, 683), (733, 758), (288, 808), (342, 835), (460, 858),
              (545, 842), (697, 830), (788, 840), (860, 775)]
for (x, y) in img2plan(HOUSES_IMG):
    bld.append({"name": "House", "x": float(x), "y": float(y), "w": float(rng.uniform(8, 12)), "d": float(rng.uniform(7, 9)),
                "h": float(rng.uniform(5.0, 7.0)), "yaw": float(rng.choice([0, 0, 90, 0, 90])), "kind": "house"})
for b in bld:
    for k in ("x", "y", "w", "d", "h", "yaw"):
        b[k] = round(float(b[k]), 2)
    b["z"] = round(sample(b["x"], b["y"]), 2)
layout["buildings"] = bld

# containers by the inner wall
layout["containers"] = [
    {"x": -118.0, "y": -70.0, "z": round(sample(-118.0, -70.0), 2), "w": 6.0, "d": 2.5, "h": 2.6, "yaw": 90.0},
    {"x": -118.0, "y": -76.0, "z": round(sample(-118.0, -76.0), 2), "w": 6.0, "d": 2.5, "h": 2.6, "yaw": 90.0},
]

# facility with the old mine portal: boxes in a local frame (a = outwards from the settlement into the hill, b = along the fence)
mine_boxes = []
mth = FAC_ANG


def mloc(a, b):
    return (FAC[0] + a * math.cos(mth) - b * math.sin(mth), FAC[1] + a * math.sin(mth) + b * math.cos(mth))


def mbox(a, b, zb, w, d, h, yaw_off=0.0, ground=False):
    x, y = mloc(a, b)
    if ground:
        zb = sample(x, y) + zb
    mine_boxes.append({"x": round(x, 2), "y": round(y, 2), "z": round(zb, 2), "w": w, "d": d, "h": h, "yaw": round(math.degrees(mth) + yaw_off, 2)})


PZ = 0.0
TW = 5.5                                                              # tunnel width
mbox(0.0, -16.5, 0.0, 16.0, 26.0, 8.5)                                # building: left block
mbox(0.0, 16.5, 0.0, 16.0, 26.0, 8.5)                                 # building: right block
mbox(0.0, 0.0, 5.8, 16.0, TW + 1.0, 3.2)                              # lintel above the portal
mbox(2.0, 0.0, 9.0, 12.0, 61.0, 3.0)                                  # upper floor (window band)
mbox(2.0, 0.0, 12.0, 12.6, 61.6, 0.5)                                 # roof slab
mbox(4.0, -20.0, 12.5, 0.5, 0.5, 6.0)                                  # radio mast
mbox(4.0, 20.0, 12.5, 2.0, 0.2, 1.8)                                   # dish
mbox(15.0, 0.0, -0.4, 14.0, TW + 1.2, 0.4)                            # tunnel floor (inside the hill)
mbox(15.0, TW / 2 + 0.3, 0.0, 14.0, 0.6, 5.0)                         # tunnel walls
mbox(15.0, -TW / 2 - 0.3, 0.0, 14.0, 0.6, 5.0)
mbox(15.0, 0.0, 5.0, 14.0, TW + 1.2, 0.6)                             # tunnel roof
mbox(22.0, 0.0, 0.0, 0.6, TW + 1.2, 5.6)                              # tunnel end wall
mbox(-9.6, TW / 2 + 1.6, 0.0, 3.2, 0.5, 5.4, 90.0)                    # door leaves swung open
mbox(-9.6, -TW / 2 - 1.6, 0.0, 3.2, 0.5, 5.4, 90.0)
mbox(-9.0, 12.0, 3.0, 2.2, 3.4, 0.3)                                   # canopy over the side door
for a in range(-34, 20, 4):                                           # rails (4 m pieces, follow the ground in front of the building)
    mbox(a + 2.0, 0.55, 0.02, 4.0, 0.12, 0.1, ground=(a < -8))
    mbox(a + 2.0, -0.55, 0.02, 4.0, 0.12, 0.1, ground=(a < -8))
mbox(-18.0, 0.0, 0.15, 2.2, 1.3, 1.0, ground=True)                    # ore cart
mbox(-14.0, -36.0, 0.0, 6.0, 5.0, 3.0, ground=True)                   # tool shed
layout["mine"] = {"portal": [round(float(mloc(-8.0, 0.0)[0]), 2), round(float(mloc(-8.0, 0.0)[1]), 2), 0.0], "tunnel_yaw_math_deg": round(math.degrees(mth), 1),
                  "boxes": mine_boxes}

# collapsed tunnel at the end of the road: portal frame in the rock face + rockfall that buries it
tdir = -R_DIR                                      # out of the wall, back along the road
lat = np.array([-tdir[1], tdir[0]])
floor0 = float(road_z[-1])
PORTAL_O = E_END + R_DIR * 0.8
yaw_a = math.degrees(math.atan2(tdir[1], tdir[0]))


def ploc(a, b):
    p = PORTAL_O + tdir * a + lat * b
    return float(p[0]), float(p[1])


portal_boxes = []
PW = 5.0                                              # half width of the opening: 10 m, two trucks side by side
PH = 7.5                                              # height of the opening


def pbox(a, b, zb, w, d, h):
    x, y = ploc(a, b)
    portal_boxes.append({"x": round(x, 2), "y": round(y, 2), "z": round(zb, 2), "w": w, "d": d, "h": h, "yaw": round(yaw_a, 2)})


for sb in (-1, 1):
    pbox(-0.3, sb * (PW + 1.4), floor0 - 1.5, 2.4, 2.8, PH + 1.5 + 1.2)     # stone pillars (sunk 1.5 m into the ground)
    pbox(-0.1, sb * (PW - 0.7), floor0 + PH - 1.2, 2.0, 1.6, 1.3)           # corner stones: the arch is only hinted
pbox(-0.3, 0.0, floor0 + PH, 2.4, 2 * (PW + 2.8), 1.6)                      # lintel
pbox(-0.3, 0.0, floor0 + PH + 1.6, 2.4, 3.0, 0.9)                           # keystone
pbox(-0.6, 0.0, floor0, 0.4, 2 * PW, PH - 0.5)                              # dark back wall of the opening (behind the rubble)
layout["tunnel_portal"] = {"x": round(PORTAL_O[0], 2), "y": round(PORTAL_O[1], 2), "z": round(floor0, 2), "yaw_math_deg": round(yaw_a, 2), "boxes": portal_boxes}

rf = np.random.default_rng(777)
rocks = []


def add_rock(x, y, zc, s, shape):
    rocks.append({"x": round(x, 2), "y": round(y, 2), "z": round(zc, 2), "sx": round(s * float(rf.uniform(0.75, 1.25)), 2),
                  "sy": round(s * float(rf.uniform(0.75, 1.25)), 2), "sz": round(s * float(rf.uniform(0.6, 1.05)), 2),
                  "yaw": round(float(rf.uniform(0, 360)), 1), "pitch": round(float(rf.uniform(-25, 25)), 1),
                  "roll": round(float(rf.uniform(-25, 25)), 1), "shape": shape})


for k in range(115):                                 # the rubble pile that buries the portal
    a = min(float(rf.exponential(5.0)), 20.0)
    b = float(np.clip(rf.normal(0.0, 4.6), -11.0, 11.0))
    env = 8.5 * (1.0 - a / 22.0) * max(0.0, 1.0 - (b / 13.0) ** 2)
    s = float(rf.uniform(2.6, 7.0)) * (1.0 - 0.35 * a / 20.0)
    x, y = ploc(a + 0.8, b)
    zc = floor0 + s * 0.30 + env * float(rf.uniform(0.15, 0.85))
    add_rock(x, y, zc, s, "cube" if rf.random() < 0.6 else "sphere")
for k in range(12):                                  # smaller debris further out along the gorge floor
    a = float(rf.uniform(14.0, 24.0)); b = float(rf.uniform(-7.0, 7.0)); s = float(rf.uniform(1.0, 2.4))
    x, y = ploc(a, b)
    add_rock(x, y, floor0 + s * 0.3, s, "cube" if rf.random() < 0.5 else "sphere")
cnt = 0
for _ in range(300):                                 # fallen blocks on the slab: behind and beside the hero, away from the view and the path
    if cnt >= 7:
        break
    u_ = float(rf.uniform(8.0, 24.0)); v_ = float(rf.uniform(-9.0, 9.0))
    p = rloc(u_, v_)
    x, y = float(p[0]), float(p[1])
    i, j = int(round(x + EXT)), int(round(y + EXT))
    if dist_t[i, j] < 3.5 or math.hypot(x - VIEWPOINT[0], y - VIEWPOINT[1]) < 4.5 or v_ < -3.0 and u_ < 16.0:
        continue
    s_ = float(rf.uniform(1.8, 3.6))
    add_rock(x, y, sample(x, y) + s_ * 0.25, s_, "cube" if rf.random() < 0.6 else "sphere")
    cnt += 1
# boulder stairs on the ramp beside the slab: flat stepping stones along the line + big boulders on both sides
_sl = len(trail_poly)
for k in range(1, _sl - 1, 2):
    x, y = float(trail_poly[k][0]), float(trail_poly[k][1])
    tx, ty = trail_poly[min(k + 1, _sl - 1)] - trail_poly[k - 1]
    tn = math.hypot(tx, ty) + 1e-9
    nx, ny = -ty / tn, tx / tn
    rocks.append({"x": round(x, 2), "y": round(y, 2), "z": round(sample(x, y) + 0.12, 2), "sx": round(float(rf.uniform(1.6, 2.2)), 2),
                  "sy": round(float(rf.uniform(2.4, 3.0)), 2), "sz": 0.9, "yaw": round(math.degrees(math.atan2(ty, tx)) + float(rf.uniform(-8, 8)), 1),
                  "pitch": round(float(rf.uniform(-4, 4)), 1), "roll": round(float(rf.uniform(-4, 4)), 1), "shape": "cube"})
    for side in (-1, 1):
        if rf.random() < 0.85:
            off = side * float(rf.uniform(3.2, 4.8))
            px, py = x + nx * off, y + ny * off
            s_ = float(rf.uniform(1.4, 2.8))
            add_rock(px, py, sample(px, py) + s_ * 0.3, s_, "cube" if rf.random() < 0.5 else "sphere")
# low rim of boulders along the sheer edges of the slab (front face, north edge, back edge) so nobody walks off by accident; the stairs side stays open
def rim(u_, v_):
    p = rloc(u_, v_)
    x, y = float(p[0]), float(p[1])
    rocks.append({"x": round(x, 2), "y": round(y, 2), "z": round(sample(x, y) + 0.45, 2), "sx": round(float(rf.uniform(2.6, 3.4)), 2),
                  "sy": round(float(rf.uniform(2.2, 2.8)), 2), "sz": round(float(rf.uniform(1.1, 1.5)), 2), "yaw": round(float(rf.uniform(0, 360)), 1),
                  "pitch": round(float(rf.uniform(-5, 5)), 1), "roll": round(float(rf.uniform(-5, 5)), 1), "shape": "cube"})


# (no rim any more: the edges are sheer and a jump off the slab is meant to be fatal)
# keep the intro walk-in clear: the opening scene walks the Redneck along entry_poly with plain movement input, and the track
# crosses the top of the boulder stairs and the fallen blocks on the slab - every rock whose footprint reaches into the corridor is dropped
ENTRY_CLEAR = 1.8                                    # half-width of the free corridor (m), well over the capsule radius


def _blocks_entry(r):
    i, j = int(round(r["x"] + EXT)), int(round(r["y"] + EXT))
    if not (0 <= i < N and 0 <= j < N):
        return False
    return float(dist_e[i, j]) - 0.5 * math.hypot(r["sx"], r["sy"]) < ENTRY_CLEAR


_n_rocks = len(rocks)
rocks = [r for r in rocks if not _blocks_entry(r)]
print("rocks removed from the intro track: %d of %d" % (_n_rocks - len(rocks), _n_rocks))
layout["rockfall"] = rocks

# footpath (ledge -> road end) and the abandoned road behind the closed north gate
layout["trail"] = None                          # no ribbon: the way down is the boulder stairs
layout["old_road"] = {"width": 5.0, "points": [[round(float(x), 2), round(float(y), 2), round(sample(x, y), 2)] for x, y in old_poly]}

# creek in the gorge (water follows the gorge floor)
cr = []
for k in range(0, len(g_dense), 3):
    t = g_t[k]
    if 0.03 < t < 0.8:
        cr.append([round(float(g_dense[k, 0]), 2), round(float(g_dense[k, 1]), 2), round(float(G_FLOOR(t)) + 0.45, 2)])
layout["creek"] = None                      # the creek was removed on request (no water in the gorge for now)

# lamp posts along the ring road
lamps = []
_lamp_line = rrect_poly(OUT_X0 + 4.0, OUT_X1 - 4.0, OUT_Y0 + 4.0, OUT_Y1 - 4.0, OUT_RAD - 4.0, 1.0)     # inside the outer fence, as on the reference
_lamp_pts, _ = resample(_lamp_line, 26.0)
for x, y in _lamp_pts[:-1]:
    if (abs(x) < 12.0 and abs(y) > 100.0) or (x < -95.0 and abs(y - FAC[1]) < MINE_HALF):
        continue
    lamps.append({"x": round(float(x), 2), "y": round(float(y), 2), "z": round(sample(x, y), 2)})
layout["lamps"] = lamps

# river (water ribbon): water level = smoothed bed profile + 1.2 (same profile that carved the channel)
wl = []
for x, y in river_poly[::2]:
    wl.append([round(float(x), 2), round(float(y), 2), round(float(np.interp(y, ry, _bed)) + 1.2, 2)])
layout["river"] = {"width": 14.0, "points": wl}

# player start + beacons
vp = [float(VIEWPOINT[0]), float(VIEWPOINT[1])]
face = math.degrees(math.atan2(0.0 - vp[1], 0.0 - vp[0]))
vp = [float(VIEWPOINT[0]), float(VIEWPOINT[1])]
layout["player_start"] = {"x": vp[0], "y": vp[1], "z": round(sample(*vp) + 0.2, 2), "yaw_math_deg": round(face, 2)}
_ew, _ = resample(entry_poly, 3.0)
layout["intro"] = {"waypoints": [[round(float(x), 2), round(float(y), 2), round(sample(float(x), float(y)), 2)] for x, y in _ew[-10:]],
                   "note": "opening scene: the Redneck walks these points in order (the last one is the PlayerStart spot)"}
_bp = rloc(14.0, -5.0)
layout["binoculars"] = {"x": float(_bp[0]), "y": float(_bp[1]), "z": round(sample(float(_bp[0]), float(_bp[1])) + 0.1, 2)}
trap_xy = (0.5, -(R_OUT + 50.0))
layout["bear_trap"] = {"x": trap_xy[0], "y": trap_xy[1], "z": round(sample(*trap_xy) + 0.1, 2)}
layout["navmesh"] = {"x": -20.0, "y": -150.0, "w": 420.0, "d": 700.0, "z": 0.0}

# trees
STEP = 7.5
trees = []
for gx in np.arange(-EXT + 6, EXT - 6, STEP):
    for gy_ in np.arange(-EXT + 6, EXT - 6, STEP):
        x = gx + rng.uniform(-4.5, 4.5)
        y = gy_ + rng.uniform(-4.5, 4.5)
        i, j = int(round(x + EXT)), int(round(y + EXT))
        if not (0 <= i < N and 0 <= j < N):
            continue
        if r0[i, j] < R_OUT + 12.0:
            continue
        if dist_r[i, j] < 6.5 or dist_v[i, j] < 14.0 or (USE_GORGE and dist_g[i, j] < 9.0):
            continue
        if math.hypot(x - FAC[0], y - FAC[1]) < 30.0:
            continue
        if dist_e[i, j] < 3.8 or dist_t[i, j] < 4.0 or dist_o[i, j] < 5.0 * (1.0 - float(t_old[i, j])) ** 1.2:   # footpath free; the abandoned road is slowly overgrown
            continue
        vx, vy = x - vp[0], y - vp[1]
        if math.hypot(vx, vy) < 90.0:                                   # keep the view from the cliff open (cone towards the gate)
            ang = math.degrees(math.atan2(vy, vx) - math.atan2(0.0 - vp[1], 0.0 - vp[0]))
            ang = (ang + 180.0) % 360.0 - 180.0
            if abs(ang) < 50.0:
                continue
        if math.hypot(x - vp[0], y - vp[1]) < 12.0:
            continue
        if slope[i, j] > 36.0:
            continue
        near_village = 1.0 - smooth((r0[i, j] - (R_OUT + 75.0)) / 60.0)       # reference: open meadow around the village, forest starts further out
        north_open = 1.0 - 0.9 * float(smooth((y - 60.0) / 60.0)) * (1.0 - float(smooth((abs(x) - 90.0) / 60.0)))   # forest comes right up to the north wall
        near_village *= north_open
        pm = 0.9 - 0.86 * near_village
        if meadow[i, j] > 0.35:
            pm = min(pm, 0.04)
        pm *= 1.0 - 0.45 * smooth((H[i, j] - 70.0) / 50.0)
        if rng.random() > pm:
            continue
        z = float(H[i, j])
        trees.append([round(float(x), 1), round(float(y), 1), round(z, 2), round(float(rng.uniform(0.6, 1.1)), 2)])
if len(trees) > 22000:
    idx = rng.choice(len(trees), 22000, replace=False)
    trees = [trees[k] for k in sorted(idx)]
layout["trees"] = trees

# stats
grade = np.abs(np.diff(road_z)) / 2.0
layout["stats"] = {
    "road_length_m": round(ROAD_LEN, 1), "road_max_grade_pct": round(float(grade.max() * 100), 1),
    "road_avg_grade_pct": round(float(road_z[-1] / ROAD_LEN * 100), 1), "viewpoint_height_m": round(vp_z, 1),
    "height_min_m": round(float(H.min()), 1), "height_max_m": round(float(H.max()), 1), "trees": len(trees),
    "buildings": len(bld), "wall_segments": len(walls),
    "gate_to_viewpoint_straight_m": round(math.hypot(vp[0], vp[1] + R_OUT), 1),
    "trail_length_m": round(float(trail_s[-1]), 1), "rocks": len(rocks), "portal_xy": [round(float(PORTAL_O[0]), 1), round(float(PORTAL_O[1]), 1)],
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
def plan_image(scale=1.0):
    Hs = H.T[::-1]                      # rows = north (top = north), cols = east
    hh = (Hs - Hs.min()) / (Hs.max() - Hs.min())
    ls = ndi.gaussian_filter(Hs, 1.0)
    gy2, gx2 = np.gradient(ls)
    shade = np.clip(0.55 + (-gx2 * 0.8 + gy2 * 0.6) * 0.9, 0.2, 1.1)   # light from the west/south-west
    base = np.stack([0.20 + 0.45 * hh, 0.34 + 0.40 * hh, 0.18 + 0.25 * hh], axis=2)
    mead = meadow.T[::-1]
    base = base * (1 - mead[..., None] * 0.5) + np.array([0.62, 0.55, 0.28]) * (mead[..., None] * 0.5)
    rk = rock.T[::-1]
    base = base * (1 - rk[..., None] * 0.6) + np.array([0.5, 0.48, 0.46]) * (rk[..., None] * 0.6)
    img = np.clip(base * shade[..., None], 0, 1)
    return (img * 255).astype(np.uint8)


prev = plan_image()
h2, w2 = prev.shape[:2]


def to_px(x, y):
    return int(round(x + EXT)), int(round(EXT - y))


def draw_poly(img, pts, color, th):
    p = np.array([to_px(x, y) for x, y in pts], np.int32).reshape(-1, 1, 2)
    cv2.polylines(img, [p], False, color, th, cv2.LINE_AA)


bgr = cv2.cvtColor(prev, cv2.COLOR_RGB2BGR)
draw_poly(bgr, [(p[0], p[1]) for p in layout["river"]["points"]], (200, 140, 60), 7)
draw_poly(bgr, [(p[0], p[1]) for p in rp], (150, 190, 215), 4)
for s in streets:
    draw_poly(bgr, [(p[0], p[1]) for p in s["pts"]], (150, 190, 215), 2)
draw_poly(bgr, outer_poly, (60, 60, 200), 2)
draw_poly(bgr, inner_poly, (60, 60, 200), 2)
cv2.circle(bgr, to_px(*layout["mine"]["portal"][:2]), 4, (0, 0, 0), -1)
cv2.putText(bgr, "Mine portal", (to_px(*layout["mine"]["portal"][:2])[0] - 40, to_px(*layout["mine"]["portal"][:2])[1] + 16), cv2.FONT_HERSHEY_SIMPLEX, 0.45, (255, 255, 255), 1, cv2.LINE_AA)
for b in bld:
    cv2.circle(bgr, to_px(b["x"], b["y"]), 3, (40, 40, 40), -1)
draw_poly(bgr, [(p[0], p[1]) for p in trail_poly], (0, 200, 255), 3)
draw_poly(bgr, [(p[0], p[1]) for p in layout["old_road"]["points"]], (120, 160, 190), 2)
cv2.circle(bgr, to_px(float(PORTAL_O[0]), float(PORTAL_O[1])), 5, (0, 0, 255), -1)
cv2.putText(bgr, "Collapsed tunnel", (to_px(float(PORTAL_O[0]), float(PORTAL_O[1]))[0] + 10, to_px(float(PORTAL_O[0]), float(PORTAL_O[1]))[1] - 12), cv2.FONT_HERSHEY_SIMPLEX, 0.45, (255, 255, 255), 1, cv2.LINE_AA)
cv2.putText(bgr, "Closed gates N", (to_px(-35, R_OUT + 14)[0], to_px(-35, R_OUT + 14)[1]), cv2.FONT_HERSHEY_SIMPLEX, 0.45, (255, 255, 255), 1, cv2.LINE_AA)
cv2.circle(bgr, to_px(*vp), 6, (0, 255, 255), -1)
cv2.putText(bgr, "Redneck start", (to_px(*vp)[0] + 10, to_px(*vp)[1] + 4), cv2.FONT_HERSHEY_SIMPLEX, 0.5, (0, 255, 255), 1, cv2.LINE_AA)
cv2.putText(bgr, "Settlement", (to_px(-20, 5)[0], to_px(-20, 5)[1]), cv2.FONT_HERSHEY_SIMPLEX, 0.5, (255, 255, 255), 1, cv2.LINE_AA)
cv2.putText(bgr, "Facility", (to_px(FAC[0] - 30, FAC[1] + 45)[0], to_px(FAC[0] - 30, FAC[1] + 45)[1]), cv2.FONT_HERSHEY_SIMPLEX, 0.45, (255, 255, 255), 1, cv2.LINE_AA)
cv2.putText(bgr, "River", (to_px(river_x(300) + 14, 300)[0], to_px(0, 300)[1]), cv2.FONT_HERSHEY_SIMPLEX, 0.5, (255, 200, 120), 1, cv2.LINE_AA)
cv2.putText(bgr, "N", (10, 22), cv2.FONT_HERSHEY_SIMPLEX, 0.7, (255, 255, 255), 2, cv2.LINE_AA)
for k in range(-500, 501, 100):
    px, py = to_px(k, -EXT)
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
    poly_img(outer_poly, (0, 0, 255), 3)
    poly_img(inner_poly, (0, 128, 255), 3)
    poly_img(ring_pts, (255, 0, 255), 2)
    poly_img([(p[0], p[1]) for p in layout["river"]["points"] if -165 < p[1] < 210], (255, 128, 0), 4)
    for b in bld:
        q = plan2img([[b["x"], b["y"]]])[0]
        cv2.circle(ov, (int(q[0]), int(q[1])), 5, (0, 255, 0), -1)
    q = plan2img([list(vp)])[0]
    cv2.circle(ov, (int(q[0]), int(q[1])), 8, (255, 255, 255), 2)
    cv2.imwrite(os.path.join(OUT, "overlay_on_reference.png"), ov)

print(json.dumps(layout["stats"], indent=1))
