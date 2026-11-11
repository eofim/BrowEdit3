#pragma once

#include "Renderer.h"
#include <browedit/gl/Shader.h>
#include <browedit/util/Singleton.h>
#include <glm/glm.hpp>
#include <map>
#include <string>

namespace gl { class TexturePoT; }
class RswObject;
class Gnd;
class NodeRenderContext;

// One textured part (type 2D, 3D or CYLINDER) of a client effect, parameters straight from data/effectparts.json.
class FxEffect : public Component
{
public:
	std::string type;
	std::string texture;	// file below data\texture\effect, without extension unless given
	std::map<std::string, float> n;
	float num(const std::string& k, float def) const { auto it = n.find(k); return it == n.end() ? def : it->second; }
	bool has(const std::string& k) const { return n.find(k) != n.end(); }
};

class FxRenderer : public Renderer
{
public:
	class CylShader : public gl::Shader
	{
	public:
		CylShader() : gl::Shader("data/shaders/fxcyl", Uniforms::End) { bindUniforms(); }
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
	class FxRenderContext : public Renderer::RenderContext, public util::Singleton<FxRenderContext>
	{
	public:
		CylShader* cylShader = nullptr;
		FxRenderContext();
		virtual void preFrame(Node* rootNode, NodeRenderContext& context, std::vector<Renderer*>& renderers) override;
		virtual void postFrame(NodeRenderContext& context) override;
	};

	FxRenderer();
	~FxRenderer();
	void setEffect(FxEffect* e) { fx = e; }
	virtual void render(NodeRenderContext& context) override;

private:
	FxEffect* fx = nullptr;
	RswObject* rswObject = nullptr;
	Gnd* gnd = nullptr;
	gl::TexturePoT* tex = nullptr;
	bool triedLoad = false;

	void drawQuad(NodeRenderContext& context, const glm::mat4& modelMatrix, float cx, float cy, float hw, float hh, float angleDeg, const glm::vec4& color);
	void drawCylinder(NodeRenderContext& context, const glm::mat4& modelMatrix, float t, float tms);
};
