#pragma once

class UEditorEngine;

// ---------------------------------------------------------------------------
// Floating ImGui panel that drives the renderer-side particle smoke tests.
//
// Lives in its own ImGui window (separate z-layer) so the buttons aren't
// fighting the SharedViewportToolbar for screen real estate. The shared
// toolbar draws after FEditorPlayToolbarWidget::Render on the same content
// region, so anything added inline to the play toolbar after the Stop button
// ends up partially occluded.
//
// Transient — to remove all smoke-test scaffolding, delete:
//   - this file pair
//   - Render/Particle/ParticleSpriteSmokeTest.{h,cpp}
//   - Render/Particle/ParticleMeshSmokeTest.{h,cpp}
//   - Render/Particle/ParticleBeamSmokeTest.{h,cpp}
//   - Asset/Materials/Editor/DefaultParticleSprite.mat
//   - Asset/Materials/Editor/DefaultParticleMesh.mat
//   - Asset/Materials/Editor/DefaultParticleBeam.mat
// ...and remove the one-line FParticleSmokeTestPanel::Render(Editor) call
// from EditorPlayToolbarWidget.cpp.
// ---------------------------------------------------------------------------
namespace FParticleSmokeTestPanel
{
	// Draws a small floating ImGui window with Sprite/Mesh/Beam inject buttons.
	// Safe to call with a null Editor (no-ops).
	void Render(UEditorEngine* Editor);
	void Render(UEditorEngine* Editor, bool* bOpen);
}
