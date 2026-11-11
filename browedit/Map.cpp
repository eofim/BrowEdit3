#include <browedit/sprite/SpriteSystem.h>
#include <browedit/advtex/AdvTex.h>
#include <browedit/CliffTool.h>
#include <Windows.h>
#include <shellapi.h>
#include <browedit/Map.h>
#include <browedit/Node.h>
#include <browedit/actions/Action.h>
#include <browedit/actions/ObjectChangeAction.h>
#include <browedit/actions/GroupAction.h>
#include <browedit/actions/SelectAction.h>
#include <browedit/actions/DeleteObjectAction.h>
#include <browedit/actions/TileSelectAction.h>
#include <browedit/actions/TileNewAction.h>
#include <browedit/actions/CubeTileChangeAction.h>
#include <browedit/actions/TilePropertyChangeAction.h>
#include <browedit/actions/GatTileChangeAction.h>
#include <browedit/components/Rsw.h>
#include <browedit/components/RsmRenderer.h>
#include <browedit/components/Gnd.h>
#include <browedit/components/Gat.h>
#include <browedit/components/LubSkyMap.h>
#include <browedit/components/SkyMapRenderer.h>
#include <browedit/components/GndRenderer.h>
#include <browedit/components/BillboardRenderer.h>
#include <browedit/BrowEdit.h>
#include <browedit/util/ResourceManager.h>
#include <filesystem>
#include <fstream>

#include <json.hpp>
using json = nlohmann::json;

#include <stb/stb_image_write.h>
#include <stb/stb_image.h>
#include <set>
#include <array>
#include <cmath>
#include <limits>
#include <unordered_map>
#include <map>
#include <set>
#include <browedit/actions/AutoGatFootprintAction.h>
#include <iostream>
#include <thread>
#include <mutex>


Map::Map(const std::string& name, BrowEdit* browEdit) : name(name)
{
	rootNode = new Node(name);
	auto rsw = new Rsw();
	rootNode->addComponent(rsw);	
	rsw->load(name, this, browEdit);
	auto lubSkyMap = new LubSkyMap();
	rootNode->addComponent(lubSkyMap);
	lubSkyMap->load(rsw->rswMapName);
	rootNode->addComponent(new SkyMapRenderer());
	changed = false;
}

Map::Map(const std::string& name, int width, int height, BrowEdit* browEdit) : name(name)
{
	rootNode = new Node(name);
	auto rsw = new Rsw();
	rootNode->addComponent(rsw);
	rsw->newMap(name, width, height, this, browEdit);
	auto lubSkyMap = new LubSkyMap();
	rootNode->addComponent(lubSkyMap);
	lubSkyMap->load(rsw->rswMapName);
	rootNode->addComponent(new SkyMapRenderer());
	changed = true;
}


Map::~Map()
{
	sprite::System::instance().forget(this);
	advtex::forget(this);
	cliff::forgetCave(this);
	for (auto u : undoStack)
		delete u;
	for (auto u : redoStack)
		delete u;
	delete rootNode;
}


std::vector<glm::ivec2> Map::getSelectionAroundTiles()
{
	auto gnd = rootNode->getComponent<Gnd>();
	std::vector<glm::ivec2> tilesAround;
	auto isTileSelected = [&](int x, int y) { return std::find(tileSelection.begin(), tileSelection.end(), glm::ivec2(x, y)) != tileSelection.end(); };

	for (auto& t : tileSelection)
		for (int xx = -1; xx <= 1; xx++)
			for (int yy = -1; yy <= 1; yy++)
				if (gnd->inMap(t + glm::ivec2(xx, yy)) &&
					std::find(tilesAround.begin(), tilesAround.end(), glm::ivec2(t.x + xx, t.y + yy)) == tilesAround.end() &&
					!isTileSelected(t.x + xx, t.y + yy))
					tilesAround.push_back(t + glm::ivec2(xx, yy));
	return tilesAround;
}

Node* Map::findAndBuildNode(const std::string& path, Node* root)
{
	if (!root)
		root = rootNode;
	if (path == "")
		return root;
	std::string firstPart = path;
	std::string secondPart = "";
	if (firstPart.find("\\") != std::string::npos)
	{
		firstPart = firstPart.substr(0, firstPart.find("\\"));
		secondPart = path.substr(path.find("\\") + 1);
	}
	for (auto c : root->children)
		if (c->name == firstPart)
			return findAndBuildNode(secondPart, c);
	Node* node = new Node(firstPart, root);
	return findAndBuildNode(secondPart, node);
}


void Map::doAction(Action* action, BrowEdit* browEdit)
{
	if (tempGroupAction)
	{
		tempGroupAction->addAction(action);
		return;
	}

	auto ga = dynamic_cast<GroupAction*>(action);
	if (ga && ga->isEmpty())
		return;

	if (std::find(undoStack.begin(), undoStack.end(), action) != undoStack.end())
	{
		std::cerr << "Double undo!" << std::endl;
		throw "Oops";
		return;
	}

	changed = true;
	action->perform(this, browEdit);
	undoStack.push_back(action);

	for (auto a : redoStack)
		delete a;
	redoStack.clear();
}

void Map::redo(BrowEdit* browEdit)
{
	if (redoStack.size() > 0)
	{
		redoStack.front()->perform(this, browEdit);
		undoStack.push_back(redoStack.front());
		redoStack.erase(redoStack.begin());
	}
}
void Map::beginGroupAction(const std::string &title)
{
	assert(!tempGroupAction);
	tempGroupAction = new GroupAction(title);
}
void Map::endGroupAction(BrowEdit* browEdit)
{
	assert(tempGroupAction);
	auto action = tempGroupAction;
	tempGroupAction = nullptr;
	doAction(action, browEdit);
}
void Map::undo(BrowEdit* browEdit)
{
	if (undoStack.size() > 0)
	{
		undoStack.back()->undo(this, browEdit);
		redoStack.insert(redoStack.begin(), undoStack.back());
		undoStack.pop_back();
	}
}



glm::vec3 Map::getSelectionCenter()
{
	int count = 0;
	glm::vec3 center(0.0f);
	for (auto n : selectedNodes)
		if (n->getComponent<RswObject>())
		{
			center += n->getComponent<RswObject>()->position;
			count++;
		}
	center /= glm::max(1, count);
	return center;
}


void Map::invertScale(int axis, BrowEdit* browEdit)
{
	auto ga = new GroupAction();
	for (auto n : selectedNodes)
	{
		auto rswObject = n->getComponent<RswObject>();
		auto rsmRenderer = n->getComponent<RsmRenderer>();
		if (rswObject)
		{
			float orig = rswObject->scale[axis];
			rswObject->scale[axis] = -rswObject->scale[axis];
			ga->addAction(new ObjectChangeAction(n, &rswObject->scale[axis], orig, "Invert Scale"));
		}
		if (rsmRenderer)
			rsmRenderer->setDirty();
	}
	autoGatFollow(selectedNodes, browEdit, ga);
	doAction(ga, browEdit);
}
void Map::nudgeSelection(int axis, int sign, BrowEdit* browEdit)
{
	float distance = browEdit->useGridForNudge ? browEdit->activeMapView->gridSizeTranslate : browEdit->nudgeDistance;
	auto ga = new GroupAction();
	for (auto n : selectedNodes)
	{
		auto rswObject = n->getComponent<RswObject>();
		auto rsmRenderer = n->getComponent<RsmRenderer>();
		if (rswObject)
		{
			float orig = rswObject->position[axis];
			rswObject->position[axis] += sign * distance;
			ga->addAction(new ObjectChangeAction(n, &rswObject->position[axis], orig, "Nudge"));
		}
		if (rsmRenderer)
			rsmRenderer->setDirty();
	}
	autoGatFollow(selectedNodes, browEdit, ga);
	doAction(ga, browEdit);
}

void Map::rotateSelection(int axis, int sign, BrowEdit* browEdit)
{
	float distance = browEdit->useGridForNudge ? browEdit->activeMapView->gridSizeRotate : browEdit->rotateDistance;
	auto ga = new GroupAction();
	glm::vec3 groupCenter = getSelectionCenter();

	for (auto n : selectedNodes)
	{
		auto rswObject = n->getComponent<RswObject>();
		auto rsmRenderer = n->getComponent<RsmRenderer>();
		if (rswObject)
		{
			float orig = rswObject->rotation[axis];
			rswObject->rotation[axis] += sign * distance;
			ga->addAction(new ObjectChangeAction(n, &rswObject->rotation[axis], orig, "Rotate"));
			if (browEdit->activeMapView->pivotPoint == MapView::PivotPoint::GroupCenter)
			{
				auto originalPos = rswObject->position;
				float originalAngle = atan2(rswObject->position.z - groupCenter.z, rswObject->position.x - groupCenter.x);
				float dist = glm::length(glm::vec2(rswObject->position.z - groupCenter.z, rswObject->position.x - groupCenter.x));
				originalAngle -= glm::radians(sign * browEdit->rotateDistance);

				rswObject->position.x = groupCenter.x + dist * glm::cos(originalAngle);
				rswObject->position.z = groupCenter.z + dist * glm::sin(originalAngle);
				ga->addAction(new ObjectChangeAction(n, &rswObject->position, originalPos, "Translate"));
			}
		}
		if (rsmRenderer)
			rsmRenderer->setDirty();
	}
	autoGatFollow(selectedNodes, browEdit, ga);
	doAction(ga, browEdit);

}

