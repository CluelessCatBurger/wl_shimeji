#ifndef PHYSICS_H
#define PHYSICS_H

#include <stdint.h>

enum border_type {
    BORDER_TYPE_NONE = 0x0,
    BORDER_TYPE_FLOOR = 0x1,
    BORDER_TYPE_CEILING = 0x2,
    BORDER_TYPE_LEFT = 0x4,
    BORDER_TYPE_RIGHT = 0x8,

    BORDER_TYPE_ANY = 0xFFFFFFFF
};

#define COLLIDED(x) !!((x) & 0xF)
#define BORDER_IS_WALL(x) (((x & 0xF) & BORDER_TYPE_RIGHT) || ((x & 0xF) & BORDER_TYPE_LEFT))
#define BORDER_TYPE(x) ((x) & 0xF)
#define MOVE_OOB(x) ((x) & 0x20)
#define APPLY_MASK(x, mask) ((x) & ~(mask))

#define COLLISION_RESULT_OOB 0x20 // Collision occurred and collision type is inner
#define COLLISION_OPT_NOCLAMP 0x40 // Do not clamp the movement

enum collision_type {
    INNER_COLLISION = 0x0,
    OUTER_COLLISION = 0x1
};

struct bounding_box {
    enum collision_type type;
    int32_t x, y;
    int32_t width, height;
};

// Checks if a point lies on the border of a bounding box
int32_t check_collision_at(struct bounding_box* box, int32_t x, int32_t y, int32_t border_mask);
int32_t check_movement_collision(
    struct bounding_box* box,
    int32_t starting_x, int32_t starting_y,
    int32_t target_x, int32_t target_y,
    int32_t border_mask,
    int32_t* out_x, int32_t* out_y
);

int32_t is_inside(struct bounding_box* box, int32_t x, int32_t y);
int32_t is_outside(struct bounding_box* box, int32_t x, int32_t y);
int32_t project_coords_to_border(struct bounding_box* box, int32_t x, int32_t y, int32_t border_type, int32_t* out_x, int32_t* out_y);

int32_t bounding_boxes_intersect(struct bounding_box* a, struct bounding_box* b, struct bounding_box* out);
int32_t bounding_box_touches(struct bounding_box* a, struct bounding_box* b, int32_t border_mask);

#endif
