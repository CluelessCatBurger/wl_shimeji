#include "physics.h"

#include <stdbool.h>
#include <math.h>

// Checks if a point lies on the border of a bounding box
int32_t check_collision_at(struct bounding_box* box, int32_t x, int32_t y, int32_t border_mask)
{
    int32_t borders = 0x0;
    if (x != box->x && x != box->x + box->width &&
        y != box->y && y != box->y + box->height) {
        return borders;
    }

    if (y == box->y && x >= box->x && x <= box->x + box->width) {
        borders |= BORDER_TYPE_CEILING;
    }
    if (x == box->x && y >= box->y && y <= box->y + box->height) {
        borders |= BORDER_TYPE_LEFT;
    }
    if (x == box->x + box->width && y >= box->y && y <= box->y + box->height) {
        borders |= BORDER_TYPE_RIGHT;
    }
    if (y == box->y + box->height && x >= box->x && x <= box->x + box->width) {
        borders |= BORDER_TYPE_FLOOR;
    }

    return borders & ~border_mask;
}

static inline bool point_in_box(const struct bounding_box* b, int32_t px, int32_t py) {
    return (px >= b->x && px <= b->x + b->width
         && py >= b->y && py <= b->y + b->height);
}

int32_t check_movement_collision(
    struct bounding_box* box,
    int32_t sx, int32_t sy,
    int32_t tx, int32_t ty,
    int32_t border_mask,
    int32_t* out_x, int32_t* out_y
)
{

    int result = 0;

    // default: no clamp → keep target
    *out_x = tx;
    *out_y = ty;

    bool no_clamp = (border_mask & COLLISION_OPT_NOCLAMP) != 0;
    // which borders to *exclude* from the returned hit bits
    int exclude = border_mask
                & (BORDER_TYPE_FLOOR | BORDER_TYPE_CEILING
                 | BORDER_TYPE_LEFT  | BORDER_TYPE_RIGHT);

    // ─── NEW: if the segment lies exactly on a border‐line, just exclude that one ───
    if (sy == box->y           && ty == box->y)           exclude |= BORDER_TYPE_CEILING;
    if (sy == box->y + box->height && ty == box->y + box->height) exclude |= BORDER_TYPE_FLOOR;
    if (sx == box->x           && tx == box->x)           exclude |= BORDER_TYPE_LEFT;
    if (sx == box->x + box->width  && tx == box->x + box->width)  exclude |= BORDER_TYPE_RIGHT;
    // ────────────────────────────────────────────────────────────────────────────────

    bool start_in  = point_in_box(box, sx, sy);
    bool target_in = point_in_box(box, tx, ty);

    // enforce INNER vs OUTER rules
    if (box->type == INNER_COLLISION) {
        if (!start_in || target_in) return 0;
    } else { // OUTER_COLLISION
        if ( start_in || !target_in) return 0;
    }

    double dx = (double)tx - sx;
    double dy = (double)ty - sy;

    // find *all* intersections, track the earliest t∈(0..1]
    double best_t = 2.0;
    int    hit_all = 0;
    double ix = tx, iy = ty;

    // LEFT & RIGHT
    if (dx != 0.0) {
        double t, y_at, bx;
        // LEFT
        bx = box->x;
        t  = (bx - sx) / dx;
        if (t > 0.0 && t <= 1.0) {
            y_at = sy + t*dy;
            if (y_at >= box->y && y_at <= box->y + box->height) {
                if (t < best_t - 1e-9) {
                    best_t = t; hit_all = BORDER_TYPE_LEFT;
                    ix = bx; iy = y_at;
                }
                else if (fabs(t - best_t) < 1e-9) {
                    hit_all |= BORDER_TYPE_LEFT;
                }
            }
        }
        // RIGHT
        bx = box->x + box->width;
        t  = (bx - sx) / dx;
        if (t > 0.0 && t <= 1.0) {
            y_at = sy + t*dy;
            if (y_at >= box->y && y_at <= box->y + box->height) {
                if (t < best_t - 1e-9) {
                    best_t = t; hit_all = BORDER_TYPE_RIGHT;
                    ix = bx; iy = y_at;
                }
                else if (fabs(t - best_t) < 1e-9) {
                    hit_all |= BORDER_TYPE_RIGHT;
                }
            }
        }
    }

    // CEILING & FLOOR
    if (dy != 0.0) {
        double t, x_at, by;
        // CEILING
        by = box->y;
        t  = (by - sy) / dy;
        if (t > 0.0 && t <= 1.0) {
            x_at = sx + t*dx;
            if (x_at >= box->x && x_at <= box->x + box->width) {
                if (t < best_t - 1e-9) {
                    best_t = t; hit_all = BORDER_TYPE_CEILING;
                    ix = x_at; iy = by;
                }
                else if (fabs(t - best_t) < 1e-9) {
                    hit_all |= BORDER_TYPE_CEILING;
                }
            }
        }
        // FLOOR
        by = box->y + box->height;
        t  = (by - sy) / dy;
        if (t > 0.0 && t <= 1.0) {
            x_at = sx + t*dx;
            if (x_at >= box->x && x_at <= box->x + box->width) {
                if (t < best_t - 1e-9) {
                    best_t = t; hit_all = BORDER_TYPE_FLOOR;
                    ix = x_at; iy = by;
                }
                else if (fabs(t - best_t) < 1e-9) {
                    hit_all |= BORDER_TYPE_FLOOR;
                }
            }
        }
    }

    if (hit_all == 0) {
        // no intersection at all
        return 0;
    }

    // clamp output unless NOCLAMP
    if (!no_clamp) {
        *out_x = (int32_t)lround(ix);
        *out_y = (int32_t)lround(iy);
    }

    if (result) {
        if (box->type == INNER_COLLISION) result |= COLLISION_RESULT_OOB;
    }
    // strip out the excluded borders before returning
    int hit_visible = hit_all & ~exclude;
    result |= hit_visible;

    // for INNER_COLLISION, always add the OOB flag if we crossed any side
    return result;
}
//     int32_t result = 0x0;
//     int32_t _out_x = 0;
//     int32_t _out_y = 0;


