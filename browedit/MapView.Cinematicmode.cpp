#include <Windows.h>
#include "MapView.h"
#include <browedit/BrowEdit.h>
#include <browedit/shaders/SimpleShader.h>
#include <browedit/gl/FBO.h>
#include <browedit/Map.h>
#include <browedit/components/Rsw.h>
#include <browedit/components/Gnd.h>
#include <browedit/components/GndRenderer.h>
#include <browedit/Node.h>
#include <browedit/actions/GroupAction.h>
#include <browedit/math/HermiteCurve.h>
#include <browedit/CinematicDraw.h>
#include <glm/gtc/type_ptr.hpp>
#include <GLFW/glfw3.h>

extern float timeSelected;
extern int selectedKeyFrame;
extern int selectedTrack;


void MapView::postRenderCinematicMode(BrowEdit* browEdit)
{
	if (cinematicPlay)
		return;
	auto rsw = map->rootNode->getComponent<Rsw>();
	if (rsw->cinematicTracks.size() == 0)
		return;
	auto gnd = map->rootNode->getComponent<Gnd>();
	auto gndRenderer = map->rootNode->getComponent<GndRenderer>();

	glUseProgram(0);
	glMatrixMode(GL_PROJECTION);
	glLoadMatrixf(glm::value_ptr(nodeRenderContext.projectionMatrix));
	glMatrixMode(GL_MODELVIEW);
	glLoadMatrixf(glm::value_ptr(nodeRenderContext.viewMatrix));
	glColor3f(1, 0, 0);
	fbo->bind();
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glDisable(GL_TEXTURE_2D);

	simpleShader->use();
	simpleShader->setUniform(SimpleShader::Uniforms::projectionMatrix, nodeRenderContext.projectionMatrix);
	simpleShader->setUniform(SimpleShader::Uniforms::viewMatrix, nodeRenderContext.viewMatrix);
	simpleShader->setUniform(SimpleShader::Uniforms::modelMatrix, glm::mat4(1.0f));
	simpleShader->setUniform(SimpleShader::Uniforms::textureFac, 0.0f);
	glEnable(GL_BLEND);


	auto mouse3D = gnd->rayCast(mouseRay, browEdit->config.viewEmptyTiles);
	glm::ivec2 tileHovered((int)glm::floor(mouse3D.x / 10), (gnd->height - (int)glm::floor(mouse3D.z) / 10));

	ImGui::Begin("Statusbar");
	ImGui::SetNextItemWidth(100.0f);
	ImGui::Text("Cursor: %d,%d", tileHovered.x, tileHovered.y);
	ImGui::SameLine();
	ImGui::End();

	std::vector<VertexP3T2N3> verts;
	for (float t = 0; t < 20; t += 0.05f)
	{
		auto beforePos = (Rsw::KeyFrameData<std::pair<glm::vec3, glm::vec3>>*)rsw->cinematicTracks[0].getBeforeFrame(t);
		auto afterPos = (Rsw::KeyFrameData<std::pair<glm::vec3, glm::vec3>>*)rsw->cinematicTracks[0].getAfterFrame(t);
		//assert(beforePos->time <= time);

		float frameTime = afterPos->time - beforePos->time;
		float mixFactor = (t - beforePos->time) / frameTime;

		//TODO: cache this!!!
		float segmentLength = math::HermiteCurve::getLength(beforePos->data.first, beforePos->data.second, afterPos->data.first, afterPos->data.second);
		glm::vec3 pos = math::HermiteCurve::getPointAtDistance(beforePos->data.first, beforePos->data.second, afterPos->data.first, afterPos->data.second, mixFactor * segmentLength);
		verts.push_back(VertexP3T2N3(pos, glm::vec2(0), glm::vec3(1,1,1)));
	}

	{
		auto beforePos = (Rsw::KeyFrameData<std::pair<glm::vec3, glm::vec3>>*)rsw->cinematicTracks[0].getBeforeFrame(timeSelected);
		auto afterPos = (Rsw::KeyFrameData<std::pair<glm::vec3, glm::vec3>>*)rsw->cinematicTracks[0].getAfterFrame(timeSelected);
		float frameTime = afterPos->time - beforePos->time;
		float mixFactor = (timeSelected - beforePos->time) / frameTime;
		float segmentLength = math::HermiteCurve::getLength(beforePos->data.first, beforePos->data.second, afterPos->data.first, afterPos->data.second);
		glm::vec3 pos = math::HermiteCurve::getPointAtDistance(beforePos->data.first, beforePos->data.second, afterPos->data.first, afterPos->data.second, mixFactor * segmentLength);

		for (float x = -1; x <= 1; x += 0.1f)
			for (float y = -1; y <= 1; y += 0.1f)
				for (float z = -1; z <= 1; z += 0.1f)
					verts.push_back(VertexP3T2N3(pos+glm::vec3(x,y,z), glm::vec2(0), glm::vec3(1, 1, 1)));

	}

	if (verts.size() > 0)
	{
		glEnableVertexAttribArray(0);
		glEnableVertexAttribArray(1);
		glEnableVertexAttribArray(2);
		glDisableVertexAttribArray(3);
		glDisableVertexAttribArray(4); //TODO: vao
		glVertexAttribPointer(0, 3, GL_FLOAT, false, sizeof(VertexP3T2N3), verts[0].data);
		glVertexAttribPointer(1, 2, GL_FLOAT, false, sizeof(VertexP3T2N3), verts[0].data + 3);
		glVertexAttribPointer(2, 3, GL_FLOAT, false, sizeof(VertexP3T2N3), verts[0].data + 5);

		simpleShader->setUniform(SimpleShader::Uniforms::color, glm::vec4(0, 1, 0, 1));
		simpleShader->setUniform(SimpleShader::Uniforms::textureFac, 0.0f);
		simpleShader->setUniform(SimpleShader::Uniforms::lightMin, 1.0f);
		glPointSize(4);
		glDrawArrays(GL_POINTS, 0, (int)verts.size());
	}

	Gadget::setMatrices(nodeRenderContext.projectionMatrix, nodeRenderContext.viewMatrix);
	bool canSelectObject = !cinedraw::active;
	float gridSize = gridSizeTranslate;
	float gridOffset = gridOffsetTranslate;
	static glm::vec3 originalPos;
	static std::map<Rsw::KeyFrame*, Gadget> gadgetMap;
	int frameIndex = 0;
	for (auto f : rsw->cinematicTracks[0].frames)
	{
		if (gadgetMap.find(f) == gadgetMap.end())
			gadgetMap[f] = Gadget();
		auto& g = gadgetMap[f];
		auto frame = dynamic_cast<Rsw::KeyFrameData<std::pair<glm::vec3, glm::vec3>>*>(f);
		glm::vec3 &pos = frame->data.first;
		glm::vec3& direction = frame->data.second;

		//if (frameIndex == selectedKeyFrame && selectedTrack == 0)
		{
			glm::mat4 mat = glm::translate(glm::mat4(1.0f), pos);
			mat = glm::scale(mat, glm::vec3(0.25f, 0.25f, -0.25f));
			mat = glm::scale(mat, glm::vec3(1, 1, glm::length(direction) / 100.0f));
			mat = mat * glm::toMat4(util::RotationBetweenVectors(direction, glm::vec3(0,0,1)));
			mat = glm::rotate(mat, glm::radians(90.0f), glm::vec3(-1, 0, 0));
			simpleShader->use();
			simpleShader->setUniform(SimpleShader::Uniforms::modelMatrix, mat);
			Gadget::arrowMesh.draw();
		}


		//glm::mat4 mat = glm::scale(glm::mat4(1.0f), glm::vec3(1, 1, -1));
		glm::mat4 mat = glm::translate(glm::mat4(1.0f), pos);
		g.mode = Gadget::Mode::Translate;
		g.draw(mouseRay, mat);
		if (hovered)
		{
			if (g.axisClicked && canSelectObject)
			{
				selectedKeyFrame = frameIndex;
				selectedTrack = 0;
				mouseDragPlane.normal = glm::normalize(glm::vec3(nodeRenderContext.viewMatrix * glm::vec4(0, 0, 1, 1)) - glm::vec3(nodeRenderContext.viewMatrix * glm::vec4(0, 0, 0, 1))) * glm::vec3(1, 1, -1);
				if (g.selectedAxis == Gadget::Axis::X)
					mouseDragPlane.normal.x = 0;
				if (g.selectedAxis == Gadget::Axis::Y)
					mouseDragPlane.normal.y = 0;
				if (g.selectedAxis == Gadget::Axis::Z)
					mouseDragPlane.normal.z = 0;
				mouseDragPlane.normal = glm::normalize(mouseDragPlane.normal);
				mouseDragPlane.D = -glm::dot(pos, mouseDragPlane.normal);

				float f;
				mouseRay.planeIntersection(mouseDragPlane, f);
				mouseDragStart = mouseRay.origin + f * mouseRay.dir;
				mouseDragStart2D = mouseState.position;
				canSelectObject = false;
				originalPos = pos;
			}
			if (g.axisReleased && canSelectObject)
			{
			}
			if (g.axisDragged && canSelectObject)
			{
				float f;
				mouseRay.planeIntersection(mouseDragPlane, f);
				glm::vec3 mouseOffset = (mouseRay.origin + f * mouseRay.dir) - mouseDragStart;

				if ((g.selectedAxis & Gadget::X) == 0)
					mouseOffset.x = 0;
				if ((g.selectedAxis & Gadget::Y) == 0)
					mouseOffset.y = 0;
				if ((g.selectedAxis & Gadget::Z) == 0)
					mouseOffset.z = 0;

				bool snap = snapToGrid;
				if (ImGui::GetIO().KeyShift)
					snap = !snap;
				if (snap && gridLocal)
					mouseOffset = glm::round(mouseOffset / (float)gridSize) * (float)gridSize;

				float mouseOffset2D = glm::length(mouseDragStart2D - mouseState.position);
				if (snap && gridLocal)
					mouseOffset2D = glm::round(mouseOffset2D / (float)gridSize) * (float)gridSize;
				float poss = glm::sign(mouseState.position.x - mouseDragStart2D.x);
				if (poss == 0)
					poss = 1;

				ImGui::Begin("Statusbar");
				ImGui::SetNextItemWidth(200.0f);
				if (g.mode == Gadget::Mode::Translate || g.mode == Gadget::Mode::Scale)
					ImGui::InputFloat3("Drag Offset", glm::value_ptr(mouseOffset));
				else
					ImGui::Text(std::to_string(poss * mouseOffset2D).c_str());
				ImGui::SameLine();
				ImGui::End();

				if (g.mode == Gadget::Mode::Translate)
				{
					pos = originalPos + mouseOffset;
					if (snap && !gridLocal)
						pos[g.selectedAxisIndex()] = glm::round((pos[g.selectedAxisIndex()] - gridOffset) / (float)gridSize) * (float)gridSize + gridOffset;
				}
				canSelectObject = false;
			}
		}
		frameIndex++;
	}


	// ---- drawn camera path -------------------------------------------------------------------------
	if (cinedraw::showPanel && cinedraw::points.size() > 0)
	{
		auto drawVerts = [&](const std::vector<glm::vec3>& pts, GLenum mode, glm::vec4 color)
		{
			if (pts.empty()) return;
			std::vector<VertexP3T2N3> v;
			for (auto& p : pts) v.push_back(VertexP3T2N3(p, glm::vec2(0), glm::vec3(1, 1, 1)));
			glEnableVertexAttribArray(0); glEnableVertexAttribArray(1); glEnableVertexAttribArray(2);
			glDisableVertexAttribArray(3); glDisableVertexAttribArray(4);
			glVertexAttribPointer(0, 3, GL_FLOAT, false, sizeof(VertexP3T2N3), v[0].data);
			glVertexAttribPointer(1, 2, GL_FLOAT, false, sizeof(VertexP3T2N3), v[0].data + 3);
			glVertexAttribPointer(2, 3, GL_FLOAT, false, sizeof(VertexP3T2N3), v[0].data + 5);
			simpleShader->setUniform(SimpleShader::Uniforms::color, color);
			simpleShader->setUniform(SimpleShader::Uniforms::textureFac, 0.0f);
			simpleShader->setUniform(SimpleShader::Uniforms::lightMin, 1.0f);
			glDrawArrays(mode, 0, (int)v.size());
		};
		simpleShader->use();
		simpleShader->setUniform(SimpleShader::Uniforms::modelMatrix, glm::mat4(1.0f));
		glDisable(GL_DEPTH_TEST);
		glLineWidth(2.0f);
		auto line = cinedraw::curve(12, cinedraw::closedLoop && cinedraw::points.size() >= 3);
		drawVerts(line, GL_LINE_STRIP, glm::vec4(0.2f, 0.9f, 1.0f, 1.0f));
		std::vector<glm::vec3> stems, cams, grounds, focusLines;
		for (size_t i = 0; i < cinedraw::points.size(); i++)
		{
			auto& p = cinedraw::points[i];
			glm::vec3 cam(p.pos.x, cinedraw::camHeight(p), p.pos.z);
			stems.push_back(p.pos); stems.push_back(cam);
			if (p.hasFocus) { focusLines.push_back(cam); focusLines.push_back(p.focus); }
		}
		drawVerts(stems, GL_LINES, glm::vec4(1.0f, 1.0f, 0.3f, 1.0f));
		drawVerts(focusLines, GL_LINES, glm::vec4(1.0f, 0.4f, 1.0f, 1.0f));
		glPointSize(9.0f);
		for (size_t i = 0; i < cinedraw::points.size(); i++)
		{
			auto& p = cinedraw::points[i];
			bool sel = (int)i == cinedraw::selected;
			drawVerts({ glm::vec3(p.pos.x, cinedraw::camHeight(p), p.pos.z) }, GL_POINTS, sel ? glm::vec4(1, 0.2f, 0.2f, 1) : glm::vec4(0.3f, 1, 0.3f, 1));
			if (p.hasFocus) drawVerts({ p.focus }, GL_POINTS, glm::vec4(1.0f, 0.4f, 1.0f, 1.0f));
		}
		glEnable(GL_DEPTH_TEST);
		glLineWidth(1.0f);
	}
	if (cinedraw::active && hovered && !ImGui::GetIO().KeyShift && mouse3D.x < 1e30f)
	{
		using namespace cinedraw;
		auto pickDist = [&](const glm::vec3& p)
		{
			float d = glm::length(glm::cross(mouseRay.dir, p - mouseRay.origin));
			return d - glm::max(8.0f, 0.015f * glm::distance(mouseRay.origin, p));
		};
		auto nearest = [&]()
		{
			int best = -1; float bd = 0;
			for (size_t i = 0; i < points.size(); i++)
			{
				float d = glm::min(pickDist(glm::vec3(points[i].pos.x, camHeight(points[i]), points[i].pos.z)), pickDist(points[i].pos));
				if (d < bd) { bd = d; best = (int)i; }
			}
			return best;
		};
		if (ImGui::IsMouseClicked(0))
		{
			if (pickFocus && selected >= 0 && selected < (int)points.size())
			{
				points[selected].focus = mouse3D + glm::vec3(0, 8, 0);
				points[selected].hasFocus = true;
				pickFocus = false;
				dirty = true;
			}
			else
			{
				int hit = nearest();
				if (hit >= 0) { selected = hit; dragging = hit; }
				else
				{
					Point np;
					np.pos = mouse3D;
					if (ImGui::GetIO().KeyAlt && selected >= 0 && selected < (int)points.size())
					{
						points.insert(points.begin() + selected + 1, np);
						selected++;
					}
					else
					{
						points.push_back(np);
						selected = (int)points.size() - 1;
					}
					dirty = true;
				}
			}
		}
		if (dragging >= 0 && dragging < (int)points.size() && ImGui::IsMouseDown(0) && ImGui::GetIO().MouseDelta.x != 0)
		{
			points[dragging].pos = mouse3D;
			dirty = true;
		}
		if (ImGui::IsMouseReleased(0))
			dragging = -1;
		if (ImGui::IsMouseReleased(1) && ImGui::GetIO().MouseDragMaxDistanceSqr[1] < 5)
		{
			int hit = nearest();
			if (hit >= 0) { points.erase(points.begin() + hit); selected = -1; dirty = true; }
		}
		if (ImGui::IsKeyPressed(GLFW_KEY_ENTER) && !ImGui::GetIO().WantTextInput)
			active = false;
	}


	fbo->unbind();
}