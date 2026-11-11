#include <glad/gl.h>
#include "PortalRenderer.h"
#include <browedit/util/ResourceManager.h>
#include <browedit/Node.h>
#include <browedit/NodeRenderer.h>
#include <browedit/components/Rsw.h>
#include <browedit/components/Gnd.h>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>
#include <cmath>

PortalRenderer::PortalRenderer()
{
	renderContext = PortalRenderContext::getInstance();
}

PortalRenderer::PortalRenderContext::PortalRenderContext() : shader(util::ResourceManager<gl::Shader>::load<PortalShader>())
{
	order = RendererDrawPriority::Str;
	// generated textures: a soft round glow and a plain white pixel
	unsigned char white[4] = { 255,255,255,255 };
	glGenTextures(1, &whiteTex);
	glBindTexture(GL_TEXTURE_2D, whiteTex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	std::vector<unsigned char> g(32 * 32 * 4);
	for (int y = 0; y < 32; y++) for (int x = 0; x < 32; x++)
	{
		float d = sqrtf((x - 15.5f) * (x - 15.5f) + (y - 15.5f) * (y - 15.5f)) / 16.0f;
		float a = std::max(0.0f, 1.0f - d); a = a * a;
		unsigned char* p = &g[((size_t)y * 32 + x) * 4];
		p[0] = p[1] = p[2] = 255; p[3] = (unsigned char)(a * 255);
	}
	glGenTextures(1, &glowTex);
	glBindTexture(GL_TEXTURE_2D, glowTex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 32, 32, 0, GL_RGBA, GL_UNSIGNED_BYTE, g.data());
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

void PortalRenderer::PortalRenderContext::preFrame(Node* rootNode, NodeRenderContext& context, std::vector<Renderer*>& renderers)
{
	view = context.viewMatrix;
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE);	// light: adds up
	glEnable(GL_DEPTH_TEST);
	glDepthMask(0);
	glDisable(GL_CULL_FACE);
	shader->use();
	shader->setUniform(PortalShader::Uniforms::projectionMatrix, context.projectionMatrix);
	shader->setUniform(PortalShader::Uniforms::cameraMatrix, context.viewMatrix);
	shader->setUniform(PortalShader::Uniforms::s_texture, 0);
	glEnableVertexAttribArray(0);
	glEnableVertexAttribArray(1);
	glDisableVertexAttribArray(2);
	glDisableVertexAttribArray(3);
	glDisableVertexAttribArray(4);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void PortalRenderer::PortalRenderContext::postFrame(NodeRenderContext& context)
{
	glDepthMask(1);
	glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
}

void PortalRenderer::render(NodeRenderContext& context)
{
	if (!rswObject) rswObject = node->getComponent<RswObject>();
	if (!gnd) gnd = node->root->getComponent<Gnd>();
	if (!effect) effect = node->getComponent<PortalEffect>();
	if (!rswObject || !gnd || !effect) return;
	auto ctx = dynamic_cast<PortalRenderContext*>(renderContext);
	auto shader = ctx->shader;
	const glm::mat4& view = ctx->view;
	const float t = context.time;
	float R = effect->radius * glm::max(0.1f, glm::abs(rswObject->scale.x));

	glm::mat4 model(1.0f);
	model = glm::scale(model, glm::vec3(1, 1, -1));
	model = glm::translate(model, glm::vec3(5 * gnd->width + rswObject->position.x, -rswObject->position.y + 0.2f, -10 - 5 * gnd->height + rswObject->position.z));
	shader->setUniform(PortalShader::Uniforms::modelMatrix, model);
	shader->setUniform(PortalShader::Uniforms::depthBias, R * 0.9f);	// keeps the flat rings from sinking into the ground

	auto offset = [&](float x, float y, float z) { return glm::vec3(view * glm::vec4(x, y, z, 0) * glm::vec4(1, 1, 1, 1)); };
	auto drawArray = [&](const std::vector<float>& v, GLenum mode)
	{
		glVertexAttribPointer(0, 3, GL_FLOAT, false, 5 * sizeof(float), &v[0]);
		glVertexAttribPointer(1, 2, GL_FLOAT, false, 5 * sizeof(float), &v[3]);
		glDrawArrays(mode, 0, (int)(v.size() / 5));
	};

	// faint glow on the ground
	{
		std::vector<float> fan;
		glm::vec3 c = offset(0, 0, 0);
		fan.insert(fan.end(), { c.x, c.y, 0, 0.5f, 0.5f });
		for (int k = 0; k <= 32; k++)
		{
			float a = k / 32.0f * 6.2831853f;
			glm::vec3 o = offset(cosf(a) * R, 0, sinf(a) * R);
			fan.insert(fan.end(), { o.x, o.y, 0, 0.5f + 0.5f * cosf(a), 0.5f + 0.5f * sinf(a) });
		}
		glBindTexture(GL_TEXTURE_2D, ctx->glowTex);
		shader->setUniform(PortalShader::Uniforms::color, glm::vec4(0.7f, 0.85f, 1.0f, 0.35f));
		drawArray(fan, GL_TRIANGLE_FAN);
	}
	// ripple rings spreading outwards
	glBindTexture(GL_TEXTURE_2D, ctx->whiteTex);
	for (int i = 0; i < 3; i++)
	{
		float ph = fmodf(t / 2.4f + i / 3.0f, 1.0f);
		float r = R * (0.12f + 0.88f * ph), w = R * (0.05f + 0.07f * ph);
		float a = powf(1.0f - ph, 1.4f) * 0.9f * glm::min(1.0f, ph * 6.0f);
		std::vector<float> strip;
		for (int k = 0; k <= 48; k++)
		{
			float ang = k / 48.0f * 6.2831853f;
			glm::vec3 o1 = offset(cosf(ang) * (r + w), 0, sinf(ang) * (r + w)), o2 = offset(cosf(ang) * r, 0, sinf(ang) * r);
			strip.insert(strip.end(), { o1.x, o1.y, 0, 0, 0 }); strip.insert(strip.end(), { o2.x, o2.y, 0, 0, 0 });
		}
		shader->setUniform(PortalShader::Uniforms::color, glm::vec4(0.8f, 0.92f, 1.0f, a));
		drawArray(strip, GL_TRIANGLE_STRIP);
	}
	// small lights floating around
	glBindTexture(GL_TEXTURE_2D, ctx->glowTex);
	for (int i = 0; i < 9; i++)
	{
		float s = i * 1.7f;
		float x = sinf(t * (0.35f + 0.07f * i) + s) * R * 0.95f, z = cosf(t * (0.29f + 0.05f * i) + s * 1.3f) * R * 0.95f;
		float y = 1.5f + R * 0.3f * (0.5f + 0.5f * sinf(t * (0.6f + 0.1f * i) + s));
		float tw = 0.45f + 0.55f * (0.5f + 0.5f * sinf(t * 2.3f + s * 2.0f));
		float sz = 0.28f * R * 0.5f + 0.15f;
		glm::vec3 c = offset(x, y, z);
		const float q[4][5] = { {c.x - sz, c.y - sz, 0, 0, 1}, {c.x - sz, c.y + sz, 0, 0, 0}, {c.x + sz, c.y + sz, 0, 1, 0}, {c.x + sz, c.y - sz, 0, 1, 1} };
		shader->setUniform(PortalShader::Uniforms::color, glm::vec4(1.0f, 1.0f, 0.95f, tw));
		glVertexAttribPointer(0, 3, GL_FLOAT, false, 5 * sizeof(float), &q[0][0]);
		glVertexAttribPointer(1, 2, GL_FLOAT, false, 5 * sizeof(float), &q[0][3]);
		glDrawArrays(GL_QUADS, 0, 4);
	}
}
