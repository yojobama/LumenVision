#include "ApriltagDetection.h"

ApriltagDetection::ApriltagDetection(apriltag_detection_t detection, apriltag_pose_t pose)
    : m_pFamily(detection.family), m_pDetection(new apriltag_detection_t(detection)), m_Pose(pose)  {}

std::string ApriltagDetection::ToString()
{
    std::ostringstream oss;
    oss << "{";

    // Detection parameters
    oss << "\"detection\":{";
    oss << "\"family\":\"" << (m_pFamily ? m_pFamily->name : "null") << "\",";
    oss << "\"id\":" << (m_pDetection ? m_pDetection->id : -1) << ",";
    oss << "\"hamming\":" << (m_pDetection ? m_pDetection->hamming : -1) << ",";
    oss << "\"decision_margin\":" << (m_pDetection ? m_pDetection->decision_margin : 0.0) << ",";
    oss << "\"center\":[";
    if (m_pDetection) {
        oss << m_pDetection->c[0] << "," << m_pDetection->c[1];
    } else {
        oss << "0,0";
    }
    oss << "],";
    oss << "\"corners\":[";
    if (m_pDetection) {
        for (int i = 0; i < 4; ++i) {
            oss << "[" << m_pDetection->p[i][0] << "," << m_pDetection->p[i][1] << "]";
            if (i < 3) oss << ",";
        }
    }
    oss << "],";

    // Pose parameters
    oss << "\"pose\":{";
    // Rotation matrix
    oss << "\"R\":[";
    if (m_Pose.R && m_Pose.R->data && m_Pose.R->nrows == 3 && m_Pose.R->ncols == 3) {
        for (unsigned int i = 0; i < 3; ++i) {
            oss << "[";
            for (unsigned int j = 0; j < 3; ++j) {
                oss << m_Pose.R->data[i * 3 + j];
                if (j < 2) oss << ",";
            }
            oss << "]";
            if (i < 2) oss << ",";
        }
    }
    oss << "],";
    // Translation vector
    oss << "\"t\":[";
    if (m_Pose.t && m_Pose.t->data && m_Pose.t->nrows == 3 && m_Pose.t->ncols == 1) {
        for (unsigned int i = 0; i < 3; ++i) {
            oss << m_Pose.t->data[i];
            if (i < 2) oss << ",";
        }
    }
    oss << "]";
    oss << "}"; // close pose

    oss << "}"; // close detection

    oss << "}"; // close root

    return oss.str();
}