void Map::randomRotateSelection(int axis, BrowEdit* browEdit)
{
	auto ga = new GroupAction();
	glm::vec3 groupCenter = getSelectionCenter();

	for (auto n : selectedNodes)
	{
		auto rswObject = n->getComponent<RswObject>();
		auto rsmRenderer = n->getComponent<RsmRenderer>();
		if (rswObject)
		{
			float orig = rswObject->rotation[axis];
			rswObject->rotation[axis] = 360 * (rand() / (float)RAND_MAX);
			ga->addAction(new ObjectChangeAction(n, &rswObject->rotation[axis], orig, "Rotate"));
		}
		if (rsmRenderer)
			rsmRenderer->setDirty();
	}
	autoGatFollow(selectedNodes, browEdit, ga);
	doAction(ga, browEdit);

}

void Map::flipSelection(int axis, BrowEdit* browEdit)
{
	glm::vec3 center = getSelectionCenter();
	auto ga = new GroupAction();
	for (auto n : selectedNodes)
	{
		auto rswObject = n->getComponent<RswObject>();
		auto rsmRenderer = n->getComponent<RsmRenderer>();
		if (rswObject)
		{
			float orig = rswObject->position[axis];
			rswObject->position[axis] = center[axis] - (rswObject->position[axis] - center[axis]);
			ga->addAction(new ObjectChangeAction(n, &rswObject->position[axis], orig, "Moving"));
		}
		if (rsmRenderer)
			rsmRenderer->setDirty();
	}
	autoGatFollow(selectedNodes, browEdit, ga);
	doAction(ga, browEdit);
}

void Map::setSelectedItemsToFloorHeight(BrowEdit* browEdit)
{
	auto gnd = rootNode->getComponent<Gnd>();
	auto ga = new GroupAction();
	for (auto n : selectedNodes)
	{
		auto rswObject = n->getComponent<RswObject>();
		if (rswObject)
		{
			glm::vec3 position(5 * gnd->width + rswObject->position.x, -rswObject->position.y, 5 * gnd->height - rswObject->position.z + 10);

			glm::vec3 pos = gnd->rayCast(math::Ray(position + glm::vec3(0, 1000, 0), glm::vec3(0, -1, 0)));
			if (pos != glm::vec3(std::numeric_limits<float>().max()))
			{
				float orig = rswObject->position.y;
				rswObject->position.y = -pos.y;
				ga->addAction(new ObjectChangeAction(n, &rswObject->position.y, orig, "Moving"));
				if (n->getComponent<RsmRenderer>())
					n->getComponent<RsmRenderer>()->setDirty();
			}
		}
	}
	autoGatFollow(selectedNodes, browEdit, ga);
	doAction(ga, browEdit);

}


void Map::selectNear(float nearDistance, BrowEdit* browEdit)
{
	auto selection = selectedNodes;
	auto ga = new GroupAction();
	rootNode->traverse([&](Node* n) {
		auto rswObject = n->getComponent<RswObject>();
		if (!rswObject)
			return;
		auto distance = 999999999.0f;
		for (auto nn : selection)
			if (n != nn && glm::distance(nn->getComponent<RswObject>()->position, rswObject->position) < distance)
				distance = glm::distance(nn->getComponent<RswObject>()->position, rswObject->position);
		if (distance < nearDistance)
		{
			auto sa = new SelectAction(this, n, true, false);
			sa->perform(this, browEdit);
			ga->addAction(sa);
		}
		});
	doAction(ga, browEdit);
}


void Map::selectSameModels(BrowEdit* browEdit)
{
	std::set<std::string> files;
	for (auto n : selectedNodes)
	{
		auto rswModel = n->getComponent<RswModel>();
		if (rswModel)
			files.insert(rswModel->fileName);
	}
	auto ga = new GroupAction();
	rootNode->traverse([&](Node* n) {
		if (std::find(selectedNodes.begin(), selectedNodes.end(), n) != selectedNodes.end())
			return;
		auto rswModel = n->getComponent<RswModel>();
		if (rswModel && std::find(files.begin(), files.end(), rswModel->fileName) != files.end())
		{
			auto sa = new SelectAction(this, n, true, false);
			sa->perform(this, browEdit);
			ga->addAction(sa);
		}
		});
	doAction(ga, browEdit);
}

template<class T>
void Map::selectAll(BrowEdit* browEdit)
{
	std::vector<Node*> newNodes;
	rootNode->traverse([&](Node* n) {
		if (std::find(selectedNodes.begin(), selectedNodes.end(), n) != selectedNodes.end())
			return;
		auto rswObject = n->getComponent<T>();
		if (rswObject)
		{
			newNodes.push_back(n);
		}
		});
	auto sa = new SelectAction(this, newNodes, true, false);
	doAction(sa, browEdit);
}
template void Map::selectAll<RswObject>(BrowEdit* browEdit);
template void Map::selectAll<RswModel>(BrowEdit* browEdit);
template void Map::selectAll<RswSound>(BrowEdit* browEdit);
template void Map::selectAll<RswEffect>(BrowEdit* browEdit);
template void Map::selectAll<RswLight>(BrowEdit* browEdit);

void Map::selectInvert(BrowEdit* browEdit)
{
	auto ga = new GroupAction();
	rootNode->traverse([&](Node* n) {
		bool selected = std::find(selectedNodes.begin(), selectedNodes.end(), n) != selectedNodes.end();

		auto rswObject = n->getComponent<RswObject>();
		if (rswObject)
		{
			auto sa = new SelectAction(this, n, true, selected);
			sa->perform(this, browEdit);
			ga->addAction(sa);
		}
		});
	doAction(ga, browEdit);
}


void Map::deleteSelection(BrowEdit* browEdit)
{
	if (selectedNodes.size() > 0)
	{
		auto ga = new GroupAction();
		for (auto n : selectedNodes)
			ga->addAction(new DeleteObjectAction(n));
		autoGatFollow(selectedNodes, browEdit, ga, true);
		doAction(ga, browEdit);
	}
}

void Map::copySelection()
{
	json clipboard;
	for (auto n : selectedNodes)
		clipboard.push_back(*n);
	ImGui::SetClipboardText(clipboard.dump(1).c_str());
}


void Map::pasteSelection(BrowEdit* browEdit)
{
	try
	{
		auto c = ImGui::GetClipboardText();
		if (c == nullptr)
			return;
		std::string cb = c;
		if (cb == "")
			return;
		json clipboard = json::parse(cb);
		if (clipboard.size() > 0)
		{
			for (auto n : browEdit->newNodes) //TODO: should I do this?
				delete n.first;
			browEdit->newNodes.clear();

			for (auto n : clipboard)
			{
				auto newNode = new Node(n["name"].get<std::string>());
				newNode->addComponentsFromJson(n["components"]);
				glm::vec3 pos(0.0f);
				if (newNode->getComponent<RswObject>())
					pos = newNode->getComponent<RswObject>()->position;
				browEdit->newNodes.push_back(std::pair<Node*, glm::vec3>(newNode, pos));
			}
			glm::vec3 center(0, 0, 0);
			for (auto& n : browEdit->newNodes)
				center += n.second;
			center /= browEdit->newNodes.size();
			
			browEdit->newNodesCenter = center;
			for (auto& n : browEdit->newNodes)
				n.second = n.second - center;
			browEdit->newNodePlacement = BrowEdit::Ground;
		}
	}
	catch (...) {
		std::cerr << "!!!!!!!!!!! Some sort of error occurred???" << std::endl;
	};
}


// ---------------------------------------------------------------------------------------------
// Auto-GAT
// ---------------------------------------------------------------------------------------------
namespace
{
	// Does the 2D triangle overlap the axis aligned rectangle? Touching does not count.
	// Separating axis test, which also works for triangles that are squashed into a line (vertical faces).
	bool triangleOverlapsRect(const glm::vec2* tri, const glm::vec2& rMin, const glm::vec2& rMax)
	{
		for (int axis = 0; axis < 2; axis++)
		{
			float tMin = glm::min(tri[0][axis], glm::min(tri[1][axis], tri[2][axis]));
			float tMax = glm::max(tri[0][axis], glm::max(tri[1][axis], tri[2][axis]));
			if (tMax <= rMin[axis] || tMin >= rMax[axis])
				return false;
		}

		const glm::vec2 corners[4] = { rMin, glm::vec2(rMax.x, rMin.y), glm::vec2(rMin.x, rMax.y), rMax };
		for (int i = 0; i < 3; i++)
		{
			glm::vec2 edge = tri[(i + 1) % 3] - tri[i];
			glm::vec2 normal(-edge.y, edge.x);
			float len = glm::length(normal);
			if (len < 1e-6f)
				continue;
			normal /= len;

			float tMin = std::numeric_limits<float>::max(), tMax = -std::numeric_limits<float>::max();
			float cMin = std::numeric_limits<float>::max(), cMax = -std::numeric_limits<float>::max();
			for (int k = 0; k < 3; k++)
			{
				float d = glm::dot(normal, tri[k]);
				tMin = glm::min(tMin, d);
				tMax = glm::max(tMax, d);
			}
			for (int k = 0; k < 4; k++)
			{
				float d = glm::dot(normal, corners[k]);
				cMin = glm::min(cMin, d);
				cMax = glm::max(cMax, d);
			}
			if (tMax <= cMin || tMin >= cMax)
				return false;
		}
		return true;
	}

