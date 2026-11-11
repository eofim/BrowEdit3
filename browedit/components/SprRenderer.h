#pragma once

#include "Renderer.h"
#include <browedit/gl/Shader.h>
#include <browedit/util/Singleton.h>
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <memory>

namespace gl { class Texture; }
class RswObject;
class Gnd;
class NodeRenderContext;

// A .spr/.act pair (sprite images + animation), loaded from "data\sprite\<folder>\<name>".
class SprAct
{
public:
	struct Layer { int x = 0, y = 0, sprIndex = -1, mirror = 0, sprType = 0; glm::vec4 color = glm::vec4(1); glm::vec2 scale = glm::vec2(1); float rotation = 0; };
	struct Motion { std::vector<Layer> layers; };
	struct Action { std::vector<Motion> motions; float interval = 4.0f; };
	struct Image { int w = 0, h = 0; std::vector<unsigned char> rgba; std::unique_ptr<gl::Texture> tex; };

	std::vector<Action> actions;
	std::vector<Image> indexed, rgba;
	bool ok = false;

	SprAct(const std::string& baseName);	// full path without extension, iso-8859-1
	~SprAct();
	gl::Texture* texture(const Layer& l);
	Image* image(const Layer& l);
};

// Marks a node as an effect that shows a sprite animation.
class SprEffect : public Component
{
public:
	std::string file;		// name in the effect sprite folder, iso-8859-1, no extension
	float pixelSize = 0.4f;	// world units per sprite pixel
	bool dirty = true;
};

class SprRenderer : public Renderer
{
public:
	class SprShader : public gl::Shader
	{
	public:
		SprShader() : gl::Shader("data/shaders/spr", Uniforms::End) { bindUniforms(); }
		struct Uniforms { enum { projectionMatrix, cameraMatrix, modelMatrix, s_texture, color, End }; };
		void bindUniforms() override
		{
			bindUniform(Uniforms::projectionMatrix, "projectionMatrix");
			bindUniform(Uniforms::cameraMatrix, "cameraMatrix");
			bindUniform(Uniforms::modelMatrix, "modelMatrix");
			bindUniform(Uniforms::s_texture, "s_texture");
			bindUniform(Uniforms::color, "color");
		}
	};
	class SprRenderContext : public Renderer::RenderContext, public util::Singleton<SprRenderContext>
	{
	public:
		SprShader* shader = nullptr;
		SprRenderContext();
		virtual void preFrame(Node* rootNode, NodeRenderContext& context, std::vector<Renderer*>& renderers) override;
		virtual void postFrame(NodeRenderContext& context) override;
	};

	SprRenderer();
	void setSprEffect(SprEffect* e) { sprEffect = e; }
	~SprRenderer();
	virtual void render(NodeRenderContext& context) override;

private:
	SprEffect* sprEffect = nullptr;
	RswObject* rswObject = nullptr;
	Gnd* gnd = nullptr;
	SprAct* sprAct = nullptr;
	bool triedLoad = false;
};
