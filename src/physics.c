#include "physics.h"
#include <stdio.h>

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

int32_t check_movement_collision(
    struct bounding_box* box,
    int32_t starting_x, int32_t starting_y,
    int32_t target_x, int32_t target_y,
    int32_t border_mask,
    int32_t* out_x, int32_t* out_y
)
{
    int32_t result = 0x0;
    int32_t _out_x = 0;
    int32_t _out_y = 0;


    if (!out_x) {
        out_x = &_out_x;
    }
    if (!out_y) {
        out_y = &_out_y;
    }

    // Check if starting point is inside the box
    int32_t starting_inside = (starting_x >= box->x && starting_x <= box->x + box->width &&
                                starting_y >= box->y && starting_y <= box->y + box->height);

    // Check if target point is inside the box
    int32_t target_inside = (target_x >= box->x && target_x <= box->x + box->width &&
                             target_y >= box->y && target_y <= box->y + box->height);

    if (starting_inside == target_inside) {
        *out_x = target_x;
        *out_y = target_y;
        return result;
    }

    // Determine collision type
    if ((box->type == INNER_COLLISION) && !starting_inside) {
        // Ignore movement from outside to inside
        *out_x = target_x;
        *out_y = target_y;
        return result;
    } else if ((box->type == OUTER_COLLISION) && starting_inside) {
        // Ignore movement from inside to outside
        *out_x = target_x;
        *out_y = target_y;
        return result;
    }

    // Check for intersection with the box borders
    if (target_x < box->x && starting_x != target_x) {
        result = BORDER_TYPE_LEFT;
        if (!(border_mask & COLLISION_OPT_NOCLAMP)) *out_x = box->x;
    } else if (target_x > box->x + box->width && starting_x != target_x) {
        result = BORDER_TYPE_RIGHT;
        if (!(border_mask & COLLISION_OPT_NOCLAMP)) *out_x = box->x + box->width;
    } else {
        *out_x = target_x;
    }

    if (target_y < box->y && starting_y != target_y) {
        result = BORDER_TYPE_CEILING;
        if (!(border_mask & COLLISION_OPT_NOCLAMP)) *out_y = box->y;
    } else if (target_y > box->y + box->height && starting_y != target_y) {
        result = BORDER_TYPE_FLOOR;
        if (!(border_mask & COLLISION_OPT_NOCLAMP)) *out_y = box->y + box->height;
    } else {
        *out_y = target_y;
    }

    if (result) {
        if (box->type == INNER_COLLISION) result |= COLLISION_RESULT_OOB;
    }

    return result & ~border_mask;
}

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
