#include "ApriltagFamily.h"
#include <apriltag/tag16h5.h>
#include <apriltag/tag25h9.h>
#include <apriltag/tag36h11.h>
#include <apriltag/tagStandard41h12.h>
#include <algorithm>

apriltag_family_t* CreateApriltagFamily(ApriltagFamilyKind kind)
{
	switch (kind) {
	case APRILTAG_FAMILY_16H5: return tag16h5_create();
	case APRILTAG_FAMILY_25H9: return tag25h9_create();
	case APRILTAG_FAMILY_STANDARD41H12: return tagStandard41h12_create();
	case APRILTAG_FAMILY_36H11:
	default: return tag36h11_create();
	}
}

void DestroyApriltagFamily(ApriltagFamilyKind kind, apriltag_family_t* family)
{
	switch (kind) {
	case APRILTAG_FAMILY_16H5: tag16h5_destroy(family); break;
	case APRILTAG_FAMILY_25H9: tag25h9_destroy(family); break;
	case APRILTAG_FAMILY_STANDARD41H12: tagStandard41h12_destroy(family); break;
	case APRILTAG_FAMILY_36H11:
	default: tag36h11_destroy(family); break;
	}
}

int ClampMaxHamming(int requested)
{
	return std::clamp(requested, 0, 2);
}
