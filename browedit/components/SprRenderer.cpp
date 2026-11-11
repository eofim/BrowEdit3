#include <glad/gl.h>
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
#include <iostream>
#include <iterator>
#include <cstring>

namespace
{
	struct Reader
	{
		std::vector<unsigned char> d; size_t p = 0; bool bad = false;
		bool has(size_t n) { if (p + n > d.size()) { bad = true; return false; } return true; }
		int u8() { if (!has(1)) return 0; return d[p++]; }
		int u16() { if (!has(2)) return 0; int v = d[p] | (d[p + 1] << 8); p += 2; return v; }
		int i32() { if (!has(4)) return 0; int v; memcpy(&v, &d[p], 4); p += 4; return v; }
		float f32() { if (!has(4)) return 0; float v; memcpy(&v, &d[p], 4); p += 4; return v; }
		void skip(size_t n) { if (has(n)) p += n; }
	};

	bool slurp(const std::string& name, Reader& r)
	{
		if (!util::FileIO::exists(name))
			return false;
		auto f = util::FileIO::open(name);
		if (!f)
			return false;
		r.d.assign(std::istreambuf_iterator<char>(*f), std::istreambuf_iterator<char>());
		delete f;
		return r.d.size() > 4;
	}
}

SprAct::SprAct(const std::string& baseName)
{
	Reader spr, act;
	if (!slurp(baseName + ".spr", spr) || !slurp(baseName + ".act", act))
	{
		std::cerr << "SPR/ACT: unable to open " << baseName << std::endl;
		return;
	}

	// ---- SPR ----
	if (spr.d[0] != 'S' || spr.d[1] != 'P')
		return;
	spr.p = 2;
	int minor = spr.u8(), major = spr.u8();
	int sver = major * 10 + minor;
	int indexedCount = spr.u16();
	int rgbaCount = sver >= 20 ? spr.u16() : 0;
	std::vector<std::vector<unsigned char>> idx(indexedCount);
	indexed.resize(indexedCount);
	for (int i = 0; i < indexedCount && !spr.bad; i++)
	{
		int w = spr.u16(), h = spr.u16();
		indexed[i].w = w; indexed[i].h = h;
		idx[i].assign((size_t)w * h, 0);
		if (sver >= 21)
		{
			int csize = spr.u16();
			size_t end = spr.p + csize, o = 0;
			while (spr.p < end && o < idx[i].size() && !spr.bad)
			{
				int c = spr.u8();
				if (c == 0)
				{
					int n = spr.u8();
					if (n == 0) n = 1;
					o += n;	// zeros
				}
				else
					idx[i][o++] = (unsigned char)c;
			}
			spr.p = end;
		}
		else
		{
			size_t n = (size_t)w * h;
			if (spr.has(n)) { memcpy(idx[i].data(), &spr.d[spr.p], n); spr.p += n; }
		}
	}
	rgba.resize(rgbaCount);
	for (int i = 0; i < rgbaCount && !spr.bad; i++)
	{
		int w = spr.u16(), h = spr.u16();
		rgba[i].w = w; rgba[i].h = h;
		rgba[i].rgba.assign((size_t)w * h * 4, 0);
		size_t n = (size_t)w * h * 4;
		if (!spr.has(n)) break;
		for (int y = 0; y < h; y++)			// stored as ABGR, bottom row first
			for (int x = 0; x < w; x++)
			{
				const unsigned char* s = &spr.d[spr.p + ((size_t)(h - 1 - y) * w + x) * 4];
				unsigned char* t = &rgba[i].rgba[((size_t)y * w + x) * 4];
				t[0] = s[3]; t[1] = s[2]; t[2] = s[1]; t[3] = s[0];
			}
		spr.p += n;
	}
	unsigned char pal[1024];
	memset(pal, 0, sizeof(pal));
	if (sver >= 11 && spr.d.size() >= 1024)
		memcpy(pal, &spr.d[spr.d.size() - 1024], 1024);
	for (int i = 0; i < indexedCount; i++)
	{
		auto& im = indexed[i];
		im.rgba.assign((size_t)im.w * im.h * 4, 0);
		for (size_t k = 0; k < idx[i].size(); k++)
		{
			int c = idx[i][k];
			if (c == 0) continue;
			im.rgba[k * 4 + 0] = pal[c * 4 + 0]; im.rgba[k * 4 + 1] = pal[c * 4 + 1]; im.rgba[k * 4 + 2] = pal[c * 4 + 2]; im.rgba[k * 4 + 3] = 255;
		}
	}

	// ---- ACT ----
	if (act.d[0] != 'A' || act.d[1] != 'C')
		return;
	act.p = 2;
	minor = act.u8(); major = act.u8();
	int ver = major * 10 + minor;
	int actionCount = act.u16();
	act.skip(10);
	actions.resize(actionCount);
	for (int a = 0; a < actionCount && !act.bad; a++)
	{
		int motionCount = act.i32();
		if (motionCount < 0 || motionCount > 100000) { act.bad = true; break; }
		actions[a].motions.resize(motionCount);
		for (int m = 0; m < motionCount && !act.bad; m++)
		{
			act.skip(32);
			int layerCount = act.i32();
			if (layerCount < 0 || layerCount > 10000) { act.bad = true; break; }
			actions[a].motions[m].layers.resize(layerCount);
			for (int l = 0; l < layerCount && !act.bad; l++)
			{
				auto& L = actions[a].motions[m].layers[l];
				L.x = act.i32(); L.y = act.i32(); L.sprIndex = act.i32(); L.mirror = act.i32();
				if (ver >= 20)
				{
					int r = act.u8(), g = act.u8(), b = act.u8(), al = act.u8();
					L.color = glm::vec4(r, g, b, al) / 255.0f;
					L.scale.x = act.f32();
					L.scale.y = ver >= 24 ? act.f32() : L.scale.x;
					L.rotation = (float)act.i32();
					L.sprType = act.i32();
					if (ver >= 25) { act.i32(); act.i32(); }
				}
			}
			if (ver >= 20) act.i32();	// sound id
			if (ver >= 23)
			{
				int ap = act.i32();
				if (ap < 0 || ap > 1000) { act.bad = true; break; }
				act.skip((size_t)ap * 16);
			}
		}
	}
	if (!act.bad && ver >= 21)
	{
		int sounds = act.i32();
		if (sounds >= 0 && sounds < 100000) act.skip((size_t)sounds * 40);
		if (ver >= 22)
			for (int a = 0; a < actionCount; a++)
				actions[a].interval = act.f32();
	}
	ok = actions.size() > 0 && actions[0].motions.size() > 0;
}