//     if (!out_x) {
//         out_x = &_out_x;
//     }
//     if (!out_y) {
//         out_y = &_out_y;
//     }

//     // Check if starting point is inside the box
//     int32_t starting_inside = (starting_x >= box->x && starting_x <= box->x + box->width &&
//                                 starting_y >= box->y && starting_y <= box->y + box->height);

//     // Check if target point is inside the box
//     int32_t target_inside = (target_x >= box->x && target_x <= box->x + box->width &&
//                              target_y >= box->y && target_y <= box->y + box->height);

//     if (starting_inside == target_inside) {
//         *out_x = target_x;
//         *out_y = target_y;
//         return result;
//     }

//     // Determine collision type
//     if ((box->type == INNER_COLLISION) && !starting_inside) {
//         // Ignore movement from outside to inside
//         *out_x = target_x;
//         *out_y = target_y;
//         return result;
//     } else if ((box->type == OUTER_COLLISION) && starting_inside) {
//         // Ignore movement from inside to outside
//         *out_x = target_x;
//         *out_y = target_y;
//         return result;
//     }

//     // Check for intersection with the box borders
//     if (
//     //  COLLISION_INNER
//         (
//             starting_inside ?
//             (target_x > box->x + box->width)
//             :
//             (target_x < box->x + box->width) && (starting_x > box->x)// && (starting_x < box->x + box->width)
//         )
//         &&
//         starting_x != target_x
//     ) {
//         printf("CLAMP TO RIGHT\n");
//         result = BORDER_TYPE_RIGHT;
//         if (!(border_mask & COLLISION_OPT_NOCLAMP)) *out_x = box->x + box->width;
//     } else if (
//     //  we start inside? is our target outside bb? orelse is out target inside bb and we are outside
//         (
//             starting_inside ?
//             (target_x < box->x)
//             :
//             (target_x < box->x) && (starting_x < box->x + box->width)// && (starting_x < box->x)
//         )
//         &&
//         starting_x != target_x // We are actually moving on that axis
//     ) {
//         result = BORDER_TYPE_LEFT; // So we are on left border
//         if (!(border_mask & COLLISION_OPT_NOCLAMP)) *out_x = box->x; // If we clamping, clamp to left border
//     } else {
//         *out_x = target_x;
//     }

