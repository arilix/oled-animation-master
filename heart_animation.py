"""
Beautiful animated heart, generated from the parametric "Heart Curve":

    x(t) = 16 sin^3(t)
    y(t) = 13 cos(t) - 5 cos(2t) - 2 cos(3t) - cos(4t)     0 <= t < 2*pi

Two phases:
  1. Draw-in  : the curve is traced stroke-by-stroke with a glowing pen tip.
  2. Heartbeat: once fully drawn, the heart pulses like a real heartbeat
                (double-thump), with a soft neon glow and drifting sparkles.

Output: heart_animation.mp4 (via ffmpeg) in the project root.
"""

import numpy as np
import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.animation import FuncAnimation, FFMpegWriter

# ---------------- configuration ----------------
OUTPUT_FILE = "heart_animation.mp4"
FPS = 30
DRAW_SECONDS = 1.8
BEAT_SECONDS = 3.6
HOLD_SECONDS = 0.6
BEAT_PERIOD = 1.1          # seconds per heartbeat cycle
N_CURVE_POINTS = 1000
N_PARTICLES = 35

DRAW_FRAMES = int(DRAW_SECONDS * FPS)
BEAT_FRAMES = int(BEAT_SECONDS * FPS)
HOLD_FRAMES = int(HOLD_SECONDS * FPS)
TOTAL_FRAMES = DRAW_FRAMES + BEAT_FRAMES + HOLD_FRAMES

BG_COLOR = "#05010a"
HEART_COLOR = (1.0, 0.10, 0.28)      # crimson core
GLOW_COLOR = (1.0, 0.10, 0.28)
SPARK_COLOR = (1.0, 0.85, 0.9)

rng = np.random.default_rng(7)

# ---------------- heart curve ----------------
t_full = np.linspace(0, 2 * np.pi, N_CURVE_POINTS)
x_full = 16 * np.sin(t_full) ** 3
y_full = (
    13 * np.cos(t_full)
    - 5 * np.cos(2 * t_full)
    - 2 * np.cos(3 * t_full)
    - np.cos(4 * t_full)
)
center_y = (y_full.max() + y_full.min()) / 2.0
center = np.array([0.0, center_y])

# ---------------- particles (ambient sparkles) ----------------
particle_x = rng.uniform(-19, 19, N_PARTICLES)
particle_y = rng.uniform(-19, 15, N_PARTICLES)
particle_speed = rng.uniform(0.03, 0.10, N_PARTICLES)
particle_phase = rng.uniform(0, 2 * np.pi, N_PARTICLES)
particle_size = rng.uniform(4, 16, N_PARTICLES)


def heartbeat_scale(t):
    """Double-thump heartbeat waveform, returns scale multiplier around 1.0."""
    phase = (t % BEAT_PERIOD) / BEAT_PERIOD
    bump1 = 0.14 * np.exp(-(((phase - 0.06) / 0.028) ** 2))
    bump2 = 0.07 * np.exp(-(((phase - 0.20) / 0.035) ** 2))
    return 1.0 + bump1 + bump2


# ---------------- figure setup ----------------
fig, ax = plt.subplots(figsize=(6, 6), dpi=140)
fig.patch.set_facecolor(BG_COLOR)
ax.set_facecolor(BG_COLOR)
ax.set_xlim(-20, 20)
ax.set_ylim(-21, 15)
ax.set_aspect("equal")
ax.axis("off")
fig.subplots_adjust(left=0, right=1, top=1, bottom=0)

# soft radial vignette behind everything
grad_res = 300
gx, gy = np.meshgrid(
    np.linspace(-20, 20, grad_res), np.linspace(-21, 15, grad_res)
)
dist = np.sqrt(gx**2 + (gy - center_y) ** 2)
vignette = np.clip(1.0 - dist / 26.0, 0, 1) ** 2
vignette_rgba = np.zeros((grad_res, grad_res, 4))
vignette_rgba[..., 0] = 0.35
vignette_rgba[..., 1] = 0.02
vignette_rgba[..., 2] = 0.08
vignette_rgba[..., 3] = vignette * 0.55
ax.imshow(
    vignette_rgba,
    extent=(-20, 20, -21, 15),
    origin="lower",
    zorder=0,
    interpolation="bilinear",
)

# glow layers (drawn thick->thin, transparent->opaque) + bright core line
glow_widths = [14, 10, 7, 4.5]
glow_alphas = [0.05, 0.08, 0.12, 0.18]
glow_lines = [
    ax.plot([], [], color=GLOW_COLOR, linewidth=w, alpha=a, solid_capstyle="round",
             zorder=2)[0]
    for w, a in zip(glow_widths, glow_alphas)
]
core_line = ax.plot([], [], color=HEART_COLOR, linewidth=2.2, alpha=0.95,
                     solid_capstyle="round", zorder=3)[0]
bright_line = ax.plot([], [], color="white", linewidth=0.9, alpha=0.55,
                       solid_capstyle="round", zorder=4)[0]

pen_tip = ax.scatter([], [], s=110, color="white", alpha=0.9, zorder=5,
                      edgecolors="none")
sparkles = ax.scatter(
    particle_x, particle_y, s=particle_size, color=SPARK_COLOR, alpha=0.0,
    zorder=1, edgecolors="none"
)

all_lines = glow_lines + [core_line, bright_line]


def scaled_curve(scale):
    pts = np.stack([x_full, y_full], axis=1)
    pts = center + (pts - center) * scale
    return pts[:, 0], pts[:, 1]


def update(frame):
    if frame < DRAW_FRAMES:
        # --- phase 1: draw-in ---
        progress = (frame + 1) / DRAW_FRAMES
        n_pts = max(2, int(progress * N_CURVE_POINTS))
        xs, ys = x_full[:n_pts], y_full[:n_pts]
        for ln in all_lines:
            ln.set_data(xs, ys)
        pen_tip.set_offsets([[xs[-1], ys[-1]]])
        pen_tip.set_alpha(0.9)
        sparkles.set_alpha(np.zeros(N_PARTICLES))
    else:
        # --- phase 2 & 3: heartbeat pulse + gentle hold ---
        beat_t = (frame - DRAW_FRAMES) / FPS
        if frame < DRAW_FRAMES + BEAT_FRAMES:
            scale = heartbeat_scale(beat_t)
        else:
            scale = 1.0  # settle after the last beat
        xs, ys = scaled_curve(scale)
        for ln in all_lines:
            ln.set_data(xs, ys)
        pen_tip.set_alpha(0.0)

        twinkle = 0.25 + 0.25 * np.sin(beat_t * 2.0 + particle_phase)
        fade_in = min(1.0, (frame - DRAW_FRAMES) / (FPS * 0.5))
        sparkles.set_alpha(np.clip(twinkle * fade_in, 0, 0.85))
        new_y = particle_y + (frame - DRAW_FRAMES) * particle_speed * 0.06
        wrapped_y = ((new_y + 21) % 36) - 21
        sparkles.set_offsets(np.stack([particle_x, wrapped_y], axis=1))

        pulse_extra = (scale - 1.0) * 2.0
        for ln, base_a in zip(glow_lines, glow_alphas):
            ln.set_alpha(min(0.9, base_a + pulse_extra))

    return all_lines + [pen_tip, sparkles]


anim = FuncAnimation(fig, update, frames=TOTAL_FRAMES, blit=False, interval=1000 / FPS)

writer = FFMpegWriter(fps=FPS, bitrate=4000)
anim.save(OUTPUT_FILE, writer=writer)
print(f"Saved {OUTPUT_FILE} ({TOTAL_FRAMES} frames @ {FPS}fps)")
