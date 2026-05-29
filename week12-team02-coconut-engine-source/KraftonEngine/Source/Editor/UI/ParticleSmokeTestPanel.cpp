#include "Editor/UI/ParticleSmokeTestPanel.h"

#include "Editor/EditorEngine.h"
#include "Render/Particle/ParticleSpriteSmokeTest.h"
#include "Render/Particle/ParticleMeshSmokeTest.h"
#include "Render/Particle/ParticleBeamSmokeTest.h"
#include "Render/Pipeline/Renderer.h"
#include "Render/Device/D3DDevice.h"

#include "ImGui/imgui.h"

void FParticleSmokeTestPanel::Render(UEditorEngine* Editor)
{
	Render(Editor, nullptr);
}

void FParticleSmokeTestPanel::Render(UEditorEngine* Editor, bool* bOpen)
{
	if (!Editor) return;

	// Floating window — separate z-layer above the play toolbar / shared toolbar.
	// AlwaysAutoResize keeps it tight to the buttons; FirstUseEver lets the user
	// drag it elsewhere if it's in the way.
	ImGui::SetNextWindowPos(ImVec2(20.0f, 80.0f), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowBgAlpha(0.85f);
	if (ImGui::Begin("Particle Smoke Tests##transient", bOpen,
		ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse))
	{
		UWorld* World = Editor->GetWorld();
		ID3D11Device* Device = Editor->GetRenderer().GetFD3DDevice().GetDevice();

		if (ImGui::Button("Sprite Ring"))
		{
			UMaterial* Mat = ParticleSpriteSmokeTest::GetDefaultMaterial();
			ParticleSpriteSmokeTest::InjectIntoWorld(World, Mat);
		}

		ImGui::SameLine();

		if (ImGui::Button("Mesh Ring"))
		{
			UMaterial*   Mat  = ParticleMeshSmokeTest::GetDefaultMaterial();
			UStaticMesh* Mesh = ParticleMeshSmokeTest::GetDefaultMesh(Device);
			ParticleMeshSmokeTest::InjectIntoWorld(World, Mat, Mesh);
		}

		ImGui::SameLine();

		if (ImGui::Button("Beam Starburst"))
		{
			UMaterial* Mat = ParticleBeamSmokeTest::GetDefaultMaterial();
			ParticleBeamSmokeTest::InjectIntoWorld(World, Mat);
		}
	}
	ImGui::End();
}
