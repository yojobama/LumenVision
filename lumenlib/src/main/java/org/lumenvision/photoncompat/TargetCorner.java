package org.lumenvision.photoncompat;

/** A point in image pixels, as photonlib's TargetCorner. */
public class TargetCorner {
    public final double x;
    public final double y;

    public TargetCorner(double x, double y) {
        this.x = x;
        this.y = y;
    }
}
