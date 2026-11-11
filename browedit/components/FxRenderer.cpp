#include <glad/gl.h>
#include "FxRenderer.h"
#include "SprRenderer.h"
#include <browedit/util/ResourceManager.h>
#include <browedit/util/FileIO.h>
#include <browedit/Node.h>
#include <browedit/NodeRenderer.h>
#include <browedit/components/Rsw.h>
#include <browedit/components/Gnd.h>
#include <browedit/gl/Texture.h>
#include <browedit/gl/Vertex.h>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <vector>

namespace
{
	// sizes / positions in the effect table are in "client units"; 1 unit = 5 map units, sizes additionally /35
	const float FX_POS = 5.0f;
	const float FX_SIZE = 1.0f / 7.0f;

	float lerpf(float a, float b, float t) { return a + (b - a) * t; }

	void setBlend(int mode)
	{
		glEnable(GL_BLEND);
		if (mode == 5 || mode == 0)
			glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
		else
			glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE, GL_ONE, GL_ONE);
	}
}

FxRenderer::FxRenderer()
{
	renderContext = FxRenderContext::getInstance();
}

FxRenderer::~FxRenderer()
{
	if (tex)
		util::ResourceManager<gl::TexturePoT>::unload(tex);
}

void FxRenderer::drawQuad(NodeRenderContext& context, const glm::mat4& modelMatrix, float cx, float cy, float hw, float hh, float angleDeg, const glm::vec4& color)
{
	auto shader = SprRenderer::SprRenderContext::getInstance()->shader;
	shader->use();
	shader->setUniform(SprRenderer::SprShader::Uniforms::projectionMatrix, context.projectionMatrix);
	shader->setUniform(SprRenderer::SprShader::Uniforms::cameraMatrix, context.viewMatrix);
	shader->setUniform(SprRenderer::SprShader::Uniforms::modelMatrix, modelMatrix);
	shader->setUniform(SprRenderer::SprShader::Uniforms::color, color);
	float ca = cosf(glm::radians(-angleDeg)), sa = sinf(glm::radians(-angleDeg));
	const float cs[4][4] = { {-hw, -hh, 0, 1}, {-hw, hh, 0, 0}, {hw, hh, 1, 0}, {hw, -hh, 1, 1} };
	std::vector<VertexP3T2> verts;
	verts.reserve(4);
	for (int i = 0; i < 4; i++)
		verts.push_back(VertexP3T2(glm::vec3(cx + cs[i][0] * ca - cs[i][1] * sa, cy + cs[i][0] * sa + cs[i][1] * ca, 0), glm::vec2(cs[i][2], cs[i][3])));
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glEnableVertexAttribArray(0);
	glEnableVertexAttribArray(1);
	glVertexAttribPointer(0, 3, GL_FLOAT, false, sizeof(VertexP3T2), verts[0].data);
	glVertexAttribPointer(1, 2, GL_FLOAT, false, sizeof(VertexP3T2), verts[0].data + 3);
	glDrawArrays(GL_QUADS, 0, 4);
}

void FxRenderer::drawCylinder(NodeRenderContext& context, const glm::mat4& modelMatrix, float t, float tms)
{
	auto shader = FxRenderContext::getInstance()->cylShader;
	shader->use();
	shader->setUniform(CylShader::Uniforms::projectionMatrix, context.projectionMatrix);
	shader->setUniform(CylShader::Uniforms::cameraMatrix, context.viewMatrix);

	float r0 = fx->num("bottomSize", 1) * 10.0f, r1 = fx->num("topSize", 1) * 10.0f;
	float h = fx->num("height", 35) / 35.0f * 10.0f;
	float z0 = fx->num("posZ", 0) * FX_POS;
	float alpha = fx->num("alphaMax", 1);
	if (fx->num("fade", 0) > 0.5f || fx->num("fadeOut", 0) > 0.5f)
		alpha *= glm::clamp((1.0f - t) * 3.0f, 0.0f, 1.0f);
	alpha *= glm::clamp(t * 6.0f, 0.0f, 1.0f);
	shader->setUniform(CylShader::Uniforms::color, glm::vec4(fx->num("red", 1), fx->num("green", 1), fx->num("blue", 1), alpha));

	float spin = fx->num("rotate", 0) > 0.5f ? tms * 0.2f : tms * 0.05f;
	float grow = 1.0f + (fx->num("animation", 0) >= 2 ? t * 0.5f : 0.0f);
	int segments = 24;
	float arc = fx->num("semiCircle", 0) > 0.5f ? 3.14159265f : 6.2831853f;
	std::vector<VertexP3T2> verts;
	verts.reserve(segments * 4);
	for (int i = 0; i < segments; i++)
	{
		float a0 = glm::radians(spin) + arc * i / segments, a1 = glm::radians(spin) + arc * (i + 1) / segments;
		float u0 = (float)i / segments, u1 = (float)(i + 1) / segments;
		verts.push_back(VertexP3T2(glm::vec3(cosf(a0) * r0 * grow, z0, sinf(a0) * r0 * grow), glm::vec2(u0, 1)));
		verts.push_back(VertexP3T2(glm::vec3(cosf(a0) * r1 * grow, z0 + h, sinf(a0) * r1 * grow), glm::vec2(u0, 0)));
		verts.push_back(VertexP3T2(glm::vec3(cosf(a1) * r1 * grow, z0 + h, sinf(a1) * r1 * grow), glm::vec2(u1, 0)));
		verts.push_back(VertexP3T2(glm::vec3(cosf(a1) * r0 * grow, z0, sinf(a1) * r0 * grow), glm::vec2(u1, 1)));
	}
	shader->setUniform(CylShader::Uniforms::modelMatrix, modelMatrix);
	bool cull = glIsEnabled(GL_CULL_FACE);
	glDisable(GL_CULL_FACE);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glEnableVertexAttribArray(0);
	glEnableVertexAttribArray(1);
	glVertexAttribPointer(0, 3, GL_FLOAT, false, sizeof(VertexP3T2), verts[0].data);
	glVertexAttribPointer(1, 2, GL_FLOAT, false, sizeof(VertexP3T2), verts[0].data + 3);
	glDrawArrays(GL_QUADS, 0, (int)verts.size());
	if (cull)
		glEnable(GL_CULL_FACE);
}

