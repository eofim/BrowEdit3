#include <browedit/BrowEdit.h>
#include <browedit/Lightmapper.h>
#include <browedit/Map.h>
#include <browedit/Node.h>
#include <browedit/components/Gnd.h>
#include <browedit/components/Rsw.h>
#include <glm/gtc/type_ptr.hpp>

void BrowEdit::showLightmapSettingsWindow()
{
	if (windowData.openLightmapSettings)
	{
		ImGui::OpenPopup("Lightmap Settings");
		windowData.openLightmapSettings = false;
	}

	ImVec2 center = ImGui::GetMainViewport()->GetCenter();
	ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
	if (ImGui::BeginPopupModal("Lightmap Settings"))
	{
		auto gnd = lightmapper->map->rootNode->getComponent<Gnd>();
		auto rsw = lightmapper->map->rootNode->getComponent<Rsw>();
		auto& settings = rsw->lightmapSettings;
		ImGui::DragInt("Quality", &settings.quality, 1, 1, 10);
		ImGui::Checkbox("Shadows", &settings.shadows);
		ImGui::Checkbox("Height Edit Mode Selection Only", &settings.heightSelectionOnly);
		ImGui::Checkbox("Old shadow formula (v3.561 and below)", &config.additiveShadow);
		ImGui::Checkbox("Run on GPU", &settings.runOnGpu);
		ImGui::Checkbox("Soft shadows (faded edges)", &settings.softShadows);
		if (settings.softShadows)
		{
			ImGui::SliderInt("Soft shadow samples", &settings.softSamples, 4, 64);
			ImGui::SliderFloat("Sun softness (degrees)", &settings.softAngle, 0.5f, 12.0f, "%.1f");
			ImGui::SliderFloat("Point/spot light size", &settings.lightRadius, 0.5f, 30.0f, "%.1f");
			ImGui::TextWrapped("Works on the GPU and the CPU lightmapper, and in the live lightmap. It takes about as many times longer as the sample count. Shadows stay sharp right where they touch the object and fade the further they fall.");
		}
		ImGui::Checkbox("Skip 'backside.bmp' texture", &config.ignoreBacksideTexture);
		ImGui::DragInt2("Generate Range X", glm::value_ptr(settings.rangeX), 1, 0, gnd->width);
		ImGui::DragInt2("Generate Range Y", glm::value_ptr(settings.rangeY), 1, 0, gnd->height);
		ImGui::DragInt("Thread Count", &config.lightmapperThreadCount, 1, 1, 32);
		ImGui::DragInt("Update Time", &config.lightmapperRefreshTimer, 1, 1, 600);

		if (ImGui::Button("Lightmap!"))
		{
			settings.additiveShadow = config.additiveShadow;
			settings.ignoreBacksideTexture = config.ignoreBacksideTexture;
			config.save();
			lightmapper->begin();
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel"))
		{
			delete lightmapper;
			lightmapper = nullptr;
			ImGui::CloseCurrentPopup();
		}

		ImGui::EndPopup();
	}
}