	// Collects the faces of a model in world space, the same way RswModelCollider does, but without touching its BVH
	void collectWorldTriangles(Rsm::Mesh* mesh, RsmRenderer* renderer, const glm::mat4& matrix, std::vector<std::array<glm::vec3, 3>>& out)
	{
		if (!mesh || mesh->index < 0 || mesh->index >= (int)renderer->renderInfo.size())
			return;

		glm::mat4 meshMatrix = matrix * renderer->renderInfo[mesh->index].matrix;
		for (const auto& face : mesh->faces)
		{
			std::array<glm::vec3, 3> tri;
			bool valid = true;
			for (int i = 0; i < 3; i++)
			{
				if (face.vertexIds[i] >= mesh->vertices.size())
				{
					valid = false;
					break;
				}
				tri[i] = glm::vec3(meshMatrix * glm::vec4(mesh->vertices[face.vertexIds[i]], 1.0f));
			}
			if (valid)
				out.push_back(tri);
		}

		for (auto child : mesh->children)
			collectWorldTriangles(child, renderer, matrix, out);
	}
}

namespace
{
	// same matrix the renderer builds for a model, so it can be used before the renderer has had a chance to refresh its cache
	glm::mat4 autoGatModelMatrix(Gnd* gnd, RswObject* rswObject, Rsm* rsm)
	{
		glm::mat4 m(1.0f);
		m = glm::scale(m, glm::vec3(1, 1, -1));
		m = glm::translate(m, glm::vec3(5 * gnd->width + rswObject->position.x, -rswObject->position.y, -10 - 5 * gnd->height + rswObject->position.z));
		m = glm::rotate(m, -glm::radians(rswObject->rotation.z), glm::vec3(0, 0, 1));
		m = glm::rotate(m, -glm::radians(rswObject->rotation.x), glm::vec3(1, 0, 0));
		m = glm::rotate(m, glm::radians(rswObject->rotation.y), glm::vec3(0, 1, 0));
		m = glm::scale(m, glm::vec3(rswObject->scale.x, -rswObject->scale.y, rswObject->scale.z));
		if (rsm->version < 0x0202)
			m = glm::translate(m, glm::vec3(-rsm->realbbrange.x, rsm->realbbmin.y, -rsm->realbbrange.z));
		else
			m = glm::scale(m, glm::vec3(1, -1, 1));
		return m;
	}
}

// Keeps the GAT in sync with the models: every model remembers which tiles it blocked (and what they were before),
// so moving, rotating, scaling or deleting a model frees its old tiles and blocks the new ones. Tiles that were edited
// by hand since, or that another model also blocks, are left alone.
void Map::addAutoGatActions(const std::vector<Node*>& nodes, float bandHeight, int defaultGatType, GroupAction* ga, bool removed, bool useMatrixCache, bool fillInterior, int closeGap)
{
	auto gnd = rootNode->getComponent<Gnd>();
	auto gat = rootNode->getComponent<Gat>();
	if (!gnd || !gat || !ga)
		return;

	typedef std::pair<int, int> TileKey;

	// who currently owns which tile (all models in the map)
	std::map<TileKey, std::map<RswModel*, RswModel::AutoGatTile>> owners;
	rootNode->traverse([&](Node* n)
	{
		auto m = n->getComponent<RswModel>();
		if (m)
			for (const auto& t : m->autoGatTiles)
				owners[{t.x, t.y}][m] = t;
	});

	// gat values as they will be once the actions are performed
	std::map<TileKey, int> virtualValue;
	auto valueOf = [&](const TileKey& k) -> int
	{
		auto it = virtualValue.find(k);
		return it != virtualValue.end() ? it->second : gat->cubes[k.first][k.second]->gatType;
	};

	// terrain height below a world position, cached because most vertices are shared between faces
	std::unordered_map<long long, float> terrainCache;
	auto terrainHeightAt = [&](const glm::vec3& p) -> float // NaN when unknown
	{
		long long key = (static_cast<long long>(static_cast<int>(std::lround(p.x * 20.0f))) << 32) |
			static_cast<unsigned int>(static_cast<int>(std::lround(p.z * 20.0f)));
		auto it = terrainCache.find(key);
		if (it != terrainCache.end())
			return it->second;

		float result = std::numeric_limits<float>::quiet_NaN();
		glm::ivec2 tile((int)glm::floor(p.x / 5.0f), gat->height - (int)glm::floor(p.z / 5.0f) + 1);
		if (gat->inMap(tile))
		{
			math::Ray ray(glm::vec3(p.x, glm::max(1000.0f, p.y + 100.0f), p.z), glm::vec3(0, -1, 0));
			auto hit = gnd->rayCast(ray, true, tile.x / 2 - 2, tile.y / 2 - 2, tile.x / 2 + 2, tile.y / 2 + 2);
			if (hit.y > 100000 || hit.y < -100000)
				hit = gnd->rayCast(ray, true, tile.x / 2 - 6, tile.y / 2 - 6, tile.x / 2 + 6, tile.y / 2 + 6);
			if (!(hit.y > 100000 || hit.y < -100000))
				result = hit.y;
		}
		terrainCache[key] = result;
		return result;
	};

	for (auto node : nodes)
	{
		if (!node)
			continue;

		node->traverse([&](Node* n)
		{
			auto rswModel = n->getComponent<RswModel>();
			auto rswObject = n->getComponent<RswObject>();
			auto rsm = n->getComponent<Rsm>();
			auto rsmRenderer = n->getComponent<RsmRenderer>();
			if (!rswModel || !rswObject || !rsm || !rsmRenderer || !rsm->loaded || rsm->rootMesh == nullptr)
				return;

			int gatType = rswModel->gatType > -1 ? rswModel->gatType : defaultGatType;

			// footprint of the model: all tiles its ground contact area overlaps
			std::set<TileKey> footprint;
			if (!removed)
			{
				std::vector<std::array<glm::vec3, 3>> triangles;
				glm::mat4 matrix = useMatrixCache ? rsmRenderer->matrixCache : autoGatModelMatrix(gnd, rswObject, rsm);
				collectWorldTriangles(rsm->rootMesh, rsmRenderer, matrix, triangles);

				const float margin = 0.05f; // so a model that is exactly aligned to the grid doesn't spill into its neighbours
				for (const auto& tri : triangles)
				{
					// only faces that touch the ground: at least one corner close to (or below) the terrain under it
					bool touchesGround = false;
					for (int i = 0; i < 3 && !touchesGround; i++)
					{
						float ground = terrainHeightAt(tri[i]);
						if (!std::isnan(ground) && tri[i].y <= ground + bandHeight)
							touchesGround = true;
					}
					if (!touchesGround)
						continue;

					// project onto the ground plane and collect every GAT tile it overlaps
					glm::vec2 flat[3] = { glm::vec2(tri[0].x, tri[0].z), glm::vec2(tri[1].x, tri[1].z), glm::vec2(tri[2].x, tri[2].z) };
					glm::vec2 lo = glm::min(flat[0], glm::min(flat[1], flat[2]));
					glm::vec2 hi = glm::max(flat[0], glm::max(flat[1], flat[2]));

					int tileXMin = (int)glm::floor(lo.x / 5.0f);
					int tileXMax = (int)glm::floor(hi.x / 5.0f);
					int tileYMin = gat->height - (int)glm::floor(hi.y / 5.0f) + 1;
					int tileYMax = gat->height - (int)glm::floor(lo.y / 5.0f) + 1;

					for (int tx = glm::max(tileXMin, 0); tx <= glm::min(tileXMax, gat->width - 1); tx++)
					{
						for (int ty = glm::max(tileYMin, 0); ty <= glm::min(tileYMax, gat->height - 1); ty++)
						{
							if (footprint.count({ tx, ty }) > 0)
								continue;

							// tile bounds in world space, matching Gat::getPos
							glm::vec2 rMin(5.0f * tx + margin, 5.0f * gat->height - 5.0f * ty + 5.0f + margin);
							glm::vec2 rMax(5.0f * tx + 5.0f - margin, 5.0f * gat->height - 5.0f * ty + 10.0f - margin);
							if (triangleOverlapsRect(flat, rMin, rMax))
								footprint.insert({ tx, ty });
						}
					}
				}
			}


			// fill the area the walls enclose (the inside of a house): close small gaps like doorways, then everything
			// that can't be reached from outside the model is blocked too (the doorway itself included)
			if (fillInterior && !footprint.empty())
			{
				int r = glm::clamp(closeGap, 0, 3);
				int pad = r + 1;
				int minX = footprint.begin()->first, maxX = minX, minY = footprint.begin()->second, maxY = minY;
				for (const auto& t : footprint)
				{
					minX = glm::min(minX, t.first); maxX = glm::max(maxX, t.first);
					minY = glm::min(minY, t.second); maxY = glm::max(maxY, t.second);
				}
				int x0 = minX - pad, y0 = minY - pad;
				int W = maxX - minX + 1 + 2 * pad, H = maxY - minY + 1 + 2 * pad;
				std::vector<char> solid((size_t)W * H, 0);
				for (const auto& t : footprint)
					solid[(size_t)(t.second - y0) * W + (t.first - x0)] = 1;

				auto morph = [&](const std::vector<char>& in, bool dilate)
				{
					std::vector<char> out((size_t)W * H, 0);
					for (int y = 0; y < H; y++)
						for (int x = 0; x < W; x++)
						{
							bool any = false, all = true;
							for (int dy = -r; dy <= r; dy++)
								for (int dx = -r; dx <= r; dx++)
								{
									int xx = x + dx, yy = y + dy;
									bool v = xx >= 0 && yy >= 0 && xx < W && yy < H && in[(size_t)yy * W + xx];
									any = any || v;
									all = all && v;
								}
							out[(size_t)y * W + x] = dilate ? any : all;
						}
					return out;
				};
				std::vector<char> closed = r > 0 ? morph(morph(solid, true), false) : solid;

				// flood the outside, starting from the padded border (which is always empty)
				std::vector<char> outside((size_t)W * H, 0);
				std::vector<int> stack = { 0 };
				outside[0] = 1;
				while (!stack.empty())
				{
					int i = stack.back(); stack.pop_back();
					int x = i % W, y = i / W;
					const int nx[4] = { x - 1, x + 1, x, x }, ny[4] = { y, y, y - 1, y + 1 };
					for (int k = 0; k < 4; k++)
					{
						if (nx[k] < 0 || ny[k] < 0 || nx[k] >= W || ny[k] >= H)
							continue;
						int j = ny[k] * W + nx[k];
						if (!outside[j] && !closed[j])
						{
							outside[j] = 1;
							stack.push_back(j);
						}
					}
				}
				for (int y = 0; y < H; y++)
					for (int x = 0; x < W; x++)
					{
						size_t i = (size_t)y * W + x;
						if (!solid[i] && !outside[i] && gat->inMap(glm::ivec2(x + x0, y + y0)))
							footprint.insert({ x + x0, y + y0 });
					}
			}

			std::vector<RswModel::AutoGatTile> result;

			// tiles the model used to block but doesn't anymore
			for (const auto& old : rswModel->autoGatTiles)
			{
				TileKey k(old.x, old.y);
				if (footprint.count(k) > 0)
					continue;
				auto& tileOwners = owners[k];
				tileOwners.erase(rswModel);
				// give the tile back, unless somebody else still blocks it or it was changed by hand in the meantime
				if (tileOwners.empty() && valueOf(k) == old.set)
					virtualValue[k] = old.orig;
			}

			// tiles the model blocks now
			for (const auto& k : footprint)
			{
				auto& tileOwners = owners[k];
				auto existing = tileOwners.find(rswModel);
				if (existing != tileOwners.end())
				{
					// already blocked by this model; follow a changed type
					if (existing->second.set != gatType && valueOf(k) == existing->second.set)
					{
						virtualValue[k] = gatType;
						existing->second.set = gatType;
					}
					result.push_back(existing->second);
					continue;
				}

				RswModel::AutoGatTile t;
				t.x = k.first;
				t.y = k.second;
				// when another model already blocks the tile, the value from before any model is what we have to give back
				t.orig = tileOwners.empty() ? valueOf(k) : tileOwners.begin()->second.orig;
				t.set = gatType;
				if (valueOf(k) != gatType)
					virtualValue[k] = gatType;
				tileOwners[rswModel] = t;
				result.push_back(t);
			}

			bool changed = result.size() != rswModel->autoGatTiles.size();
			for (size_t i = 0; !changed && i < result.size(); i++)
			{
				const auto& a = result[i];
				const auto& b = rswModel->autoGatTiles[i];
				changed = a.x != b.x || a.y != b.y || a.orig != b.orig || a.set != b.set;
			}
			if (changed)
				ga->addAction(new AutoGatFootprintAction(rswModel, result));
		});
	}

	for (const auto& v : virtualValue)
		if (gat->cubes[v.first.first][v.first.second]->gatType != v.second)
			ga->addAction(new GatTileChangeAction(glm::ivec2(v.first.first, v.first.second), gat->cubes[v.first.first][v.first.second], v.second));
}