//     if (
//         (
//             starting_inside ?
//             (target_y < box->y)
//             :
//             (target_y > box->y) && (starting_y < box->y + box->height) && (starting_y < box->y)
//         )
//         &&
//         starting_y != target_y // We are actually moving on that axis
//     ) {
//         result = BORDER_TYPE_CEILING;
//         if (!(border_mask & COLLISION_OPT_NOCLAMP)) *out_y = box->y;
//     } else if (
//         (
//             starting_inside ?
//             (target_y > box->y + box->height)
//             :
//             (target_y < box->y + box->height) && (starting_y > box->y) && (starting_y > box->y + box->height)
//         )
//         &&
//         starting_y != target_y
//     ) {
//         result = BORDER_TYPE_FLOOR;
//         if (!(border_mask & COLLISION_OPT_NOCLAMP)) *out_y = box->y + box->height;
//     } else {
//         *out_y = target_y;
//     }

//     if (result) {
//         if (box->type == INNER_COLLISION) result |= COLLISION_RESULT_OOB;
//     }

//     return result & ~border_mask;
// }

int32_t is_inside(struct bounding_box *box, int32_t x, int32_t y)
{
    return (x >= box->x && x <= box->x + box->width &&
            y >= box->y && y <= box->y + box->height);
}

int32_t is_outside(struct bounding_box *box, int32_t x, int32_t y)
{
    return (x < box->x || x > box->x + box->width ||
            y < box->y || y > box->y + box->height);
}

int32_t project_coords_to_border(struct bounding_box *box, int32_t x, int32_t y, int32_t border_type, int32_t *out_x, int32_t *out_y)
{
    if (border_type & BORDER_TYPE_LEFT) {
        *out_x = box->x;
        *out_y = y;
    } else if (border_type & BORDER_TYPE_RIGHT) {
        *out_x = box->x + box->width;
        *out_y = y;
    } else if (border_type & BORDER_TYPE_CEILING) {
        *out_x = x;
        *out_y = box->y;
    } else if (border_type & BORDER_TYPE_FLOOR) {
        *out_x = x;
        *out_y = box->y + box->height;
    } else if (border_type == (int32_t)BORDER_TYPE_ANY) {
        if (x < box->x) {
            *out_x = box->x;
        } else if (x > box->x + box->width) {
            *out_x = box->x + box->width;
        } else {
            *out_x = x;
        }

        if (y < box->y) {
            *out_y = box->y;
        } else if (y > box->y + box->height) {
            *out_y = box->y + box->height;
        } else {
            *out_y = y;
        }
    } else {
        return -1;
    }

    return 0;
}

int32_t bounding_boxes_intersect(struct bounding_box *a, struct bounding_box *b, struct bounding_box* out)
{
    if (!a || !b) {
        return 0;
    }

    int32_t x1 = (a->x > b->x) ? a->x : b->x;
    int32_t y1 = (a->y > b->y) ? a->y : b->y;
    int32_t x2 = ((a->x + a->width) < (b->x + b->width)) ? (a->x + a->width) : (b->x + b->width);
    int32_t y2 = ((a->y + a->height) < (b->y + b->height)) ? (a->y + a->height) : (b->y + b->height);

    if (x1 < x2 && y1 < y2) {
        if (out) {
            out->x = x1;
            out->y = y1;
            out->width = x2 - x1;
            out->height = y2 - y1;
        }
        return 1;
    }

    return 0;
}


int32_t bounding_box_touches(struct bounding_box* a, struct bounding_box* b, int32_t border_mask)
{
    if (!a || !b) {
        return 0;
    }

    int32_t result = 0x0;

    // Check if any of the borders of bounding box 'a' touch bounding box 'b'
    if (a->x == b->x + b->width && a->y < b->y + b->height && a->y + a->height > b->y) {
        result |= BORDER_TYPE_LEFT;
    } else if (a->x + a->width == b->x && a->y < b->y + b->height && a->y + a->height > b->y) {
        result |= BORDER_TYPE_RIGHT;
    } else if (a->y == b->y + b->height && a->x < b->x + b->width && a->x + a->width > b->x) {
        result |= BORDER_TYPE_CEILING;
    } else if (a->y + a->height == b->y && a->x < b->x + b->width && a->x + a->width > b->x) {
        result |= BORDER_TYPE_FLOOR;
    }

    return result & ~border_mask;
}
