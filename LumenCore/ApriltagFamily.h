#pragma once
#include "IApriltagBackend.h"
#include <apriltag/apriltag.h>

// Creates the libapriltag family for a kind; destroy it with DestroyApriltagFamily using the same kind.
apriltag_family_t* CreateApriltagFamily(ApriltagFamilyKind kind);
void DestroyApriltagFamily(ApriltagFamilyKind kind, apriltag_family_t* family);

// the maxHamming a detector accepts: clamped to what libapriltag can decode with sensible memory
int ClampMaxHamming(int requested);