void Map::autoGatFollow(const std::vector<Node*>& nodes, BrowEdit* browEdit, GroupAction* ga, bool removed)
{
	if (browEdit->autoGatOnPlace)
		addAutoGatActions(nodes, browEdit->autoGatBandHeight, browEdit->autoGatType, ga, removed, false, browEdit->autoGatFillInterior, browEdit->autoGatCloseGap);
}


void Map::optimizeGndTiles(BrowEdit* browEdit)
{
	rootNode->getComponent<Gnd>()->cleanTiles();
}


void Map::exportShadowMap(BrowEdit* browEdit, bool exportWalls, bool exportBorders)
{
	int tileSize = 8;
	int tilePadding = 0;
	if (!exportBorders)
	{
		tileSize = 6;
		tilePadding = 1;
	}
	int wallMultiplier = 2;
	if (!exportWalls)
		wallMultiplier = 1;

	auto shadowmapcpy = [&](unsigned char* src, char* dst, int dstx, int dsty, int w)
	{
		for (int x = 0; x < tileSize; x++)
			for (int y = 0; y < tileSize; y++)
				dst[(dstx+x)+w*(dsty+y)] = src[(x+tilePadding) + 8 * (y+tilePadding)];
	};
	auto gnd = rootNode->getComponent<Gnd>();
	char* img = new char[gnd->width * tileSize* wallMultiplier * gnd->height * tileSize * wallMultiplier];
	memset(img, 0, gnd->width * tileSize * wallMultiplier * gnd->height * tileSize * wallMultiplier);
	for (auto x = 0; x < gnd->width; x++)
	{
		for (auto y = 0; y < gnd->height; y++)
		{
			auto cube = gnd->cubes[x][y];
			int xx = wallMultiplier * tileSize * x;
			int yy = wallMultiplier * tileSize * y;

			if (cube->tileUp > -1 && gnd->tiles[cube->tileUp]->lightmapIndex > -1)
				shadowmapcpy(gnd->lightmaps[gnd->tiles[cube->tileUp]->lightmapIndex]->data, img, xx, yy, wallMultiplier*tileSize*gnd->width);
			if (exportWalls)
			{
				if (cube->tileSide > -1 && gnd->tiles[cube->tileSide]->lightmapIndex > -1)
					shadowmapcpy(gnd->lightmaps[gnd->tiles[cube->tileSide]->lightmapIndex]->data, img, xx, yy + tileSize, wallMultiplier * tileSize * gnd->width);
				if (cube->tileFront > -1 && gnd->tiles[cube->tileFront]->lightmapIndex > -1)
					shadowmapcpy(gnd->lightmaps[gnd->tiles[cube->tileFront]->lightmapIndex]->data, img, xx + tileSize, yy, wallMultiplier * tileSize * gnd->width);
			}
		}
	}
	stbi_write_png((browEdit->config.ropath + name + ".shadowmap.png").c_str(), gnd->width* wallMultiplier * tileSize, gnd->height* wallMultiplier * tileSize, 1, img, gnd->width * wallMultiplier * tileSize);
	delete[] img;
}
void Map::importShadowMap(BrowEdit* browEdit, bool exportWalls, bool exportBorders)
{
	int tileSize = 8;
	int tilePadding = 0;
	if (!exportBorders)
	{
		tileSize = 6;
		tilePadding = 1;
	}
	int wallMultiplier = 2;
	if (!exportWalls)
		wallMultiplier = 1;

	auto shadowmapcpy = [&](unsigned char* dst, unsigned char* src, int srcx, int srcy, int w)
	{
		for (int x = 0; x < tileSize; x++)
			for (int y = 0; y < tileSize; y++)
				dst[(x + tilePadding) + 8 * (y + tilePadding)] = src[(srcx + x) + w * (srcy + y)];
	};
	auto gnd = rootNode->getComponent<Gnd>();
	int w, h,c;
	unsigned char* img = stbi_load((browEdit->config.ropath + name + ".shadowmap.png").c_str(), &w, &h, &c, 1);
	if (w != gnd->width * wallMultiplier * tileSize ||
		h != gnd->height * wallMultiplier * tileSize)
	{
		std::cerr << "Image is wrong size" << std::endl;
		return;
	}

	for (auto x = 0; x < gnd->width; x++)
	{
		for (auto y = 0; y < gnd->height; y++)
		{
			auto cube = gnd->cubes[x][y];
			int xx = wallMultiplier * tileSize * x;
			int yy = wallMultiplier * tileSize * y;

			if (cube->tileUp > -1 && gnd->tiles[cube->tileUp]->lightmapIndex > -1)
				shadowmapcpy(gnd->lightmaps[gnd->tiles[cube->tileUp]->lightmapIndex]->data, img, xx, yy, wallMultiplier * tileSize * gnd->width);
			if (exportWalls)
			{
				if (cube->tileSide > -1 && gnd->tiles[cube->tileSide]->lightmapIndex > -1)
					shadowmapcpy(gnd->lightmaps[gnd->tiles[cube->tileSide]->lightmapIndex]->data, img, xx, yy + tileSize, wallMultiplier * tileSize * gnd->width);
				if (cube->tileFront > -1 && gnd->tiles[cube->tileFront]->lightmapIndex > -1)
					shadowmapcpy(gnd->lightmaps[gnd->tiles[cube->tileFront]->lightmapIndex]->data, img, xx + tileSize, yy, wallMultiplier * tileSize * gnd->width);
			}
		}
	}
	rootNode->getComponent<GndRenderer>()->gndShadowDirty = true;
	stbi_image_free(img);
}

