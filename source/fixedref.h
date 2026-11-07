// SPDX-FileCopyrightText: Copyright 2026 edenprobe contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// What the fixed suite expects of the state a game's driver sets and Vulkan keeps elsewhere or
/// lacks: the point size, the alpha test, the depth bounds, the viewport's sign and swizzle, the
/// window origin and the depth mode, worked out on the processor.
///
/// In its own vocabulary rather than the graphics library's, so this file builds for the machine
/// the reference suite runs on; the fixed suite translates each description into deko3d state.

/// A compare operation, in the order the specifications list them.
typedef enum {
    FIXED_NEVER,
    FIXED_LESS,
    FIXED_EQUAL,
    FIXED_LEQUAL,
    FIXED_GREATER,
    FIXED_NOTEQUAL,
    FIXED_GEQUAL,
    FIXED_ALWAYS,
    FIXED_COMPARE_COUNT,
} FixedCompare;

/// The word a case's name carries for @p op: "less than", "not equal".
const char *FixedCompareName(FixedCompare op);

/// Whether the alpha test passes a fragment whose alpha is @p alpha against the reference
/// @p reference: the fragment's value on the left, as the alpha test of OpenGL 4.6 compatibility
/// profile 17.3.4 and NVN's alpha function state it.
bool FixedAlphaPasses(FixedCompare op, float alpha, float reference);

/// Whether the depth bounds test keeps a sample whose depth buffer holds @p stored: the value
/// already in the buffer, never the fragment's, inside the bounds with both ends included
/// (EXT_depth_bounds_test; Vulkan 1.3 28.8).
bool FixedBoundsPass(float stored, float min_bound, float max_bound);

/// The depth the window transform makes of clip space @p z (w of one) for a full depth range:
/// z itself when the device was made with depth from nought to one, z / 2 + 1 / 2 when it was
/// made with depth from minus one to one (deko3d's DkDeviceFlags_DepthMinusOneToOne, which sets
/// the viewport's z scale and offset to a half).
float FixedWindowDepth(float z, bool minus_one_to_one);

/// A rectangle of pixels, x0 and y0 included, x1 and y1 not, in the target's rows as memory holds
/// them: y downwards from the first row.
typedef struct {
    int32_t x0;
    int32_t y0;
    int32_t x1;
    int32_t y1;
} FixedRect;

/// The pixels an aliased point of @p size covers, centred at (@p cx, @p cy) in target space: the
/// pixels whose centres lie inside the size by size square around the centre. The suite only
/// draws points with no pixel centre on the square's edge, FixedPointEdgeFree, so the rule for a
/// centre exactly on an edge decides nothing.
FixedRect FixedPointRect(double cx, double cy, double size);

/// Whether no pixel centre lies on an edge of that square.
bool FixedPointEdgeFree(double cx, double cy, double size);

/// The transform from clip space to the target's rows for one viewport over a whole target of
/// @p width by @p height, as the console applies it: the viewport swizzle first, then the
/// viewport's scale and offset.
typedef struct {
    uint32_t width;
    uint32_t height;
    /// The viewport's y scale is positive, so clip space y of one lands on the last row: a device
    /// made with the lower left origin, or a scale a game wrote itself. deko3d's upper left
    /// origin writes it negative, so y of one is the first row.
    bool scale_y_positive;
    /// The swizzle hands y the negated clip y (NegativeY).
    bool negate_y;
    /// The swizzle hands x the clip y and y the clip x (PositiveY, PositiveX).
    bool swap_xy;
    /// The window origin's FlipY bit: the rasteriser takes the target as upside down when it
    /// decides which way a triangle winds. deko3d sets it for the lower left origin.
    bool flip_winding;
} FixedTransform;

/// Where clip space (@p clip_x, @p clip_y) lands, in target space with the first row's centre at
/// half a pixel down.
void FixedProject(const FixedTransform *transform, double clip_x, double clip_y, double *x,
                  double *y);

/// The pixels a clip space rectangle from (@p x0, @p y0) to (@p x1, @p y1) covers once projected:
/// its corners land on pixel edges in every rectangle the suite draws, so it is whole pixels.
FixedRect FixedProjectRect(const FixedTransform *transform, double x0, double y0, double x1,
                           double y1);

/// Whether a triangle given counter clockwise in clip space, y upwards, is front facing under
/// @p transform with the front face counter clockwise when @p front_ccw and clockwise otherwise.
///
/// The rasteriser decides from the triangle as it lands in the target, rows downwards: a triangle
/// whose corners run counter clockwise as the target is shown with its first row on top has a
/// negative signed area in rows-down coordinates and is counter clockwise, unless the window
/// origin's FlipY bit says the target is upside down. That is the console's rule as deko3d relies
/// on it (an upper left device flips the viewport and not the winding, a lower left one the
/// winding and not the viewport, and both keep OpenGL's counter clockwise front), and Vulkan's
/// for a negative viewport height.
bool FixedFrontFacing(const FixedTransform *transform, bool front_ccw);

#ifdef __cplusplus
}
#endif
