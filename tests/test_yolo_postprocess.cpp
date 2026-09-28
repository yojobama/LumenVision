#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "YoloPostProcess.h"

TEST_CASE("Letterbox scales and centers padding correctly for a wider-than-tall image", "[YoloPostProcess]") {
    cv::Mat input(480, 640, CV_8UC3, cv::Scalar(0, 0, 0));
    // Mark the top-left pixel to check its letterboxed position and the surrounding fill.
    input.at<cv::Vec3b>(0, 0) = cv::Vec3b(255, 255, 255);

    YoloPostProcess::LetterboxInfo info;
    cv::Mat output = YoloPostProcess::Letterbox(input, 640, 640, info);

    REQUIRE(output.cols == 640);
    REQUIRE(output.rows == 640);
    // scale = min(640/640, 640/480) = 1.0; the narrower dimension is padded.
    REQUIRE(info.scale == Catch::Approx(1.0));
    REQUIRE(info.padLeft == 0);
    REQUIRE(info.padTop == 80); // (640 - 480) / 2
    REQUIRE(info.originalWidth == 640);
    REQUIRE(info.originalHeight == 480);

    // The marked pixel sits at (padLeft, padTop).
    cv::Vec3b marked = output.at<cv::Vec3b>(info.padTop, info.padLeft);
    REQUIRE(marked == cv::Vec3b(255, 255, 255));

    // YOLO fill colour (114,114,114) fills the padding.
    cv::Vec3b fill = output.at<cv::Vec3b>(0, 0);
    REQUIRE(fill == cv::Vec3b(114, 114, 114));
}

TEST_CASE("DecodeAndNms recovers a single high-confidence detection in original image space", "[YoloPostProcess]") {
    // Letterbox for a 640x480 original: scale=1.0, padLeft=0, padTop=80.
    YoloPostProcess::LetterboxInfo letterbox;
    letterbox.scale = 1.0f;
    letterbox.padLeft = 0;
    letterbox.padTop = 80;
    letterbox.originalWidth = 640;
    letterbox.originalHeight = 480;

    const int numClasses = 1;
    const int numAnchors = 1;
    // channel-major, shape [4 + numClasses][numAnchors] with numAnchors=1: [cx, cy, w, h, class0]
    float outputData[4 + numClasses] = {
        200.0f, // cx (letterboxed space)
        200.0f, // cy (letterboxed space)
        50.0f,  // w
        50.0f,  // h
        0.9f    // class0 confidence
    };

    std::vector<std::string> labels = {"widget"};
    auto detections = YoloPostProcess::DecodeAndNms(
        outputData, numClasses, numAnchors, labels, letterbox, /*confThreshold*/ 0.5f, /*nmsThreshold*/ 0.5f);

    REQUIRE(detections.size() == 1);
    REQUIRE(detections[0].GetClassId() == 0);
    REQUIRE(detections[0].GetClassName() == "widget");
    REQUIRE(detections[0].GetConfidence() > 0.89f);

    // Original-space centre = ((cx - padLeft) / scale, (cy - padTop) / scale) = (200, 120).
    cv::Rect2d box = detections[0].GetBoundingBox();
    REQUIRE(box.x + box.width / 2.0 == Catch::Approx(200.0).margin(1.0));
    REQUIRE(box.y + box.height / 2.0 == Catch::Approx(120.0).margin(1.0));
}

TEST_CASE("DecodeAndNms drops detections below the confidence threshold", "[YoloPostProcess]") {
    YoloPostProcess::LetterboxInfo letterbox;
    letterbox.scale = 1.0f;
    letterbox.padLeft = 0;
    letterbox.padTop = 0;
    letterbox.originalWidth = 640;
    letterbox.originalHeight = 640;

    float outputData[5] = {200.0f, 200.0f, 50.0f, 50.0f, 0.1f}; // below confThreshold
    std::vector<std::string> labels = {"widget"};

    auto detections = YoloPostProcess::DecodeAndNms(
        outputData, 1, 1, labels, letterbox, /*confThreshold*/ 0.5f, /*nmsThreshold*/ 0.5f);

    REQUIRE(detections.empty());
}