void Map::exportLightMap(BrowEdit* browEdit, bool exportWalls, bool exportBorders)
{
	int tileSize = 8;
	int tilePadding = 0;
	if (!exportBorders)
	{
		tileSize = 6;
		tilePadding = 1;
	}
	int wallMultiplier = 2;
	if (!exportWalls)
		wallMultiplier = 1;

	auto lightmapcpy = [&](unsigned char* src, char* dst, int dstx, int dsty, int w)
	{
		for (int x = 0; x < tileSize; x++)
			for (int y = 0; y < tileSize; y++)
				for (int c = 0; c < 3; c++)
					dst[((dstx + x) + w * (dsty + y))*3+c] = src[64+ ((x+tilePadding) + 8 * (y+tilePadding))*3+c];
	};
	auto gnd = rootNode->getComponent<Gnd>();
	char* img = new char[(gnd->width * tileSize * wallMultiplier * gnd->height * tileSize * wallMultiplier)*3];
	memset(img, 0, gnd->width * tileSize * wallMultiplier * gnd->height * tileSize * wallMultiplier *3);
	for (auto x = 0; x < gnd->width; x++)
	{
		for (auto y = 0; y < gnd->height; y++)
		{
			auto cube = gnd->cubes[x][y];
			int xx = tileSize * wallMultiplier * x;
			int yy = tileSize * wallMultiplier * y;

			if (cube->tileUp > -1 && gnd->tiles[cube->tileUp]->lightmapIndex > -1)
				lightmapcpy(gnd->lightmaps[gnd->tiles[cube->tileUp]->lightmapIndex]->data, img, xx, yy, tileSize * wallMultiplier * gnd->width);
			if (exportWalls)
			{
				if (cube->tileSide > -1 && gnd->tiles[cube->tileSide]->lightmapIndex > -1)
					lightmapcpy(gnd->lightmaps[gnd->tiles[cube->tileSide]->lightmapIndex]->data, img, xx, yy + tileSize, tileSize * wallMultiplier * gnd->width);
				if (cube->tileFront > -1 && gnd->tiles[cube->tileFront]->lightmapIndex > -1)
					lightmapcpy(gnd->lightmaps[gnd->tiles[cube->tileFront]->lightmapIndex]->data, img, xx + tileSize, yy, tileSize * wallMultiplier * gnd->width);
			}
		}
	}
	stbi_write_png((browEdit->config.ropath + name + ".colormap.png").c_str(), gnd->width * tileSize * wallMultiplier, gnd->height * tileSize * wallMultiplier, 3, img, gnd->width * tileSize * wallMultiplier *3);
	delete[] img;
}
void Map::importLightMap(BrowEdit* browEdit, bool exportWalls, bool exportBorders)
{
	int tileSize = 8;
	int tilePadding = 0;
	if (!exportBorders)
	{
		tileSize = 6;
		tilePadding = 1;
	}
	int wallMultiplier = 2;
	if (!exportWalls)
		wallMultiplier = 1;

	auto lightmapcpy = [&](unsigned char* dst, unsigned char* src, int srcx, int srcy, int w)
	{
		for (int x = 0; x < tileSize; x++)
			for (int y = 0; y < tileSize; y++)
				for (int c = 0; c < 3; c++)
					dst[64 + ((x + tilePadding) + 8 * (y + tilePadding)) * 3 + c] = src[((srcx + x) + w * (srcy + y)) * 3 + c];
	};
	auto gnd = rootNode->getComponent<Gnd>();
	gnd->makeLightmapsUnique();
	int w, h, c;
	unsigned char* img = stbi_load((browEdit->config.ropath + name + ".colormap.png").c_str(), &w, &h, &c, 3);
	if (w != gnd->width * wallMultiplier*tileSize ||
		h != gnd->height * wallMultiplier*tileSize)
	{
		std::cerr << "Image is wrong size" << std::endl;
		return;
	}
	for (auto x = 0; x < gnd->width; x++)
	{
		for (auto y = 0; y < gnd->height; y++)
		{
			auto cube = gnd->cubes[x][y];
			int xx = tileSize * wallMultiplier * x;
			int yy = tileSize * wallMultiplier * y;

			if (cube->tileUp > -1)
			{
				if (gnd->tiles[cube->tileUp]->lightmapIndex == -1)
				{
					gnd->tiles[cube->tileUp]->lightmapIndex = (int)gnd->lightmaps.size();
					gnd->lightmaps.push_back(new Gnd::Lightmap(gnd));
				}
				lightmapcpy(gnd->lightmaps[gnd->tiles[cube->tileUp]->lightmapIndex]->data, img, xx, yy, tileSize * wallMultiplier * gnd->width);
			}
			if (exportWalls)
			{
				if (cube->tileSide > -1)
				{
					if (gnd->tiles[cube->tileSide]->lightmapIndex == -1)
					{
						gnd->tiles[cube->tileSide]->lightmapIndex = (int)gnd->lightmaps.size();
						gnd->lightmaps.push_back(new Gnd::Lightmap(gnd));
					}
					lightmapcpy(gnd->lightmaps[gnd->tiles[cube->tileSide]->lightmapIndex]->data, img, xx, yy + tileSize, tileSize * wallMultiplier * gnd->width);
				}
				if (cube->tileFront > -1)
				{
					if (gnd->tiles[cube->tileFront]->lightmapIndex == -1)
					{
						gnd->tiles[cube->tileFront]->lightmapIndex = (int)gnd->lightmaps.size();
						gnd->lightmaps.push_back(new Gnd::Lightmap(gnd));
					}
					lightmapcpy(gnd->lightmaps[gnd->tiles[cube->tileFront]->lightmapIndex]->data, img, xx + tileSize, yy, tileSize * wallMultiplier * gnd->width);
				}
			}
		}
	}
	gnd->makeLightmapsUnique();
	rootNode->getComponent<GndRenderer>()->gndShadowDirty = true;

	stbi_image_free(img);
}

void Map::exportTileColors(BrowEdit* browEdit, bool exportWalls)
{
	int wallMultiplier = 2;
	if (!exportWalls)
		wallMultiplier = 1;

	auto lightmapcpy = [&](const glm::ivec4& src, char* dst, int dstx, int dsty, int w)
	{
		for (int c = 0; c < 3; c++)
			dst[((dstx) + w * (dsty)) * 3 + c] = src[c];
	};
	auto gnd = rootNode->getComponent<Gnd>();
	char* img = new char[(gnd->width * wallMultiplier * gnd->height * wallMultiplier) * 3];
	memset(img, 0, gnd->width * wallMultiplier * gnd->height * wallMultiplier * 3);
	for (auto x = 0; x < gnd->width; x++)
	{
		for (auto y = 0; y < gnd->height; y++)
		{
			auto cube = gnd->cubes[x][y];
			int xx = wallMultiplier * x;
			int yy = wallMultiplier * y;

			if (cube->tileUp > -1)
				lightmapcpy(gnd->tiles[cube->tileUp]->color, img, xx, yy, wallMultiplier * gnd->width);
			if (exportWalls)
			{
				if (cube->tileSide > -1)
					lightmapcpy(gnd->tiles[cube->tileSide]->color, img, xx, yy + 1, wallMultiplier * gnd->width);
				if (cube->tileFront > -1)
					lightmapcpy(gnd->tiles[cube->tileFront]->color, img, xx + 1, yy, wallMultiplier * gnd->width);
			}
		}
	}


	stbi_write_png((browEdit->config.ropath + name + ".tilecolor.png").c_str(), gnd->width * wallMultiplier, gnd->height * wallMultiplier, 3, img, gnd->width * wallMultiplier * 3);
	delete[] img;
}


