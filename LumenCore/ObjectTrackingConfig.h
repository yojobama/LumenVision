#pragma once

// Settings of the tracker that gives an object detector's boxes a stable identity and a velocity (ObjectTracker.h). A plain struct of plain
// types in a header of its own so SWIG can wrap it for Manager's get/set.
struct ObjectTrackingConfig {
	bool enabled = false;
	// a detection can only continue a track when its box overlaps the track's predicted box by at least this much (intersection over union)
	double minIoU = 0.1;
	// consecutive frames a new object must be seen before it gets a track id (until then its id is -1), so a one-frame false positive burns none
	int minHits = 3;
	// a track that is not seen for this long (capture time) is dropped, and its id is never reused
	int maxAgeMs = 500;
	// a detection only continues a track of its own class
	bool classAware = true;
};

