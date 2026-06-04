#include "../../include/coco.h"

static const char *coco_version_string = "2.2.0";

const char *coco_version(void) {
    return coco_version_string;
}

int coco_version_major(void) { return 2; }
int coco_version_minor(void) { return 2; }
int coco_version_patch(void) { return 0; }