void Map::exportMinimapWalkable(BrowEdit* browEdit)
{
	auto gat = rootNode->getComponent<Gat>();
	if (!gat)
		return;

	const int outW = 512;
	const int outH = 512;
	unsigned char* img = new unsigned char[outW * outH * 3];

	// Fill with magenta (255,0,255) — the RO transparency key color for padding
	for (int i = 0; i < outW * outH * 3; i += 3)
	{
		img[i + 0] = 255;
		img[i + 1] = 0;
		img[i + 2] = 255;
	}

	// Preserve aspect ratio: scale to fit 512x512, center with magenta padding
	float scale = std::min((float)outW / gat->width, (float)outH / gat->height);
	int contentW = (int)(gat->width * scale);
	int contentH = (int)(gat->height * scale);
	int ox = (outW - contentW) / 2;
	int oy = (outH - contentH) / 2;

	// GAT type colors — dark = walkable, light = walls (matching game minimap)
	auto gatColor = [](int gatType) -> glm::ivec3
	{
		switch (gatType)
		{
		case 0:  return glm::ivec3(40, 40, 40);      // walkable — dark
		case 1:  return glm::ivec3(170, 170, 170);    // not walkable — light grey
		case 3:  return glm::ivec3(60, 80, 120);      // walkable + water — dark blue
		case 4:  return glm::ivec3(50, 50, 50);       // walkable (no-water variant)
		case 5:  return glm::ivec3(80, 80, 80);       // snipable — medium dark
		default: return glm::ivec3(130, 130, 130);    // unknown — medium grey
		}
	};

	for (int py = 0; py < contentH; py++)
	{
		for (int px = 0; px < contentW; px++)
		{
			int gx = (int)((float)px / contentW * gat->width);
			// Flip Y: GAT Y=0 is bottom, BMP Y=0 is top
			int gy = (int)((float)(contentH - 1 - py) / contentH * gat->height);
			if (gx >= gat->width) gx = gat->width - 1;
			if (gy >= gat->height) gy = gat->height - 1;
			if (gy < 0) gy = 0;

			auto cube = gat->cubes[gx][gy];
			auto color = gatColor(cube->gatType);
			int outX = ox + px;
			int outY = oy + py;
			int idx = (outX + outW * outY) * 3;
			img[idx + 0] = (unsigned char)color.r;
			img[idx + 1] = (unsigned char)color.g;
			img[idx + 2] = (unsigned char)color.b;
		}
	}

	// Extract map name — strip path and file extension (.rsw, .gnd, .gat)
	std::string mapName = name;
	auto pos = mapName.rfind('\\');
	if (pos != std::string::npos)
		mapName = mapName.substr(pos + 1);
	pos = mapName.rfind('/');
	if (pos != std::string::npos)
		mapName = mapName.substr(pos + 1);
	pos = mapName.rfind('.');
	if (pos != std::string::npos)
		mapName = mapName.substr(0, pos);

	std::string outDir = browEdit->config.ropath + "data\\texture\\\xc0\xaf\xc0\xfa\xc0\xce\xc5\xcd\xc6\xe4\xc0\xcc\xbd\xba\\map";
	std::filesystem::create_directories(outDir);
	std::string outPath = outDir + "\\" + mapName + ".bmp";
	int ok = stbi_write_bmp(outPath.c_str(), outW, outH, 3, img);
	delete[] img;
	if (ok)
	{
		std::cout << "Exported walkable minimap: " << outPath << std::endl;
		std::string cmd = "/select,\"" + outPath + "\"";
		ShellExecuteA(NULL, "open", "explorer.exe", cmd.c_str(), NULL, SW_SHOWNORMAL);
	}
	else
	{
		std::cout << "ERROR: Failed to write " << outPath << std::endl;
		MessageBoxA(NULL, ("Failed to write:\n" + outPath).c_str(), "Export Error", MB_OK | MB_ICONERROR);
	}
}


// top-down picture of the ground (textures, tile colours, shadows and light colours), saved where the client looks for the minimap
std::string Map::exportMinimapRendered(BrowEdit* browEdit, int size, bool shading, bool relief, bool padMagenta)
{
	auto gnd = rootNode->getComponent<Gnd>();
	if (!gnd || gnd->width < 1 || gnd->height < 1)
		return "No ground to render";
	size = std::clamp(size, 64, 4096);
	const int outW = size, outH = size;
	std::vector<unsigned char> img((size_t)outW * outH * 3, 0);
	if (padMagenta)
		for (size_t i = 0; i < img.size(); i += 3) { img[i] = 255; img[i + 1] = 0; img[i + 2] = 255; }

	float scale = std::min((float)outW / (10.0f * gnd->width), (float)outH / (10.0f * gnd->height));		// pixels per world unit
	int contentW = std::max(1, (int)(10.0f * gnd->width * scale)), contentH = std::max(1, (int)(10.0f * gnd->height * scale));
	int ox = (outW - contentW) / 2, oy = (outH - contentH) / 2;

	std::vector<const advtex::Img*> imgs(gnd->textures.size(), nullptr);
	for (size_t i = 0; i < gnd->textures.size(); i++)
		imgs[i] = advtex::getImg(gnd->textures[i]->file);

	// colour of one point of the ground; (wx, wzTop) are world units, wzTop counted from the top of the picture
	auto sampleGround = [&](float wx, float wzTop, float out[3]) -> bool
	{
		int cx = (int)std::floor(wx / 10.0f), cy = (int)std::floor(wzTop / 10.0f);
		if (cx < 0 || cy < 0 || cx >= gnd->width || cy >= gnd->height) return false;
		float fx = wx / 10.0f - cx, fz = 1.0f - (wzTop / 10.0f - cy);
		auto cube = gnd->cubes[cx][cy];
		if (cube->tileUp < 0 || cube->tileUp >= (int)gnd->tiles.size()) return false;
		auto tile = gnd->tiles[cube->tileUp];
		float rgb[3] = { 128, 128, 128 };
		if (tile->textureIndex >= 0 && tile->textureIndex < (int)imgs.size() && imgs[tile->textureIndex])
		{
			const advtex::Img& im = *imgs[tile->textureIndex];
			glm::vec2 uv = (tile->v3 * (1 - fx) + tile->v4 * fx) * (1 - fz) + (tile->v1 * (1 - fx) + tile->v2 * fx) * fz;
			float x = std::clamp(uv.x, 0.0f, 1.0f) * im.w - 0.5f, y = std::clamp(uv.y, 0.0f, 1.0f) * im.h - 0.5f;
			int x0 = (int)std::floor(x), y0 = (int)std::floor(y);
			float ax = x - x0, ay = y - y0;
			auto at = [&](int xx, int yy, int c) { xx = std::clamp(xx, 0, im.w - 1); yy = std::clamp(yy, 0, im.h - 1); return (float)im.px[((size_t)yy * im.w + xx) * 4 + c]; };
			for (int c = 0; c < 3; c++)
			{
				float a = at(x0, y0, c) * (1 - ax) + at(x0 + 1, y0, c) * ax, b = at(x0, y0 + 1, c) * (1 - ax) + at(x0 + 1, y0 + 1, c) * ax;
				rgb[c] = a * (1 - ay) + b * ay;
			}
		}
		for (int c = 0; c < 3; c++) rgb[c] *= tile->color[c] / 255.0f;
		if (shading && tile->lightmapIndex >= 0 && tile->lightmapIndex < (int)gnd->lightmaps.size())
		{
			auto lm = gnd->lightmaps[tile->lightmapIndex];
			int lw = gnd->lightmapWidth, lh = gnd->lightmapHeight;
			int lx = std::clamp((int)(1 + fx * (lw - 2)), 0, lw - 1), ly = std::clamp((int)(1 + (1.0f - fz) * (lh - 2)), 0, lh - 1);
			float shade = lm->data[lw * ly + lx] / 255.0f;
			for (int c = 0; c < 3; c++)
				rgb[c] = rgb[c] * (0.45f + 0.55f * shade) + lm->data[lw * lh + 3 * (lw * ly + lx) + c] * 0.5f;
		}
		if (relief)
		{
			// small hill shading from the ground's slope, so cliffs and hollows read on a flat picture
			float hh = -((cube->h3 * (1 - fx) + cube->h4 * fx) * (1 - fz) + (cube->h1 * (1 - fx) + cube->h2 * fx) * fz);
			float dx = -((cube->h2 + cube->h4) - (cube->h1 + cube->h3)) * 0.5f, dz = -((cube->h1 + cube->h2) - (cube->h3 + cube->h4)) * 0.5f;
			(void)hh;
			float l = std::clamp(1.0f + (dx * -0.5f + dz * 0.5f) * 0.02f, 0.6f, 1.4f);
			for (int c = 0; c < 3; c++) rgb[c] *= l;
		}
		for (int c = 0; c < 3; c++) out[c] = rgb[c];
		return true;
	};

	for (int py = 0; py < contentH; py++)
		for (int px = 0; px < contentW; px++)
		{
			float acc[3] = { 0, 0, 0 }; int n = 0;
			for (int sy = 0; sy < 2; sy++)
				for (int sx = 0; sx < 2; sx++)
				{
					float c[3];
					if (sampleGround((px + 0.25f + 0.5f * sx) / scale, (py + 0.25f + 0.5f * sy) / scale, c)) { for (int k = 0; k < 3; k++) acc[k] += c[k]; n++; }
				}
			if (n == 0) continue;
			size_t o = ((size_t)(oy + py) * outW + (ox + px)) * 3;
			for (int k = 0; k < 3; k++) img[o + k] = (unsigned char)std::clamp((int)std::lround(acc[k] / n), 0, 255);
			if (n < 4 && padMagenta) for (int k = 0; k < 3; k++) img[o + k] = (unsigned char)std::clamp((int)std::lround((acc[k] + (k == 1 ? 0 : 255.0f) * (4 - n)) / 4.0f), 0, 255);
		}

	std::string mapName = name;
	auto pos = mapName.find_last_of("\\/");
	if (pos != std::string::npos) mapName = mapName.substr(pos + 1);
	pos = mapName.rfind('.');
	if (pos != std::string::npos) mapName = mapName.substr(0, pos);

	std::string outDir = browEdit->config.ropath + "data\\texture\\\xc0\xaf\xc0\xfa\xc0\xce\xc5\xcd\xc6\xe4\xc0\xcc\xbd\xba\\map";
	std::error_code ec;
	std::filesystem::create_directories(outDir, ec);
	std::string outPath = outDir + "\\" + mapName + ".bmp";
	if (!stbi_write_bmp(outPath.c_str(), outW, outH, 3, img.data()))
		return "Could not write " + outPath;
	return "Saved " + outPath;
}


