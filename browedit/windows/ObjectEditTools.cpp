#include <browedit/BrowEdit.h>
#include <browedit/HotkeyRegistry.h>
#include <imgui.h>
#include <browedit/Icons.h>

void BrowEdit::showObjectEditToolsWindow()
{
	ImGui::Begin("Object Edit Tools");
	hotkeyButton("Copy", HotkeyAction::Global_Copy);
	ImGui::SameLine();
	hotkeyButton("Paste", HotkeyAction::Global_Paste);
	ImGui::SameLine();
	hotkeyButton("Delete", HotkeyAction::ObjectEdit_Delete);

	hotkeyButton("Flip horizontally", HotkeyAction::ObjectEdit_FlipHorizontal);
	ImGui::SameLine();
	hotkeyButton("Flip vertically", HotkeyAction::ObjectEdit_FlipVertical);

	hotkeyButton("Invert Scale X", HotkeyAction::ObjectEdit_InvertScaleX);
	ImGui::SameLine();
	hotkeyButton("Invert Scale Y", HotkeyAction::ObjectEdit_InvertScaleY);
	ImGui::SameLine();
	hotkeyButton("Invert Scale Z", HotkeyAction::ObjectEdit_InvertScaleZ);

	hotkeyButton("Create Prefab", HotkeyAction::ObjectEdit_CreatePrefab);
	hotkeyButton("Focus on selection", HotkeyAction::ObjectEdit_FocusOnSelection);

	ImGui::Checkbox("Use gridsize for nudge distance", &useGridForNudge);
	if(!useGridForNudge)
		ImGui::InputFloat("Nudge distance", &nudgeDistance);
	hotkeyButton("Nudge X Negative", HotkeyAction::ObjectEdit_NudgeXNeg);
	ImGui::SameLine();
	hotkeyButton("Nudge X Positive", HotkeyAction::ObjectEdit_NudgeXPos);
	hotkeyButton("Nudge Y Negative", HotkeyAction::ObjectEdit_NudgeYNeg);
	ImGui::SameLine();
	hotkeyButton("Nudge Y Positive", HotkeyAction::ObjectEdit_NudgeYPos);
	hotkeyButton("Nudge Z Negative", HotkeyAction::ObjectEdit_NudgeZNeg);
	ImGui::SameLine();
	hotkeyButton("Nudge Z Positive", HotkeyAction::ObjectEdit_NudgeZPos);

	if (activeMapView)
	{
		if (toolBarToggleButton("localglobal", activeMapView->pivotPoint == MapView::PivotPoint::Local ? ICON_PIVOT_ROT_LOCAL : ICON_PIVOT_ROT_GLOBAL, false, "Changes the pivot point for rotations", config.toolbarButtonsObjectEdit))
		{
			if (activeMapView->pivotPoint == MapView::PivotPoint::Local)
				activeMapView->pivotPoint = MapView::PivotPoint::GroupCenter;
			else
				activeMapView->pivotPoint = MapView::PivotPoint::Local;
		}
	}
	ImGui::Checkbox("Use gridsize for nudge distance", &useGridForNudge);
	if (!useGridForNudge)
		ImGui::InputFloat("Rotate distance", &rotateDistance);
	hotkeyButton("Rotate X Negative", HotkeyAction::ObjectEdit_RotXNeg);
	ImGui::SameLine();
	hotkeyButton("Rotate X Positive", HotkeyAction::ObjectEdit_RotXPos);
	hotkeyButton("Rotate Y Negative", HotkeyAction::ObjectEdit_RotYNeg);
	ImGui::SameLine();
	hotkeyButton("Rotate Y Positive", HotkeyAction::ObjectEdit_RotYPos);
	hotkeyButton("Rotate Z Negative", HotkeyAction::ObjectEdit_RotZNeg);
	ImGui::SameLine();
	hotkeyButton("Rotate Z Positive", HotkeyAction::ObjectEdit_RotZPos);

	hotkeyButton("Randomize X rotation", HotkeyAction::ObjectEdit_RandomXRotation);
	hotkeyButton("Randomize Y rotation", HotkeyAction::ObjectEdit_RandomYRotation);
	hotkeyButton("Randomize Z rotation", HotkeyAction::ObjectEdit_RandomZRotation);

	ImGui::Separator();
	ImGui::Checkbox("Random scale on place", &randomScaleOnPlace);
	if (ImGui::IsItemHovered())
		ImGui::SetTooltip("Each single model you place (trees, bushes...) gets a random uniform scale between min and max.");
	if (randomScaleOnPlace)
	{
		ImGui::DragFloat("Scale min", &randomScaleMin, 0.01f, 0.1f, 5.0f);
		ImGui::DragFloat("Scale max", &randomScaleMax, 0.01f, 0.1f, 5.0f);
	}
	ImGui::Checkbox("Random Y rotation on place", &randomRotYOnPlace);
	if (ImGui::IsItemHovered())
		ImGui::SetTooltip("Each single model you place gets a random rotation around the vertical (green) axis, 0-360.");
	ImGui::Separator();
	ImGui::Checkbox("Smart model GAT", &autoGatOnPlace);
	if (ImGui::IsItemHovered())
		ImGui::SetTooltip("Placing a model marks the GAT tiles under its ground contact area as not walkable.\nMoving, rotating, scaling or nudging it moves those tiles along, deleting it gives them back.\nTiles you edited by hand are left alone. Every step is one undo.\nOnly models placed with this ticked are followed.");
	if (autoGatOnPlace)
	{
		ImGui::DragFloat("Ground contact height", &autoGatBandHeight, 0.1f, 0.0f, 100.0f);
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("Model surfaces within this height above the terrain count as touching the ground.\nLower = only the base of the model, higher = also block things hanging slightly above ground.");
		ImGui::Checkbox("Fill inside", &autoGatFillInterior);
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("Also block the area enclosed by the model, like the floor inside a house.");
		if (autoGatFillInterior)
		{
			ImGui::DragInt("Doorway gap to close", &autoGatCloseGap, 0.1f, 0, 3);
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("Openings up to this many tiles on each side of a doorway still count as closed,\nso the inside gets filled even when the house has a door. 0 = only fully closed walls.");
		}
		ImGui::DragInt("GAT type", &autoGatType, 0.1f, 0, 15);
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("GAT type to apply (1 = not walkable).\nModels with 'Force gat tiles on model' set use that value instead.");
	}


	ImGui::End();
}