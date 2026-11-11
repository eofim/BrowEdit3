#pragma once

#include "Action.h"
#include <browedit/components/Rsw.h>
#include <vector>

// Swaps the list of tiles a model blocked through Auto-GAT, goes together with the GatTileChangeActions of the same step
class AutoGatFootprintAction : public Action
{
	RswModel* model;
	std::vector<RswModel::AutoGatTile> tiles;
public:
	AutoGatFootprintAction(RswModel* model, const std::vector<RswModel::AutoGatTile>& newTiles) : model(model), tiles(newTiles) {}

	virtual void perform(Map* map, BrowEdit* browEdit) override { std::swap(model->autoGatTiles, tiles); }
	virtual void undo(Map* map, BrowEdit* browEdit) override { std::swap(model->autoGatTiles, tiles); }
	virtual std::string str() override { return "Auto-GAT footprint"; }
};