SprAct::~SprAct() {}

SprAct::Image* SprAct::image(const Layer& l)
{
	auto& v = l.sprType == 1 ? rgba : indexed;
	if (l.sprIndex < 0 || l.sprIndex >= (int)v.size())
		return nullptr;
	return &v[l.sprIndex];
}

gl::Texture* SprAct::texture(const Layer& l)
{
	auto im = image(l);
	if (!im || im->w <= 0 || im->h <= 0 || im->rgba.empty())
		return nullptr;
	if (!im->tex)
	{
		im->tex = std::make_unique<gl::Texture>(im->w, im->h);
		glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
		im->tex->setSubImage((char*)im->rgba.data(), 0, 0, im->w, im->h);
	}
	return im->tex.get();
}


SprRenderer::SprRenderer()
{
	renderContext = SprRenderContext::getInstance();
}

SprRenderer::~SprRenderer()
{
	if (sprAct)
		util::ResourceManager<SprAct>::unload(sprAct);
}

void SprRenderer::render(NodeRenderContext& context)
{
	if (!rswObject)
		rswObject = node->getComponent<RswObject>();
	if (!gnd)
		gnd = node->root->getComponent<Gnd>();
	if (!sprEffect)
		sprEffect = node->getComponent<SprEffect>();
	if (!sprEffect || !rswObject || !gnd)
		return;
	if (!triedLoad)
	{
		triedLoad = true;
		sprAct = util::ResourceManager<SprAct>::load("data\\sprite\\\xC0\xCC\xC6\xD1\xC6\xAE\\" + sprEffect->file);
	}
	if (!sprAct || !sprAct->ok)
		return;

	auto& action = sprAct->actions[0];
	float delayMs = glm::max(1.0f, action.interval * 25.0f);
	int motionIdx = (int)(context.time * 1000.0f / delayMs) % (int)action.motions.size();
	auto& motion = action.motions[motionIdx];

	auto shader = dynamic_cast<SprRenderContext*>(renderContext)->shader;
	glm::mat4 modelMatrix(1.0f);
	modelMatrix = glm::scale(modelMatrix, glm::vec3(1, 1, -1));
	modelMatrix = glm::translate(modelMatrix, glm::vec3(5 * gnd->width + rswObject->position.x, -rswObject->position.y, -10 - 5 * gnd->height + rswObject->position.z));
	shader->setUniform(SprShader::Uniforms::modelMatrix, modelMatrix);

	float ps = sprEffect->pixelSize;
	glm::vec2 os(glm::abs(rswObject->scale.x), glm::abs(rswObject->scale.y));
	// Param 1 / Param 2 of the effect set width / height (35 = the default size), so they can be tuned live
	if (auto re = node->getComponent<RswEffect>())
	{
		if (re->param1 > 0) os.x *= re->param1 / 35.0f;
		if (re->param2 > 0) os.y *= re->param2 / 35.0f;
	}	// the object's Scale in the properties window
	for (auto& l : motion.layers)
	{
		auto tex = sprAct->texture(l);
		auto im = sprAct->image(l);
		if (!tex || !im)
			continue;
		float hw = im->w * 0.5f * l.scale.x * ps * os.x, hh = im->h * 0.5f * l.scale.y * ps * os.y;
		float cx = l.x * ps * os.x, cy = -l.y * ps * os.y;
		float ca = cosf(glm::radians(-l.rotation)), sa = sinf(glm::radians(-l.rotation));
		float u0 = l.mirror ? 1.0f : 0.0f, u1 = l.mirror ? 0.0f : 1.0f;
		const float cs[4][4] = { {-hw, -hh, u0, 1}, {-hw, hh, u0, 0}, {hw, hh, u1, 0}, {hw, -hh, u1, 1} };
		std::vector<VertexP3T2> verts;
		verts.reserve(4);
		for (int i = 0; i < 4; i++)
			verts.push_back(VertexP3T2(glm::vec3(cx + cs[i][0] * ca - cs[i][1] * sa, cy + cs[i][0] * sa + cs[i][1] * ca, 0), glm::vec2(cs[i][2], cs[i][3])));
		shader->setUniform(SprShader::Uniforms::color, l.color);
		tex->bind();
		glVertexAttribPointer(0, 3, GL_FLOAT, false, sizeof(VertexP3T2), verts[0].data);
		glVertexAttribPointer(1, 2, GL_FLOAT, false, sizeof(VertexP3T2), verts[0].data + 3);
		glDrawArrays(GL_QUADS, 0, 4);
	}
}

SprRenderer::SprRenderContext::SprRenderContext() : shader(util::ResourceManager<gl::Shader>::load<SprShader>())
{
	shader->use();
	shader->setUniform(SprShader::Uniforms::s_texture, 0);
	order = RendererDrawPriority::Str;
}

void SprRenderer::SprRenderContext::preFrame(Node* rootNode, NodeRenderContext& context, std::vector<Renderer*>& renderers)
{
	glEnable(GL_BLEND);
	glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
	glEnable(GL_DEPTH_TEST);
	glDepthMask(0);
	shader->use();
	shader->setUniform(SprShader::Uniforms::projectionMatrix, context.projectionMatrix);
	shader->setUniform(SprShader::Uniforms::cameraMatrix, context.viewMatrix);
	shader->setUniform(SprShader::Uniforms::color, glm::vec4(1, 1, 1, 1));
	glEnableVertexAttribArray(0);
	glEnableVertexAttribArray(1);
	glDisableVertexAttribArray(2);
	glDisableVertexAttribArray(3);
	glDisableVertexAttribArray(4);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void SprRenderer::SprRenderContext::postFrame(NodeRenderContext& context)
{
	glDepthMask(1);
}
