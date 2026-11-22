"""Upscaling of translated menu images to the 360 HD sizes with Real-ESRGAN (ncnn-vulkan build).

The 360 redrew most menu textures at about 1.5x the PS2 size. For a translated PS2 image the colour
and the alpha are upscaled separately with the same model (alpha as a grey image, which keeps the
outlines crisp), then both are resampled with Lanczos to the exact 360 size. Before upscaling, the
colour of fully transparent pixels is filled from the nearest visible pixels so no dark fringe
bleeds into the edges. Results are cached by content hash (<cache>/<sha1>.png), so re-runs only
process new images.

Quality guard: the upscaled image is box-filtered back to the PS2 size and compared with the source
(premultiplied RGBA, mean error), and the same is done for a plain Lanczos upscale. When the
Real-ESRGAN result strays clearly further from the source than Lanczos does (more than
FALLBACK_RATIO times and above FALLBACK_MIN_ERROR), the image falls back to Lanczos and is listed.
"""

import hashlib
import os
import shutil
import subprocess
from pathlib import Path

import numpy as np

DEFAULT_MODEL = "realesr-animevideov3"
DEFAULT_SCALE = 2
FALLBACK_RATIO = 2.0
FALLBACK_MIN_ERROR = 6.0    # premultiplied 0-255, mean over the image, vs the PS2 source


def _pil():
    from PIL import Image
    return Image


def bleed(rgba, passes=16):
    """Copies colour into transparent pixels from visible neighbours (simple dilation)."""
    rgb = rgba[..., :3].astype(np.float64)
    known = rgba[..., 3] > 0
    if known.all() or not known.any():
        return rgba[..., :3].copy()
    rgb[~known] = 0
    for _ in range(passes):
        acc = np.zeros_like(rgb)
        cnt = np.zeros(known.shape)
        for dy in (-1, 0, 1):
            for dx in (-1, 0, 1):
                if dy == dx == 0:
                    continue
                shifted_known = np.roll(np.roll(known, dy, 0), dx, 1)
                shifted_rgb = np.roll(np.roll(rgb, dy, 0), dx, 1)
                acc += shifted_rgb * shifted_known[..., None]
                cnt += shifted_known
        grow = (~known) & (cnt > 0)
        if not grow.any():
            break
        rgb[grow] = acc[grow] / cnt[grow][:, None]
        known = known | grow
    return np.clip(np.rint(rgb), 0, 255).astype(np.uint8)


def lanczos(rgba, size):
    Image = _pil()
    return np.stack([np.asarray(Image.fromarray(np.ascontiguousarray(rgba[..., c])).resize(size, Image.LANCZOS))
                     for c in range(rgba.shape[2])], axis=-1)


def premultiplied(rgba):
    a = rgba.astype(np.float64)
    return np.concatenate([a[..., :3] * a[..., 3:] / 255.0, a[..., 3:]], axis=-1)


def back_error(result, source):
    """Mean error of `result` box-filtered back to the size of `source`."""
    Image = _pil()
    h, w = source.shape[:2]
    pm = premultiplied(result)
    down = np.stack([np.asarray(Image.fromarray(pm[..., c].astype(np.float32), mode="F").resize((w, h), Image.BOX))
                     for c in range(4)], axis=-1)
    return float(np.abs(down - premultiplied(source)).mean())


class Upscaler:
    def __init__(self, exe, cache_dir, model=DEFAULT_MODEL, scale=DEFAULT_SCALE, log=print):
        self.exe = Path(exe)
        self.models = self.exe.parent / "models"
        self.cache = Path(cache_dir) / f"{model}-x{scale}"
        self.model, self.scale, self.log = model, scale, log
        self.jobs = {}
        self.stats = []   # (back error Real-ESRGAN, back error Lanczos) per image

    def available(self):
        return self.exe.is_file()

    def _key(self, image):
        return hashlib.sha1(image.tobytes() + str(image.shape).encode()).hexdigest()

    def add(self, rgba):
        """Queues the colour and alpha planes of an image; returns its key."""
        pad = 4
        padded = np.pad(rgba, ((pad, pad), (pad, pad), (0, 0)), mode="edge")
        colour = bleed(padded)
        alpha = np.repeat(padded[..., 3:4], 3, axis=2)
        keys = (self._key(colour), self._key(alpha))
        for key, plane in zip(keys, (colour, alpha)):
            self.jobs[key] = plane
        return keys

    def run(self):
        """Upscales every queued plane that is not cached yet (one GPU run for the whole batch)."""
        Image = _pil()
        self.cache.mkdir(parents=True, exist_ok=True)
        todo = {k: v for k, v in self.jobs.items() if not (self.cache / f"{k}.png").exists()}
        self.log(f"[upscale] {len(self.jobs)} planes, {len(todo)} to process with {self.model} x{self.scale}")
        if not todo:
            return
        if not self.available():
            raise FileNotFoundError(f"realesrgan-ncnn-vulkan not found: {self.exe}")
        temp_in, temp_out = self.cache.parent / "_in", self.cache.parent / "_out"
        for d in (temp_in, temp_out):
            shutil.rmtree(d, ignore_errors=True)
            d.mkdir(parents=True)
        for key, plane in todo.items():
            Image.fromarray(plane).save(temp_in / f"{key}.png")
        cmd = [str(self.exe), "-i", str(temp_in), "-o", str(temp_out), "-n", self.model,
               "-s", str(self.scale), "-m", str(self.models), "-f", "png"]
        result = subprocess.run(cmd, capture_output=True, text=True)
        for key in todo:
            out = temp_out / f"{key}.png"
            if not out.exists():
                raise RuntimeError(f"Real-ESRGAN produced no output for {key}:\n{result.stderr[-2000:]}")
            os.replace(out, self.cache / f"{key}.png")
        shutil.rmtree(temp_in, ignore_errors=True)
        shutil.rmtree(temp_out, ignore_errors=True)

    def result(self, keys, source, size):
        """(RGBA at `size`, method, back error). Falls back to Lanczos when the guard trips."""
        Image = _pil()
        pad = 4 * self.scale
        planes = []
        for key in keys:
            up = np.asarray(Image.open(self.cache / f"{key}.png").convert("RGB"))
            planes.append(up[pad:up.shape[0] - pad, pad:up.shape[1] - pad])
        colour = lanczos(planes[0], size)
        alpha = lanczos(planes[1][..., :1], size)
        out = np.concatenate([colour, alpha], axis=-1)
        error = back_error(out, source)
        fallback = lanczos(source, size)
        reference = back_error(fallback, source)
        self.stats.append((error, reference))
        if error > FALLBACK_MIN_ERROR and error > FALLBACK_RATIO * reference:
            return fallback, "lanczos (fallback)", error
        return out, f"{self.model} x{self.scale}", error
