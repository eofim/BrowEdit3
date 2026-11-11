#pragma once
// A camera path the user draws on the map (shared by the viewport tool and the Cinematic Path window).
#include <glm/glm.hpp>
#include <cmath>
#include <vector>

namespace cinedraw
{
	struct Point
	{
		glm::vec3 pos = glm::vec3(0);	// on the ground
		float height = -1;				// camera height above the ground at this point, < 0 = use the default
		int look = -1;					// -1 default, 0 ahead, 1 left, 2 right, 3 back
		bool hasFocus = false;			// look at "focus" while passing this point
		glm::vec3 focus = glm::vec3(0);
	};

	inline std::vector<Point> points;
	inline bool showPanel = false;
	inline bool active = false;			// clicking on the map edits the path
	inline bool pickFocus = false;		// the next click on the map sets the focus point of the selected point
	inline int selected = -1;
	inline int dragging = -1;
	inline bool dirty = false;

	inline float defaultHeight = 30.0f;
	inline float duration = 40.0f;
	inline float pitchDeg = 14.0f;
	inline float pacing = 60.0f;
	inline float keysPerSec = 0.8f;
	inline int defLook = 0;				// 0 ahead, 1 left, 2 right, 3 back, 5 slow pan
	inline bool slowCorners = true;
	inline bool closedLoop = false;
	inline bool autoUpdate = false;

	inline float camHeight(const Point& p) { return p.pos.y + (p.height >= 0 ? p.height : defaultHeight); }

	// smooth curve through the points: x, camera altitude, z. Directions are unit vectors scaled by the segment length,
	// so uneven spacing does not overshoot. ctrl receives the index of every control point inside the returned list.
	inline std::vector<glm::vec3> curve(int perSeg, bool closed, std::vector<int>* ctrl = nullptr)
	{
		std::vector<glm::vec3> out;
		const int n = (int)points.size();
		if (n < 2) return out;
		auto P = [&](int i) { return glm::vec3(points[i].pos.x, camHeight(points[i]), points[i].pos.z); };
		auto idx = [&](int i) { return closed ? ((i % n) + n) % n : glm::clamp(i, 0, n - 1); };
		auto dirAt = [&](int i)
		{
			glm::vec3 d = P(idx(i + 1)) - P(idx(i - 1));
			float l = glm::length(d);
			return l > 0.0001f ? d / l : glm::vec3(1, 0, 0);
		};
		const int segs = closed ? n : n - 1;
		for (int i = 0; i < segs; i++)
		{
			glm::vec3 p1 = P(i), p2 = P(idx(i + 1));
			float L = glm::length(p2 - p1);
			glm::vec3 m1 = dirAt(i) * L, m2 = dirAt(i + 1) * L;
			if (ctrl) ctrl->push_back((int)out.size());
			for (int k = 0; k < perSeg; k++)
			{
				float t = k / (float)perSeg, t2 = t * t, t3 = t2 * t;
				out.push_back((2 * t3 - 3 * t2 + 1) * p1 + (t3 - 2 * t2 + t) * m1 + (-2 * t3 + 3 * t2) * p2 + (t3 - t2) * m2);
			}
		}
		if (!closed && ctrl) ctrl->push_back((int)out.size());
		out.push_back(closed ? P(0) : P(n - 1));
		return out;
	}
}