void Map::importTileColors(BrowEdit* browEdit, bool exportWalls)
{
	int wallMultiplier = 2;
	if (!exportWalls)
		wallMultiplier = 1;

	auto lightmapcpy = [&](glm::ivec4& dst, unsigned char* src, int srcx, int srcy, int w)
	{
		for (int c = 0; c < 3; c++)
			dst[c] = src[((srcx)+w * (srcy)) * 3 + c];
	};
	auto gnd = rootNode->getComponent<Gnd>();
	int w, h, c;
	unsigned char* img = stbi_load((browEdit->config.ropath + name + ".colormap.png").c_str(), &w, &h, &c, 1);
	if (w != gnd->width * wallMultiplier ||
		h != gnd->height * wallMultiplier)
	{
		std::cerr << "Image is wrong size" << std::endl;
		return;
	}
	for (auto x = 0; x < gnd->width; x++)
	{
		for (auto y = 0; y < gnd->height; y++)
		{
			auto cube = gnd->cubes[x][y];
			int xx = wallMultiplier * x;
			int yy = wallMultiplier * y;

			if (cube->tileUp > -1)
				lightmapcpy(gnd->tiles[cube->tileUp]->color, img, xx, yy, wallMultiplier * gnd->width);
			if (exportWalls)
			{
				if (cube->tileSide > -1)
					lightmapcpy(gnd->tiles[cube->tileSide]->color, img, xx, yy + 1, wallMultiplier * gnd->width);
				if (cube->tileFront > -1)
					lightmapcpy(gnd->tiles[cube->tileFront]->color, img, xx + 1, yy, wallMultiplier * gnd->width);
			}
		}
	}
	stbi_image_free(img);
}



void Map::recalculateQuadTree(BrowEdit* browEdit)
{
	std::cout << "Recalculating quadtree" << std::endl;
	auto rsw = rootNode->getComponent<Rsw>();
	//if (!rsw->quadtree)
	{
		auto gnd = rootNode->getComponent<Gnd>();
		if(rsw->quadtree)
			delete rsw->quadtree;
		rsw->quadtree = new Rsw::QuadTreeNode(10*-gnd->width/2.0f, 10 * -gnd->height/2.0f, (float)gnd->width*10 - 1.0f, (float)gnd->height*10 - 1.0f, 0);
	}

	rsw->recalculateQuadtree();
	std::cout << "Done recalculating quadtree" << std::endl;
}

void Map::growTileSelection(BrowEdit* browEdit)
{
	auto around = getSelectionAroundTiles();
	around.insert(around.end(), tileSelection.begin(), tileSelection.end());
	auto action = new TileSelectAction(this, around);
	doAction(action, browEdit);
}

void Map::shrinkTileSelection(BrowEdit* browEdit)
{
	auto gnd = rootNode->getComponent<Gnd>();
	std::vector<glm::ivec2> shrunk;
	auto isTileSelected = [&](int x, int y) { return std::find(tileSelection.begin(), tileSelection.end(), glm::ivec2(x, y)) != tileSelection.end(); };

	for (auto& t : tileSelection)
	{
		bool ok = true;
		for (int xx = -1; xx <= 1; xx++)
			for (int yy = -1; yy <= 1; yy++)
				if (gnd->inMap(t + glm::ivec2(xx, yy)) &&
					std::find(shrunk.begin(), shrunk.end(), glm::ivec2(t.x, t.y)) == shrunk.end() &&
					!isTileSelected(t.x + xx, t.y + yy))
					ok = false;
		if(ok)
			shrunk.push_back(t);
	}

	auto action = new TileSelectAction(this, shrunk);
	doAction(action, browEdit);
}


void Map::createPrefab(const std::string& fileName, BrowEdit* browEdit)
{
	std::filesystem::path filePath("data\\prefabs\\" + fileName);
	std::filesystem::create_directories(filePath.parent_path());
	std::ofstream outFile(filePath);
	json clipboard;
	for (auto n : selectedNodes)
		clipboard.push_back(*n);
	outFile << clipboard;
	util::FileIO::reload("data\\prefabs");
}

void WallCalculation::calcUV(const glm::ivec3& position, Gnd* gnd)
{
	auto cube = gnd->cubes[position.x][position.y];
	int index = (offset + position.x + position.y) % wallWidth;

	float minHeight = 0;
	float maxHeight = 0;

	if (position.z == 1 && position.x < gnd->width-1) //tileside
	{
		auto cube2 = gnd->cubes[position.x + 1][position.y];
		index = (gnd->height - position.y - offset) % wallWidth;
		minHeight = glm::min(glm::min(glm::min(-cube->h2, -cube->h4), -cube2->h1), -cube2->h3);
		maxHeight = glm::max(glm::max(glm::max(-cube->h2, -cube->h4), -cube2->h1), -cube2->h3);
	}
	if (position.z == 2 && position.y < gnd->height-1) //tilefront
	{
		auto cube2 = gnd->cubes[position.x][position.y + 1];
		minHeight = glm::min(glm::min(glm::min(-cube->h4, -cube->h3), -cube2->h2), -cube2->h1);
		maxHeight = glm::max(glm::max(glm::max(-cube->h4, -cube->h3), -cube2->h2), -cube2->h1);
	}

	if (!wallTopAuto)
		maxHeight = this->wallTop;
	if (!wallBottomAuto)
		minHeight = this->wallBottom;


	g_uv1 = uvStart + xInc * (float)index + yInc * (float)0.0f;
	g_uv2 = g_uv1 + xInc;
	g_uv3 = g_uv1 + yInc;
	g_uv4 = g_uv1 + xInc + yInc;

	g_uv1.y = 1 - g_uv1.y;
	g_uv2.y = 1 - g_uv2.y;
	g_uv3.y = 1 - g_uv3.y;
	g_uv4.y = 1 - g_uv4.y;

	if (autoStraight)
	{
		float h1 = -cube->h4;
		float h2 = -cube->h2;
		float h3, h4;
		if (position.z == 1 && gnd->inMap(position + glm::ivec3(1, 0, 0)))
		{
			h3 = -gnd->cubes[position.x + 1][position.y]->h3;
			h4 = -gnd->cubes[position.x + 1][position.y]->h1;
		}
		else if (position.z == 2 && gnd->inMap(position + glm::ivec3(0, 1, 0)))
		{
			h1 = -cube->h3;
			h2 = -cube->h4;
			h4 = -gnd->cubes[position.x][position.y + 1]->h2;
			h3 = -gnd->cubes[position.x][position.y + 1]->h1;
		}
		else
			return;

		if (xInc.y == 0) {
			glm::vec2 guv1(g_uv1.x, glm::mix(g_uv1.y, g_uv3.y, (h1 - minHeight) / (maxHeight - minHeight)));
			glm::vec2 guv2(g_uv2.x, glm::mix(g_uv2.y, g_uv4.y, (h2 - minHeight) / (maxHeight - minHeight)));
			glm::vec2 guv3(g_uv3.x, glm::mix(g_uv1.y, g_uv3.y, (h3 - minHeight) / (maxHeight - minHeight)));
			glm::vec2 guv4(g_uv4.x, glm::mix(g_uv2.y, g_uv4.y, (h4 - minHeight) / (maxHeight - minHeight)));

			g_uv1 = guv1;
			g_uv2 = guv2;
			g_uv3 = guv3;
			g_uv4 = guv4;
		}
		else {
			glm::vec2 guv1(glm::mix(g_uv1.x, g_uv3.x, (h1 - minHeight) / (maxHeight - minHeight)), g_uv1.y);
			glm::vec2 guv2(glm::mix(g_uv2.x, g_uv4.x, (h2 - minHeight) / (maxHeight - minHeight)), g_uv2.y);
			glm::vec2 guv3(glm::mix(g_uv1.x, g_uv3.x, (h3 - minHeight) / (maxHeight - minHeight)), g_uv3.y);
			glm::vec2 guv4(glm::mix(g_uv2.x, g_uv4.x, (h4 - minHeight) / (maxHeight - minHeight)), g_uv4.y);

			g_uv1 = guv1;
			g_uv2 = guv2;
			g_uv3 = guv3;
			g_uv4 = guv4;
		}
	}

}


