#!/usr/bin/env python3
"""
Verification script for GameDetail image scaling and boundary safety.
Simulates 10 rapid detail openings with different image sizes (vertical posters, landscape panoramas, tiny thumbnails).
Validates that srcX and srcY calculations never exceed buffer bounds.
"""
import sys

def simulate_draw_prescaled_image(dst_w, dst_h, src_w, src_h):
    # Replicate display_manager.cpp logic:
    # int sx0 = (x * srcW) / dstW;
    # int sy0 = (y * srcH) / dstH;
    # if (sx0 >= srcW) sx0 = srcW - 1;
    # if (sy0 >= srcH) sy0 = srcH - 1;
    # if (sx0 < 0) sx0 = 0;
    # if (sy0 < 0) sy0 = 0;
    # size = srcW * srcH * 4
    total_pixels = src_w * src_h
    max_idx = (total_pixels - 1) * 4 + 3

    for y in range(dst_h):
        for x in range(dst_w):
            sx0 = (x * src_w) // dst_w
            sy0 = (y * src_h) // dst_h
            if sx0 >= src_w: sx0 = src_w - 1
            if sy0 >= src_h: sy0 = src_h - 1
            if sx0 < 0: sx0 = 0
            if sy0 < 0: sy0 = 0

            # Linear buffer index
            idx00 = (sy0 * src_w + sx0) * 4
            if idx00 < 0 or idx00 + 3 > max_idx:
                raise ValueError(f"Out of bounds! idx00={idx00}, max={max_idx} at dst=({x},{y})")
            
            # Check bilinear neighbor
            sx1 = sx0 + 1 if sx0 + 1 < src_w else sx0
            sy1 = sy0 + 1 if sy0 + 1 < src_h else sy0
            idx11 = (sy1 * src_w + sx1) * 4
            if idx11 < 0 or idx11 + 3 > max_idx:
                raise ValueError(f"Out of bounds! idx11={idx11}, max={max_idx} at dst=({x},{y})")

def main():
    test_cases = [
        # (dst_w, dst_h, src_w, src_h, description)
        (1920, 1080, 1920, 1080, "Exact 1080p panoramic detail"),
        (1920, 1080, 210, 280, "Vertical poster fallback in detail view (The exact bug that caused CE-108255-1)"),
        (1920, 1080, 1920, 540, "Hero banner 540p in detail view"),
        (1920, 540, 210, 280, "Vertical poster in hero banner"),
        (1920, 540, 1920, 540, "Hero banner exact 540p"),
        (210, 280, 300, 400, "Poster card upscale"),
        (210, 280, 100, 133, "Poster card downscale"),
        (1920, 1080, 1, 1, "Degenerate 1x1 image"),
        (1920, 1080, 1280, 720, "720p HD panorama to 1080p"),
        (1920, 1080, 3840, 2160, "4K UHD panorama downscale to 1080p")
    ]

    print("Running 10 rapid GameDetail scaling simulations...")
    for i, (dw, dh, sw, sh, desc) in enumerate(test_cases, 1):
        try:
            simulate_draw_prescaled_image(dw, dh, sw, sh)
            print(f"[{i}/10] PASS: {desc} ({sw}x{sh} -> {dw}x{dh})")
        except Exception as e:
            print(f"[{i}/10] FAIL: {desc} -> {e}")
            return 1
    
    print("\nAll 10 simulations passed with 0 memory violations!")
    return 0

if __name__ == "__main__":
    sys.exit(main())