void FxRenderer::render(NodeRenderContext& context)
{
	if (!fx)
		fx = node->getComponent<FxEffect>();
	if (!rswObject)
		rswObject = node->getComponent<RswObject>();
	if (!gnd)
		gnd = node->root->getComponent<Gnd>();
	if (!fx || !rswObject || !gnd)
		return;

	if (!triedLoad)
	{
		triedLoad = true;
		std::string name = fx->texture;
		for (auto& c : name) if (c == '/') c = '\\';
		std::vector<std::string> bases = { name };
		if (name.rfind("effect\\", 0) == 0)
			bases.push_back(name.substr(7));
		for (auto& b : bases)
		{
			for (const char* ext : { "", ".tga", ".bmp", ".png", ".jpg" })
			{
				std::string path = "data\\texture\\effect\\" + b + ext;
				if (!tex && util::FileIO::exists(path))
					tex = util::ResourceManager<gl::TexturePoT>::load(path);
			}
		}
	}
	if (!tex || !tex->loaded)
		return;

	float duration = glm::max(50.0f, fx->num("duration", 1000));
	int dup = (int)glm::max(1.0f, fx->num("duplicate", 1));
	float gap = fx->num("timeBetweenDupli", 150);
	float tm = fmodf(context.time * 1000.0f, duration + gap * (dup - 1) + 600.0f);

	glm::mat4 base(1.0f);
	base = glm::scale(base, glm::vec3(1, 1, -1));
	base = glm::translate(base, glm::vec3(5 * gnd->width + rswObject->position.x, -rswObject->position.y, -10 - 5 * gnd->height + rswObject->position.z));

	tex->bind();
	int blend = (int)fx->num("blendMode", fx->type == "CYLINDER" ? 2 : 2);
	setBlend(blend);

	for (int d = 0; d < dup; d++)
	{
		float td = tm - d * gap;
		if (td < 0 || td > duration)
			continue;
		float t = td / duration;

		if (fx->type == "CYLINDER")
		{
			drawCylinder(context, base, t, td);
			continue;
		}

		auto pair = [&](const char* start, const char* end, const char* single, float def, bool& found)
		{
			found = true;
			if (fx->has(start) && fx->has(end)) return lerpf(fx->num(start, 0), fx->num(end, 0), t);
			if (single && fx->has(single)) return fx->num(single, 0);
			found = false;
			return def;
		};
		bool f;
		float sx = pair("sizeStartX", "sizeEndX", "sizeX", 0, f);
		if (!f) sx = pair("sizeStart", "sizeEnd", "size", 20, f);
		float sy = pair("sizeStartY", "sizeEndY", "sizeY", 0, f);
		if (!f) sy = pair("sizeStart", "sizeEnd", "size", sx, f);
		float px = pair("posxStart", "posxEnd", "posx", 0, f);
		float py = pair("posyStart", "posyEnd", "posy", 0, f);
		float pz = pair("poszStart", "poszEnd", "posz", 0, f);

		float angle = fx->num("angle", 0);
		if (fx->has("toAngle")) angle = lerpf(angle, fx->num("toAngle", 0), t);
		float alpha = fx->num("alphaMax", 1);
		if (fx->num("fadeIn", 0) > 0.5f) alpha *= glm::clamp(t * 4.0f, 0.0f, 1.0f);
		if (fx->num("fadeOut", 0) > 0.5f) alpha *= glm::clamp((1.0f - t) * 3.0f, 0.0f, 1.0f);
		glm::vec4 color(fx->num("red", 1), fx->num("green", 1), fx->num("blue", 1), alpha);

		glm::mat4 m = glm::translate(base, glm::vec3(px * FX_POS, pz * FX_POS, py * FX_POS));
		float hw = sx * FX_SIZE * 0.5f, hh = sy * FX_SIZE * 0.5f;
		float cx = 0, cy = 0;
		if (fx->type == "2D")	// one end of the stretched image stays on the object
		{
			float a = glm::radians(angle);
			cx = -sinf(a) * hh; cy = cosf(a) * hh;
		}
		drawQuad(context, m, cx, cy, hw, hh, angle, color);
	}
}

FxRenderer::FxRenderContext::FxRenderContext() : cylShader(util::ResourceManager<gl::Shader>::load<CylShader>())
{
	cylShader->use();
	cylShader->setUniform(CylShader::Uniforms::s_texture, 0);
	order = RendererDrawPriority::Str;
}

void FxRenderer::FxRenderContext::preFrame(Node* rootNode, NodeRenderContext& context, std::vector<Renderer*>& renderers)
{
	glEnable(GL_BLEND);
	glEnable(GL_DEPTH_TEST);
	glDepthMask(0);
	glEnableVertexAttribArray(0);
	glEnableVertexAttribArray(1);
	glDisableVertexAttribArray(2);
	glDisableVertexAttribArray(3);
	glDisableVertexAttribArray(4);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void FxRenderer::FxRenderContext::postFrame(NodeRenderContext& context)
{
	glDepthMask(1);
	glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
}