void WallCalculation::prepare(BrowEdit* browEdit)
{
	wallWidth = browEdit->activeMapView->wallWidth;
	offset = browEdit->activeMapView->wallOffset;
	wallTop = browEdit->activeMapView->wallTop;
	wallTopAuto = browEdit->activeMapView->wallTopAuto;
	wallBottom = browEdit->activeMapView->wallBottom;
	wallBottomAuto = browEdit->activeMapView->wallBottomAuto;
	autoStraight = browEdit->activeMapView->wallAutoStraight;


	glm::vec2 uvSize = browEdit->activeMapView->textureEditUv2 - browEdit->activeMapView->textureEditUv1;
	uv1 = glm::vec2(0, 0);
	uv4 = glm::vec2(1, 1);

	uv2 = glm::vec2(uv4.x, uv1.y);
	uv3 = glm::vec2(uv1.x, uv4.y);

	if (browEdit->activeMapView->textureBrushFlipD)
		std::swap(uv1, uv4); // different from texturebrush window due to flipped UV

	if (browEdit->activeMapView->textureBrushFlipH)
	{
		uv1.x = 1 - uv1.x;
		uv2.x = 1 - uv2.x;
		uv3.x = 1 - uv3.x;
		uv4.x = 1 - uv4.x;
	}
	if (browEdit->activeMapView->textureBrushFlipV)
	{
		uv1.y = 1 - uv1.y;
		uv2.y = 1 - uv2.y;
		uv3.y = 1 - uv3.y;
		uv4.y = 1 - uv4.y;
	}
	xInc = (uv2 - uv1) * uvSize / (float)browEdit->activeMapView->wallWidth;
	yInc = (uv3 - uv1) * uvSize / (float)1;
	if (browEdit->activeMapView->textureBrushFlipD)
	{
		if (browEdit->activeMapView->textureBrushFlipV ^ browEdit->activeMapView->textureBrushFlipH)
		{
			xInc = (uv1 - uv2) * uvSize / (float)1;
			yInc = (uv1 - uv3) * uvSize / (float)browEdit->activeMapView->wallWidth;
		}
		if (browEdit->activeMapView->textureBrushFlipH == browEdit->activeMapView->textureBrushFlipV)
		{
			xInc = (uv2 - uv1) * uvSize / (float)1;
			yInc = (uv3 - uv1) * uvSize / (float)browEdit->activeMapView->wallWidth;

		}
		std::swap(xInc, yInc);
		std::swap(xInc.x, xInc.y);
		std::swap(yInc.x, yInc.y);
	}

	uvStart = uv1 + browEdit->activeMapView->textureEditUv1.x * (uv2 - uv1) + browEdit->activeMapView->textureEditUv1.y * (uv3 - uv1);
}

void Map::wallAddSelected(BrowEdit* browEdit)
{
	auto gnd = rootNode->getComponent<Gnd>();
	auto gndRenderer = rootNode->getComponent<GndRenderer>();

	WallCalculation calculation;
	calculation.prepare(browEdit);

	auto ga = new GroupAction();
	int id = (int)gnd->tiles.size();

	for (const auto& w : wallSelection)
	{
		auto cube = gnd->cubes[w.x][w.y];

		calculation.calcUV(w, gnd);

		auto t = new Gnd::Tile();
		t->color = glm::ivec4(255, 255, 255, 255);
		t->textureIndex = browEdit->activeMapView->textureSelected;
		t->lightmapIndex = 0;

		t->v1 = calculation.g_uv1;
		t->v2 = calculation.g_uv2;
		t->v3 = calculation.g_uv3;
		t->v4 = calculation.g_uv4;

		if (w.z == 1 && cube->tileSide == -1)
			ga->addAction(new CubeTileChangeAction(w, cube, cube->tileUp, cube->tileFront, id));
		if (w.z == 2 && cube->tileFront == -1)
			ga->addAction(new CubeTileChangeAction(w, cube, cube->tileUp, id, cube->tileSide));
		ga->addAction(new TileNewAction(t));
		id++;
	}
	doAction(ga, browEdit);
	gndRenderer->gndShadowDirty = true;
}


void Map::wallReApplySelected(BrowEdit* browEdit)
{
	auto gnd = rootNode->getComponent<Gnd>();
	auto gndRenderer = rootNode->getComponent<GndRenderer>();

	WallCalculation calculation;
	calculation.prepare(browEdit);

	auto ga = new GroupAction();
	int id = (int)gnd->tiles.size();

	for (const auto& w : wallSelection)
	{
		auto cube = gnd->cubes[w.x][w.y];

		calculation.calcUV(w, gnd);

		auto t = new Gnd::Tile();
		t->color = glm::ivec4(255, 255, 255, 255);
		t->textureIndex = browEdit->activeMapView->textureSelected;
		t->lightmapIndex = 0;

		t->v1 = calculation.g_uv1;
		t->v2 = calculation.g_uv2;
		t->v3 = calculation.g_uv3;
		t->v4 = calculation.g_uv4;

		if (w.z == 1)
			ga->addAction(new CubeTileChangeAction(w, cube, cube->tileUp, cube->tileFront, id));
		if (w.z == 2)
			ga->addAction(new CubeTileChangeAction(w, cube, cube->tileUp, id, cube->tileSide));
		ga->addAction(new TileNewAction(t));
		id++;
	}
	doAction(ga, browEdit);
	gndRenderer->gndShadowDirty = true;
}



void Map::wallRemoveSelected(BrowEdit* browEdit)
{
	auto gnd = rootNode->getComponent<Gnd>();
	auto gndRenderer = rootNode->getComponent<GndRenderer>();
	auto ga = new GroupAction();
	for (const auto& w : wallSelection)
	{
		auto cube = gnd->cubes[w.x][w.y];
		if (w.z == 1 && cube->tileSide != -1)
			ga->addAction(new CubeTileChangeAction(w, cube, cube->tileUp, cube->tileFront, -1));
		if (w.z == 2 && cube->tileFront != -1)
			ga->addAction(new CubeTileChangeAction(w, cube, cube->tileUp, -1, cube->tileSide));

	}
	doAction(ga, browEdit);
}


void Map::wallFlipSelectedTextureHorizontal(BrowEdit* browEdit)
{
	auto gnd = rootNode->getComponent<Gnd>();
	auto gndRenderer = rootNode->getComponent<GndRenderer>();
	auto ga = new GroupAction();

	for (const auto& w : wallSelection)
	{
		auto cube = gnd->cubes[w.x][w.y];
		Gnd::Tile* tile = nullptr;
		if (w.z == 1 && cube->tileSide != -1)
			tile = gnd->tiles[cube->tileSide];
		if (w.z == 2 && cube->tileFront != -1)
			tile = gnd->tiles[cube->tileFront];
		if (!tile)
			continue;
		Gnd::Tile original(*tile);
		std::swap(tile->v1.x, tile->v2.x);
		std::swap(tile->v3.x, tile->v4.x);
		for(int i = 0; i < 4; i++)
			ga->addAction(new TileChangeAction<glm::vec2>(w, tile, &tile->texCoords[i], original.texCoords[i], "Change UV"));
	}
	doAction(ga, browEdit);
}

void Map::wallFlipSelectedTextureVertical(BrowEdit* browEdit)
{
	auto gnd = rootNode->getComponent<Gnd>();
	auto gndRenderer = rootNode->getComponent<GndRenderer>();
	auto ga = new GroupAction();

	for (const auto& w : wallSelection)
	{
		auto cube = gnd->cubes[w.x][w.y];
		Gnd::Tile* tile = nullptr;
		if (w.z == 1 && cube->tileSide != -1)
			tile = gnd->tiles[cube->tileSide];
		if (w.z == 2 && cube->tileFront != -1)
			tile = gnd->tiles[cube->tileFront];
		if (!tile)
			continue;
		Gnd::Tile original(*tile);
		std::swap(tile->v1.y, tile->v3.y);
		std::swap(tile->v2.y, tile->v4.y);
		for (int i = 0; i < 4; i++)
			ga->addAction(new TileChangeAction<glm::vec2>(w, tile, &tile->texCoords[i], original.texCoords[i], "Change UV"));
	}
	doAction(ga, browEdit);
}
