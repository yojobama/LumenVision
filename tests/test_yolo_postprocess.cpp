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

namespace {
YoloPostProcess::LetterboxInfo IdentityLetterbox(int width, int height) {
    YoloPostProcess::LetterboxInfo letterbox;
    letterbox.scale = 1.0f;
    letterbox.padLeft = 0;
    letterbox.padTop = 0;
    letterbox.originalWidth = width;
    letterbox.originalHeight = height;
    return letterbox;
}
}

TEST_CASE("NMS runs per class, so overlapping boxes of different classes both survive", "[YoloPostProcess]") {
    // two anchors on the same box: [cx][2], [cy][2], [w][2], [h][2], class0[2], class1[2]
    const float outputData[] = {
        300.0f, 300.0f,
        300.0f, 300.0f,
        100.0f, 100.0f,
        100.0f, 100.0f,
        0.9f, 0.0f,
        0.0f, 0.8f,
    };
    std::vector<std::string> labels = {"person", "horse"};
    auto detections = YoloPostProcess::DecodeAndNms(outputData, 2, 2, labels, IdentityLetterbox(640, 640), 0.5f, 0.45f);
    REQUIRE(detections.size() == 2);

    // the same two anchors both scoring class 0 are still deduplicated
    const float sameClass[] = {
        300.0f, 300.0f,
        300.0f, 300.0f,
        100.0f, 100.0f,
        100.0f, 100.0f,
        0.9f, 0.8f,
        0.0f, 0.0f,
    };
    REQUIRE(YoloPostProcess::DecodeAndNms(sameClass, 2, 2, labels, IdentityLetterbox(640, 640), 0.5f, 0.45f).size() == 1);
}

TEST_CASE("a box hanging over the frame edge is clipped on every side", "[YoloPostProcess]") {
    std::vector<std::string> labels = {"widget"};
    auto run = [&](float cx, float cy) {
        const float data[] = {cx, cy, 100.0f, 100.0f, 0.9f};
        return YoloPostProcess::DecodeAndNms(data, 1, 1, labels, IdentityLetterbox(640, 480), 0.5f, 0.5f);
    };

    // left/top overhang: x spans [-40, 60], y spans [-30, 70]
    auto topLeft = run(10.0f, 20.0f);
    REQUIRE(topLeft.size() == 1);
    cv::Rect2d box = topLeft[0].GetBoundingBox();
    REQUIRE(box.x == Catch::Approx(0.0));
    REQUIRE(box.y == Catch::Approx(0.0));
    REQUIRE(box.x + box.width == Catch::Approx(60.0).margin(1.0));
    REQUIRE(box.y + box.height == Catch::Approx(70.0).margin(1.0));

    // right/bottom overhang: x spans [590, 690], y spans [430, 530]
    auto bottomRight = run(640.0f, 480.0f);
    REQUIRE(bottomRight.size() == 1);
    box = bottomRight[0].GetBoundingBox();
    REQUIRE(box.x + box.width == Catch::Approx(640.0).margin(1.0));
    REQUIRE(box.y + box.height == Catch::Approx(480.0).margin(1.0));

    // a box entirely in the padding has nothing left to report
    REQUIRE(run(-200.0f, 240.0f).empty());
}

TEST_CASE("a transposed [anchors, 4+classes] output converts to the channel-major layout the decoder reads", "[YoloPostProcess]") {
    // 3 anchors x (4 box values + 2 classes), anchor-major
    const float transposed[] = {
        1, 2, 3, 4, 5, 6,
        7, 8, 9, 10, 11, 12,
        13, 14, 15, 16, 17, 18,
    };
    std::vector<float> channelMajor = YoloPostProcess::ToChannelMajor(transposed, 3, 6);
    REQUIRE(channelMajor.size() == 18);
    REQUIRE(channelMajor[0] == 1.0f);   // channel 0, anchor 0
    REQUIRE(channelMajor[1] == 7.0f);   // channel 0, anchor 1
    REQUIRE(channelMajor[2] == 13.0f);  // channel 0, anchor 2
    REQUIRE(channelMajor[3 * 5 + 2] == 18.0f); // channel 5, anchor 2
}

TEST_CASE("DFL decoding takes class scores that are already probabilities as they are, and applies a sigmoid only to logits", "[YoloPostProcess]") {
    const int regMax = 2;
    // box[4*regMax, 1, 1]: bin 0 vs bin 1 logits per side, mass on bin 1
    const float box[8] = { 0, 20, 0, 20, 0, 20, 0, 20 };
    std::vector<std::string> labels = {"a", "b"};
    auto run = [&](float cls0, float cls1, float confThreshold) {
        const float cls[2] = { cls0, cls1 };
        YoloPostProcess::DflScaleOutput scale{ box, cls, 1, 1, 32 };
        return YoloPostProcess::DecodeDflMultiScaleAndNms({ scale }, regMax, 2, labels, IdentityLetterbox(640, 640), confThreshold, 0.45f);
    };

    // sigmoided head output (the rknn_model_zoo export): 0.9 stays 0.9, and a 0.02 background cell is not promoted to 0.5
    auto probabilities = run(0.9f, 0.1f, 0.5f);
    REQUIRE(probabilities.size() == 1);
    REQUIRE(probabilities[0].GetConfidence() == Catch::Approx(0.9).margin(0.001));
    REQUIRE(run(0.02f, 0.01f, 0.25f).empty());

    // raw logits: sigmoid(2.2) ~ 0.9, sigmoid(-4) ~ 0.02
    auto logits = run(2.2f, -4.0f, 0.5f);
    REQUIRE(logits.size() == 1);
    REQUIRE(logits[0].GetConfidence() == Catch::Approx(0.9).margin(0.01));
    REQUIRE(run(-4.0f, -5.0f, 0.25f).empty());
}
