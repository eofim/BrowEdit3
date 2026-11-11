#pragma once

#include "Renderer.h"
#include <browedit/gl/Shader.h>
#include <browedit/util/Singleton.h>
#include <glm/glm.hpp>

class RswObject;
class Gnd;
class NodeRenderContext;

// Effect 45 as it looks in the client: ripple rings spreading over the ground and a few small lights floating around.
// The client draws this from code, so it is an approximation made of generated shapes (no files needed).
class PortalEffect : public Component
{
public:
	float radius = 8.0f;	// world units, multiplied by the object's Scale
};

class PortalRenderer : public Renderer
{
public:
	class PortalShader : public gl::Shader
	{
	public:
		PortalShader() : gl::Shader("data/shaders/sprchar", Uniforms::End) { bindUniforms(); }
		struct Uniforms { enum { projectionMatrix, cameraMatrix, modelMatrix, s_texture, color, depthBias, End }; };
		void bindUniforms() override
		{
			bindUniform(Uniforms::projectionMatrix, "projectionMatrix");
			bindUniform(Uniforms::cameraMatrix, "cameraMatrix");
			bindUniform(Uniforms::modelMatrix, "modelMatrix");
			bindUniform(Uniforms::s_texture, "s_texture");
			bindUniform(Uniforms::color, "color");
			bindUniform(Uniforms::depthBias, "depthBias");
		}
	};
	class PortalRenderContext : public Renderer::RenderContext, public util::Singleton<PortalRenderContext>
	{
	public:
		PortalShader* shader = nullptr;
		unsigned int glowTex = 0;
		unsigned int whiteTex = 0;
		glm::mat4 view = glm::mat4(1.0f);
		PortalRenderContext();
		virtual void preFrame(Node* rootNode, NodeRenderContext& context, std::vector<Renderer*>& renderers) override;
		virtual void postFrame(NodeRenderContext& context) override;
	};

	PortalRenderer();
	virtual void render(NodeRenderContext& context) override;

private:
	RswObject* rswObject = nullptr;
	Gnd* gnd = nullptr;
	PortalEffect* effect = nullptr;
};
