#include "ObjectDetection.h"

/// <summary>Returns the object detection as a JSON string.</summary>
string ObjectDetection::ToString()
{
	return "{\"class_id\": " + std::to_string(m_ClassId) +
		   ", \"class_name\": \"" + m_ClassName + "\"" +
		   ", \"confidence\": " + std::to_string(m_Confidence) +
		   ", \"bbox\": {\"x\": " + std::to_string(m_BoundingBox.x) +
		   ", \"y\": " + std::to_string(m_BoundingBox.y) +
		   ", \"width\": " + std::to_string(m_BoundingBox.width) +
		", \"height\": " + std::to_string(m_BoundingBox.height) + "}}";
}
