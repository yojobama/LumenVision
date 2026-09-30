package org.lumenvision.photoncompat;

/** A point in image pixels (x right, y down), as photonlib's TargetCorner. */
public final class LumenTargetCorner {
    public final double x;
    public final double y;

    public LumenTargetCorner(double x, double y) {
        this.x = x;
        this.y = y;
    }
}
