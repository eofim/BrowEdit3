#include "ModelVariants.h"
#include <browedit/BrowEdit.h>
#include <browedit/Map.h>
#include <browedit/Node.h>
#include <browedit/MapView.h>
#include <browedit/components/Rsw.h>
#include <browedit/components/Rsm.h>
#include <browedit/actions/GroupAction.h>
#include <browedit/actions/ModelChangeAction.h>
#include <browedit/advtex/AdvTex.h>
#include <browedit/util/Util.h>
#include <browedit/util/ResourceManager.h>
#include <browedit/util/FileIO.h>
#include <functional>
#include <browedit/gl/Texture.h>
#include <browedit/math/Ray.h>
#include <browedit/components/RsmRenderer.h>
#include <browedit/shaders/SimpleShader.h>
#include <browedit/gl/FBO.h>
#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>
#include <imgui_internal.h>
#include <glad/gl.h>
#include <stb/stb_image_write.h>
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <set>
#include <array>
#include <tuple>

namespace modelvariants
{
	bool visible = false;

	namespace
	{
		struct Params
		{
			float hue = 0.0f, sat = 1.0f, bright = 1.0f, contrast = 1.0f, tintAmount = 0.0f;
			float tint[3] = { 0.6f, 0.8f, 1.0f };
			bool operator==(const Params& o) const { return hue == o.hue && sat == o.sat && bright == o.bright && contrast == o.contrast && tintAmount == o.tintAmount && tint[0] == o.tint[0] && tint[1] == o.tint[1] && tint[2] == o.tint[2]; }
		};
		Params params, thumbParams;
		bool thumbsDirty = true;
		std::string srcModel;				// "data\model\..." in ISO
		std::string suffix = "_t";
		std::string texFolder = "variants\\";	// below data\texture
		std::string modelFolder;				// below data\model, empty = next to the original
		int swapMode = 1;						// 0 nothing, 1 selected, 2 all that use the model
		std::map<std::string, bool> enabled;
		std::string result;
		struct Thumb { gl::Texture* before = nullptr; gl::Texture* after = nullptr; };
		std::map<std::string, Thumb> thumbs;

		BrowEdit* gBe = nullptr;
		std::string lowerPk(std::string s) { for (auto& c : s) c = (char)tolower((unsigned char)c); return s; }

		// text field + "Pick" button that opens a thumbnail browser of data\texture (like the texture picker); value is relative to data\texture
		void texturePicker(const char* label, std::string& value)
		{
			ImGui::PushID(label);
			ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 150);
			ImGui::InputText(label, &value);
			ImGui::SameLine();
			if (ImGui::Button("Pick...")) ImGui::OpenPopup("Pick a texture");
			if (!value.empty())
			{
				if (auto t = util::ResourceManager<gl::Texture>::load("data\\texture\\" + value))
					if (t->loaded) { ImGui::SameLine(); ImGui::Image((ImTextureID)(intptr_t)t->id(), ImVec2(24, 24)); }
			}
			ImGui::SetNextWindowSize(ImVec2(820, 560), ImGuiCond_Appearing);
			if (ImGui::BeginPopupModal("Pick a texture", nullptr, ImGuiWindowFlags_NoSavedSettings))
			{
				static util::FileIO::Node* node = nullptr;
				static std::string filter;
				ImGui::InputText("Filter (tags or name)", &filter);
				ImGui::SameLine();
				if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
				std::function<void(util::FileIO::Node*)> tree;
				tree = [&](util::FileIO::Node* n)
				{
					for (auto f : n->directories)
					{
						int flags = ImGuiTreeNodeFlags_OpenOnDoubleClick;
						if (f.second->directories.size() == 0) flags |= ImGuiTreeNodeFlags_Bullet;
						if (f.second == node) flags |= ImGuiTreeNodeFlags_Selected;
						if (ImGui::TreeNodeEx(f.second->name.c_str(), flags)) { tree(f.second); ImGui::TreePop(); }
						if (ImGui::IsItemClicked()) node = f.second;
					}
				};
				ImGui::BeginChild("left", ImVec2(230, 0), true);
				if (auto root = util::FileIO::directoryNode("data\\texture\\"))
				{
					if (ImGui::TreeNodeEx("Textures", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_OpenOnDoubleClick)) { if (ImGui::IsItemClicked()) node = root; tree(root); ImGui::TreePop(); }
					else if (ImGui::IsItemClicked()) node = root;
				}
				ImGui::EndChild();
				ImGui::SameLine();
				ImGui::BeginChild("right", ImVec2(0, 0), true);
				auto isImage = [](const std::string& f) { std::string l = lowerPk(f); return l.find(".bmp") != std::string::npos || l.find(".tga") != std::string::npos || l.find(".png") != std::string::npos || l.find(".jpg") != std::string::npos; };
				float x2 = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
				bool chosen = false;
				auto box = [&](const std::string& fileIso, const std::string& pathIso)
				{
					ImVec2 th(100, 100);
					if (ImGui::BeginChild(pathIso.c_str(), ImVec2(th.x + 10, th.y + 50), true, ImGuiWindowFlags_NoScrollbar))
					{
						auto& cache = gBe->windowData.textureManageWindowCache;
						if (cache.find(pathIso) == cache.end()) cache[pathIso] = util::ResourceManager<gl::Texture>::load(pathIso);
						ImTextureID tex = cache[pathIso]->loaded ? (ImTextureID)(long long)cache[pathIso]->id() : 0;
						if (ImGui::ImageButtonEx(ImGui::GetID(pathIso.c_str()), tex, th, ImVec2(0, 0), ImVec2(1, 1), ImVec2(0, 0), ImVec4(0, 0, 0, 0), ImVec4(1, 1, 1, 1)))
						{
							value = pathIso.size() > 13 ? pathIso.substr(13) : pathIso;		// below data\texture
							chosen = true;
						}
						ImGui::Text("%s", util::iso_8859_1_to_utf8(fileIso).c_str());
					}
					ImGui::EndChild();
					if (ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x + th.x + 10 < x2) ImGui::SameLine();
				};
				if (filter.empty())
				{
					if (node)
						for (auto file : node->files)
						{
							if (!isImage(file)) continue;
							std::string fileIso = util::utf8_to_iso_8859_1(file), path = fileIso;
							auto n = node;
							while (n) { if (n->name != "") path = util::utf8_to_iso_8859_1(n->name) + "\\" + path; n = n->parent; }
							box(fileIso, path);
						}
				}
				else if (gBe)
				{
					std::string f = lowerPk(filter);
					int shown = 0;
					for (auto& kv : gBe->tagListReverse)
					{
						if (shown > 300) break;
						bool match = lowerPk(kv.first).find(f) != std::string::npos;
						for (auto& tg : kv.second) if (lowerPk(tg).find(f) != std::string::npos) match = true;
						if (match && isImage(kv.first)) { auto sl = kv.first.find_last_of("\\/"); box(sl == std::string::npos ? kv.first : kv.first.substr(sl + 1), kv.first); shown++; }
					}
				}
				ImGui::EndChild();
				if (chosen) ImGui::CloseCurrentPopup();
				ImGui::EndPopup();
			}
			ImGui::PopID();
		}

		bool isKey(const unsigned char* p) { return p[0] > 240 && p[1] < 16 && p[2] > 240; }		// the client's transparent magenta

		void rgb2hsv(float r, float g, float b, float& h, float& s, float& v)
		{
			float mx = std::max({ r, g, b }), mn = std::min({ r, g, b }), d = mx - mn;
			v = mx; s = mx > 0 ? d / mx : 0; h = 0;
			if (d > 0)
			{
				if (mx == r) h = std::fmod((g - b) / d, 6.0f);
				else if (mx == g) h = (b - r) / d + 2.0f;
				else h = (r - g) / d + 4.0f;
				h *= 60.0f; if (h < 0) h += 360.0f;
			}
		}
		void hsv2rgb(float h, float s, float v, float& r, float& g, float& b)
		{
			float c = v * s, x = c * (1 - std::fabs(std::fmod(h / 60.0f, 2.0f) - 1)), m = v - c;
			float rr = 0, gg = 0, bb = 0;
			int i = (int)(h / 60.0f) % 6;
			switch (i) { case 0: rr = c; gg = x; break; case 1: rr = x; gg = c; break; case 2: gg = c; bb = x; break; case 3: gg = x; bb = c; break; case 4: rr = x; bb = c; break; default: rr = c; bb = x; break; }
			r = rr + m; g = gg + m; b = bb + m;
		}
		void transform(const Params& p, const unsigned char* in, unsigned char* out)
		{
			float r = in[0] / 255.0f, g = in[1] / 255.0f, b = in[2] / 255.0f, h, s, v;
			rgb2hsv(r, g, b, h, s, v);
			h = std::fmod(h + p.hue + 720.0f, 360.0f);
			s = std::clamp(s * p.sat, 0.0f, 1.0f);
			v = std::clamp(v * p.bright, 0.0f, 1.0f);
			hsv2rgb(h, s, v, r, g, b);
			float c[3] = { r, g, b };
			for (int i = 0; i < 3; i++)
			{
				c[i] = (c[i] - 0.5f) * p.contrast + 0.5f;
				// the tint keeps the light/dark structure of the texture
				float lum = 0.3f * c[0] + 0.59f * c[1] + 0.11f * c[2];
				c[i] = c[i] * (1 - p.tintAmount) + (p.tint[i] * lum * 1.6f) * p.tintAmount;
				out[i] = (unsigned char)std::clamp((int)std::lround(c[i] * 255.0f), 0, 255);
			}
		}

		std::string lower(std::string s) { for (auto& c : s) c = (char)tolower((unsigned char)c); return s; }
		std::string stripName(const std::string& n, std::string& ext)
		{
			auto sl = n.find_last_of("\\/");
			std::string b = sl == std::string::npos ? n : n.substr(sl + 1);
			auto dot = b.rfind('.');
			ext = dot == std::string::npos ? "" : b.substr(dot);
			return dot == std::string::npos ? b : b.substr(0, dot);
		}
		std::string newTextureName(const std::string& oldName)
		{
			std::string ext; auto base = stripName(oldName, ext);
			std::string folder = texFolder;
			std::replace(folder.begin(), folder.end(), '/', '\\');
			if (!folder.empty() && folder.back() != '\\') folder += "\\";
			if (lower(ext) != ".bmp" && lower(ext) != ".tga" && lower(ext) != ".png" && lower(ext) != ".jpg") ext = ".bmp";
			return folder + base + suffix + ext;
		}

		void updateThumb(gl::Texture*& tex, const advtex::Img& im, const Params* p)
		{
			const int T = 64;
			std::vector<unsigned char> px((size_t)T * T * 4);
			for (int y = 0; y < T; y++)
				for (int x = 0; x < T; x++)
				{
					int sx = std::min(im.w - 1, x * im.w / T), sy = std::min(im.h - 1, y * im.h / T);
					const unsigned char* s = &im.px[((size_t)sy * im.w + sx) * 4];
					unsigned char* d = &px[((size_t)y * T + x) * 4];
					if (p && !isKey(s)) transform(*p, s, d); else { d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; }
					d[3] = 255;
				}
			if (!tex) tex = new gl::Texture(T, T);
			glBindTexture(GL_TEXTURE_2D, tex->id());
			glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, T, T, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
		}

		// ---------------------------------------------------------------- brush painting (on the texture, with the model's UV faces shown)
		struct PaintTex
		{
			int w = 0, h = 0;
			std::vector<unsigned char> orig, overlay, comp;		// RGBA; overlay alpha = paint coverage
			std::string name;
			std::vector<unsigned char> uvMask;						// 255 where a face of this model uses the pixel
			std::vector<float> lines;								// uv edges (u0,v0,u1,v1) for the wireframe
			gl::Texture* view = nullptr;
			std::vector<std::vector<unsigned char>> undo;
			bool dirtyView = true, dirtyModel = false;
			bool hasPaint = false;
		};
		std::map<std::string, PaintTex> paintTex;
		std::string paintCur;
		float brushSize = 24.0f, brushHard = 0.6f, brushOpacity = 1.0f, zoom = 1.0f;
		float brushColor[3] = { 0.8f, 0.2f, 0.1f };
		int brushMode = 0;				// 0 colour, 1 stamp texture
		bool onlyUv = true, showUv = true, livePreview = true, eraseMode = false;
		bool paint3d = false;
		std::string stampTex;
		std::string decalTex;
		float decalRot = 0.0f;
		bool decalRandom = true, decalFlip = false, decalBrightAlpha = false, decalTint = false;
		float stampTiling = 1.0f;
		ImVec2 pan(0, 0);
		bool painting = false;
		ImVec2 lastPaintPos(-1, -1);
		bool previewActive = false;
		std::set<std::string> previewTouched;

		bool needsBase(const PaintTex& pt, int i) { return !isKey(&pt.orig[i * 4]); }

		void compositeRange(PaintTex& pt, int x0, int y0, int x1, int y1, bool = true)
		{
			bool useParams = enabled.count(pt.name) ? enabled[pt.name] : true;
			x0 = std::max(0, x0); y0 = std::max(0, y0); x1 = std::min(pt.w - 1, x1); y1 = std::min(pt.h - 1, y1);
			for (int y = y0; y <= y1; y++)
				for (int x = x0; x <= x1; x++)
				{
					size_t i = (size_t)y * pt.w + x;
					unsigned char* d = &pt.comp[i * 4];
					const unsigned char* o = &pt.orig[i * 4];
					if (isKey(o)) { d[0] = o[0]; d[1] = o[1]; d[2] = o[2]; d[3] = o[3]; continue; }
					unsigned char base[3];
					if (useParams) transform(params, o, base); else { base[0] = o[0]; base[1] = o[1]; base[2] = o[2]; }
					const unsigned char* v = &pt.overlay[i * 4];
					float a = v[3] / 255.0f;
					for (int c = 0; c < 3; c++) d[c] = (unsigned char)std::lround(base[c] * (1 - a) + v[c] * a);
					d[3] = o[3];
				}
		}

		void rasterTri(PaintTex& pt, glm::vec2 a, glm::vec2 b, glm::vec2 c)
		{
			a *= glm::vec2((float)pt.w, (float)pt.h); b *= glm::vec2((float)pt.w, (float)pt.h); c *= glm::vec2((float)pt.w, (float)pt.h);
			int x0 = (int)std::floor(std::min({ a.x, b.x, c.x })) - 1, x1 = (int)std::ceil(std::max({ a.x, b.x, c.x })) + 1;
			int y0 = (int)std::floor(std::min({ a.y, b.y, c.y })) - 1, y1 = (int)std::ceil(std::max({ a.y, b.y, c.y })) + 1;
			x0 = std::max(0, x0); y0 = std::max(0, y0); x1 = std::min(pt.w - 1, x1); y1 = std::min(pt.h - 1, y1);
			float den = (b.y - c.y) * (a.x - c.x) + (c.x - b.x) * (a.y - c.y);
			if (std::fabs(den) < 1e-6f) return;
			for (int y = y0; y <= y1; y++)
				for (int x = x0; x <= x1; x++)
				{
					float px = x + 0.5f, py = y + 0.5f;
					float l1 = ((b.y - c.y) * (px - c.x) + (c.x - b.x) * (py - c.y)) / den;
					float l2 = ((c.y - a.y) * (px - c.x) + (a.x - c.x) * (py - c.y)) / den;
					float l3 = 1 - l1 - l2;
					const float e = -0.06f;		// a hair outside, so seams are covered
					if (l1 >= e && l2 >= e && l3 >= e) pt.uvMask[(size_t)y * pt.w + x] = 255;
				}
		}

		PaintTex* getPaint(Rsm* rsm, const std::string& texName, int texIndex)
		{
			auto it = paintTex.find(texName);
			if (it != paintTex.end()) return &it->second;
			auto im = advtex::getImg(texName);
			if (!im) return nullptr;
			PaintTex pt; pt.name = texName; pt.w = im->w; pt.h = im->h;
			pt.orig = im->px;
			pt.overlay.assign(pt.orig.size(), 0);
			pt.comp = pt.orig;
			pt.uvMask.assign((size_t)pt.w * pt.h, 0);
			rsm->rootMesh->foreach([&](Rsm::Mesh* m)
				{
					for (auto& f : m->faces)
					{
						if (f.texId < 0 || f.texId >= (int)m->textures.size() || m->textures[f.texId] != texIndex) continue;
						glm::vec2 uv[3];
						bool ok = true;
						for (int k = 0; k < 3; k++) { if (f.texCoordIds[k] >= m->texCoords.size()) { ok = false; break; } uv[k] = m->texCoords[f.texCoordIds[k]]; }
						if (!ok) continue;
						rasterTri(pt, uv[0], uv[1], uv[2]);
						for (int k = 0; k < 3; k++) { auto& q = uv[(k + 1) % 3]; pt.lines.insert(pt.lines.end(), { uv[k].x, uv[k].y, q.x, q.y }); }
					}
				});
			return &(paintTex[texName] = std::move(pt));
		}

		void uploadView(PaintTex& pt)
		{
			if (!pt.view) pt.view = new gl::Texture(pt.w, pt.h);
			glBindTexture(GL_TEXTURE_2D, pt.view->id());
			glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, pt.w, pt.h, GL_RGBA, GL_UNSIGNED_BYTE, pt.comp.data());
			pt.dirtyView = false;
		}

		void uploadToModelTexture(const std::string& name, const std::vector<unsigned char>& px, int w, int h)
		{
			auto t = util::ResourceManager<gl::Texture>::load("data\\texture\\" + name);
			if (!t || !t->id() || t->width != w || t->height != h) return;
			glBindTexture(GL_TEXTURE_2D, t->id());
			glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
			glGenerateMipmap(GL_TEXTURE_2D);
			previewTouched.insert(name);
		}

		// puts the real pictures back on the model in the map
		void restorePreview()
		{
			for (auto& kv : paintTex)
				if (previewTouched.count(kv.first)) uploadToModelTexture(kv.first, kv.second.orig, kv.second.w, kv.second.h);
			previewTouched.clear();
			previewActive = false;
		}

		void resetPaint()
		{
			restorePreview();
			for (auto& kv : paintTex) delete kv.second.view;
			paintTex.clear();
			paintCur.clear();
		}

		float stampSample(const advtex::Img& im, float u, float v, int c)
		{
			int x = ((int)std::floor(u * im.w) % im.w + im.w) % im.w, y = ((int)std::floor(v * im.h) % im.h + im.h) % im.h;
			return im.px[((size_t)y * im.w + x) * 4 + c];
		}

		// one copy of a picture (crack, sign, symbol...) centred on the point, brushSize pixels wide
		void stampDecal(PaintTex& pt, float cx, float cy)
		{
			auto im = decalTex.empty() ? nullptr : advtex::getImg(decalTex);
			if (!im) return;
			float rot = decalRot + (decalRandom ? (float)(rand() % 3600) / 10.0f : 0.0f);
			float cs = std::cos(glm::radians(rot)), sn = std::sin(glm::radians(rot));
			float size = std::max(4.0f, brushSize), half = size * 0.71f;
			int x0 = (int)std::floor(cx - half), x1 = (int)std::ceil(cx + half), y0 = (int)std::floor(cy - half), y1 = (int)std::ceil(cy + half);
			for (int y = std::max(0, y0); y <= std::min(pt.h - 1, y1); y++)
				for (int x = std::max(0, x0); x <= std::min(pt.w - 1, x1); x++)
				{
					size_t i = (size_t)y * pt.w + x;
					if (onlyUv && !pt.uvMask[i]) continue;
					if (isKey(&pt.orig[i * 4])) continue;
					float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
					float u = (dx * cs + dy * sn) / size + 0.5f, v = (-dx * sn + dy * cs) / size + 0.5f;
					if (u < 0 || u >= 1 || v < 0 || v >= 1) continue;
					if (decalFlip) u = 1.0f - u;
					float fx_ = u * im->w - 0.5f, fy_ = v * im->h - 0.5f;
					int ix = (int)std::floor(fx_), iy = (int)std::floor(fy_);
					float ax = fx_ - ix, ay = fy_ - iy;
					float c4[4];
					for (int c = 0; c < 4; c++)
					{
						auto at = [&](int xx, int yy) { xx = std::clamp(xx, 0, im->w - 1); yy = std::clamp(yy, 0, im->h - 1); return (float)im->px[((size_t)yy * im->w + xx) * 4 + c]; };
						c4[c] = (at(ix, iy) * (1 - ax) + at(ix + 1, iy) * ax) * (1 - ay) + (at(ix, iy + 1) * (1 - ax) + at(ix + 1, iy + 1) * ax) * ay;
					}
					unsigned char key[3] = { (unsigned char)c4[0], (unsigned char)c4[1], (unsigned char)c4[2] };
					float a = c4[3] / 255.0f;
					if (isKey(key)) a = 0.0f;
					else if (decalBrightAlpha) a *= std::clamp((0.3f * c4[0] + 0.59f * c4[1] + 0.11f * c4[2]) / 255.0f, 0.0f, 1.0f);
					a *= brushOpacity;
					if (a <= 0.003f) continue;
					float col[3];
					for (int c = 0; c < 3; c++) col[c] = decalTint ? c4[c] * brushColor[c] : c4[c];
					unsigned char* o = &pt.overlay[i * 4];
					float oa = o[3] / 255.0f, na = a + oa * (1 - a);
					for (int c = 0; c < 3; c++) o[c] = (unsigned char)std::clamp((int)std::lround((col[c] * a + o[c] * oa * (1 - a)) / na), 0, 255);
					o[3] = (unsigned char)std::lround(na * 255.0f);
				}
			pt.hasPaint = true;
			compositeRange(pt, x0, y0, x1, y1);
			pt.dirtyView = true; pt.dirtyModel = true;
		}

		void stamp(PaintTex& pt, float cx, float cy, bool erase)
		{
			if (brushMode == 2 && !erase) { stampDecal(pt, cx, cy); return; }
			const float r = std::max(1.0f, brushSize * 0.5f);
			int x0 = (int)std::floor(cx - r), x1 = (int)std::ceil(cx + r), y0 = (int)std::floor(cy - r), y1 = (int)std::ceil(cy + r);
			const advtex::Img* st = (brushMode == 1 && !stampTex.empty()) ? advtex::getImg(stampTex) : nullptr;
			for (int y = std::max(0, y0); y <= std::min(pt.h - 1, y1); y++)
				for (int x = std::max(0, x0); x <= std::min(pt.w - 1, x1); x++)
				{
					float d = std::sqrt((x + 0.5f - cx) * (x + 0.5f - cx) + (y + 0.5f - cy) * (y + 0.5f - cy)) / r;
					if (d >= 1.0f) continue;
					size_t i = (size_t)y * pt.w + x;
					if (onlyUv && !pt.uvMask[i]) continue;
					if (isKey(&pt.orig[i * 4])) continue;
					float a = (d <= brushHard ? 1.0f : 1.0f - (d - brushHard) / (1.0f - brushHard + 1e-4f)) * brushOpacity;
					a = std::clamp(a, 0.0f, 1.0f);
					unsigned char* v = &pt.overlay[i * 4];
					if (erase) { v[3] = (unsigned char)std::lround(v[3] * (1 - a)); continue; }
					float col[3];
					if (st) for (int c = 0; c < 3; c++) col[c] = stampSample(*st, (x + 0.5f) / pt.w * stampTiling * 4.0f, (y + 0.5f) / pt.h * stampTiling * 4.0f, c);
					else for (int c = 0; c < 3; c++) col[c] = brushColor[c] * 255.0f;
					float oa = v[3] / 255.0f, na = a + oa * (1 - a);
					if (na <= 0) continue;
					for (int c = 0; c < 3; c++) v[c] = (unsigned char)std::clamp((int)std::lround((col[c] * a + v[c] * oa * (1 - a)) / na), 0, 255);
					v[3] = (unsigned char)std::lround(na * 255.0f);
				}
			pt.hasPaint = true;
			compositeRange(pt, x0, y0, x1, y1, true);
			pt.dirtyView = true; pt.dirtyModel = true;
		}

		void drawPaintUi(Rsm* rsm)
		{
			ImGui::Checkbox("Paint in the 3D view (switch to Advanced Texture mode: left paints on the model, Ctrl or right erases)", &paint3d);
		ImGui::TextWrapped("Paint straight on a texture of the model. The UV lines show where the model's faces sit, so you can see what you are painting. With 'Preview on the model' on, the model in the map shows the paint live; nothing is saved until you press Generate variant.");
			if (ImGui::BeginCombo("Texture", paintCur.empty() ? "(choose)" : util::iso_8859_1_to_utf8(paintCur).c_str()))
			{
				for (auto& t : rsm->textures)
					if (ImGui::Selectable(util::iso_8859_1_to_utf8(t).c_str(), t == paintCur)) { paintCur = t; pan = ImVec2(0, 0); }
				ImGui::EndCombo();
			}
			int texIndex = -1;
			for (size_t i = 0; i < rsm->textures.size(); i++) if (rsm->textures[i] == paintCur) texIndex = (int)i;
			if (texIndex < 0) return;
			auto pt = getPaint(rsm, paintCur, texIndex);
			if (!pt) { ImGui::TextUnformatted("Cannot read this texture."); return; }

			ImGui::RadioButton("Colour", &brushMode, 0); ImGui::SameLine(); ImGui::RadioButton("Texture stamp", &brushMode, 1); ImGui::SameLine(); ImGui::RadioButton("Decal (one click)", &brushMode, 2);
			if (brushMode == 0) { ImGui::SameLine(); ImGui::ColorEdit3("##bc", brushColor, ImGuiColorEditFlags_NoInputs); }
			else if (brushMode == 1) { texturePicker("Stamp texture", stampTex); ImGui::SliderFloat("Stamp scale", &stampTiling, 0.25f, 8.0f, "%.2f"); }
			else
			{
				texturePicker("Decal picture", decalTex);
				ImGui::SliderFloat("Rotation", &decalRot, 0.0f, 360.0f, "%.0f deg");
				ImGui::Checkbox("Random rotation", &decalRandom); ImGui::SameLine(); ImGui::Checkbox("Flip", &decalFlip);
				ImGui::Checkbox("Dark parts transparent", &decalBrightAlpha); ImGui::SameLine(); ImGui::Checkbox("Tint with colour", &decalTint);
				if (decalTint) ImGui::ColorEdit3("##dt", brushColor, ImGuiColorEditFlags_NoInputs);
				ImGui::TextWrapped("The picture's own transparency is used (TGA / PNG), magenta counts as transparent. Size is the Size slider; one click places one copy.");
			}
			ImGui::SliderFloat("Size (px)", &brushSize, 2.0f, 200.0f, "%.0f");
			ImGui::SliderFloat("Hardness", &brushHard, 0.0f, 0.95f, "%.2f");
			ImGui::SliderFloat("Opacity", &brushOpacity, 0.05f, 1.0f, "%.2f");
			ImGui::Checkbox("Only on model faces", &onlyUv); ImGui::SameLine();
			ImGui::Checkbox("Show UV lines", &showUv); ImGui::SameLine();
			if (ImGui::Checkbox("Preview on the model", &livePreview) && !livePreview) restorePreview();
			if (ImGui::Button("Undo") && !pt->undo.empty()) { pt->overlay = pt->undo.back(); pt->undo.pop_back(); compositeRange(*pt, 0, 0, pt->w, pt->h, true); pt->dirtyView = pt->dirtyModel = true; }
			ImGui::SameLine();
			if (ImGui::Button("Clear paint")) { pt->undo.push_back(pt->overlay); std::fill(pt->overlay.begin(), pt->overlay.end(), 0); pt->hasPaint = false; compositeRange(*pt, 0, 0, pt->w, pt->h, true); pt->dirtyView = pt->dirtyModel = true; }
			ImGui::SameLine(); ImGui::TextUnformatted("Left paints, Ctrl or right erases, middle drag moves, wheel zooms");

			// canvas
			ImVec2 avail = ImGui::GetContentRegionAvail();
			float side = std::max(200.0f, std::min(avail.x, 520.0f));
			ImVec2 origin = ImGui::GetCursorScreenPos();
			ImGui::InvisibleButton("##canvas", ImVec2(side, side), ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
			bool hovered = ImGui::IsItemHovered();
			auto& io = ImGui::GetIO();
			float fit = side / (float)std::max(pt->w, pt->h);
			if (hovered && io.MouseWheel != 0) zoom = std::clamp(zoom * (io.MouseWheel > 0 ? 1.15f : 1.0f / 1.15f), 0.3f, 12.0f);
			if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) { pan.x += io.MouseDelta.x; pan.y += io.MouseDelta.y; }
			float sc = fit * zoom;
			ImVec2 img0(origin.x + pan.x, origin.y + pan.y), img1(img0.x + pt->w * sc, img0.y + pt->h * sc);
			if (pt->dirtyView || !pt->view) uploadView(*pt);
			auto dl = ImGui::GetWindowDrawList();
			dl->PushClipRect(origin, ImVec2(origin.x + side, origin.y + side), true);
			dl->AddRectFilled(origin, ImVec2(origin.x + side, origin.y + side), IM_COL32(40, 40, 40, 255));
			dl->AddImage((ImTextureID)(intptr_t)pt->view->id(), img0, img1);
			if (showUv)
				for (size_t i = 0; i + 3 < pt->lines.size(); i += 4)
					dl->AddLine(ImVec2(img0.x + pt->lines[i] * pt->w * sc, img0.y + pt->lines[i + 1] * pt->h * sc), ImVec2(img0.x + pt->lines[i + 2] * pt->w * sc, img0.y + pt->lines[i + 3] * pt->h * sc), IM_COL32(255, 255, 0, 110));
			if (hovered)
				dl->AddCircle(io.MousePos, std::max(2.0f, brushSize * 0.5f * sc), IM_COL32(255, 255, 255, 220), 32);
			dl->PopClipRect();

			bool lDown = ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Left), rDown = ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Right);
			if (lDown || rDown)
			{
				bool erase = rDown || io.KeyCtrl;
				float px = (io.MousePos.x - img0.x) / sc, py = (io.MousePos.y - img0.y) / sc;
				if (!painting) { painting = true; pt->undo.push_back(pt->overlay); if (pt->undo.size() > 15) pt->undo.erase(pt->undo.begin()); lastPaintPos = ImVec2(px, py); stamp(*pt, px, py, erase); }
				else
				{
					float dx = px - lastPaintPos.x, dy = py - lastPaintPos.y, dist = std::sqrt(dx * dx + dy * dy), step = std::max(1.0f, brushSize * 0.2f);
					int n = (int)(dist / step);
					for (int i = 1; i <= n && brushMode != 2; i++) stamp(*pt, lastPaintPos.x + dx / dist * step * i, lastPaintPos.y + dy / dist * step * i, erase);
					if (n > 0) lastPaintPos = ImVec2(lastPaintPos.x + dx / dist * step * n, lastPaintPos.y + dy / dist * step * n);
				}
			}
			else painting = false;
			if (pt->dirtyModel && livePreview) { uploadToModelTexture(paintCur, pt->comp, pt->w, pt->h); previewActive = true; pt->dirtyModel = false; }
		}

		bool writeTexture(BrowEdit* be, const std::string& oldName, const std::string& newName, std::string& err)
		{
			auto im = advtex::getImg(oldName);
			if (!im) { err = "Cannot read texture " + oldName; return false; }
			std::vector<unsigned char> px(im->px.size());
			bool alpha = false;
			auto pit = paintTex.find(oldName);
			if (pit != paintTex.end() && pit->second.comp.size() == px.size())
			{
				compositeRange(pit->second, 0, 0, pit->second.w, pit->second.h);
				px = pit->second.comp;
				for (size_t i = 3; i < px.size(); i += 4) if (px[i] < 255) { alpha = true; break; }
			}
			else
			for (size_t i = 0; i < px.size(); i += 4)
			{
				const unsigned char* s = &im->px[i];
				if (isKey(s)) { px[i] = s[0]; px[i + 1] = s[1]; px[i + 2] = s[2]; }
				else transform(params, s, &px[i]);
				px[i + 3] = s[3];
				if (s[3] < 255) alpha = true;
			}
			std::string path = be->config.ropath + "data\\texture\\" + newName;
			std::error_code ec;
			std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
			std::string ext = lower(newName.substr(newName.size() - 4));
			int ok = 0;
			if (ext == ".tga") ok = stbi_write_tga(path.c_str(), im->w, im->h, 4, px.data());
			else if (ext == ".png") ok = stbi_write_png(path.c_str(), im->w, im->h, 4, px.data(), im->w * 4);
			else if (ext == ".jpg") { std::vector<unsigned char> rgb((size_t)im->w * im->h * 3); for (size_t i = 0, j = 0; i < px.size(); i += 4, j += 3) { rgb[j] = px[i]; rgb[j + 1] = px[i + 1]; rgb[j + 2] = px[i + 2]; } ok = stbi_write_jpg(path.c_str(), im->w, im->h, 3, rgb.data(), 95); }
			else
			{
				std::vector<unsigned char> rgb((size_t)im->w * im->h * 3);
				for (size_t i = 0, j = 0; i < px.size(); i += 4, j += 3) { rgb[j] = px[i]; rgb[j + 1] = px[i + 1]; rgb[j + 2] = px[i + 2]; }
				ok = stbi_write_bmp(path.c_str(), im->w, im->h, 3, rgb.data());
				(void)alpha;
			}
			if (!ok) { err = "Cannot write " + path; return false; }
			return true;
		}
	}

	// ---------------------------------------------------------------- automatic effects (snow, side growth, bottom wear), from the faces' direction and height
	namespace
	{
		struct Effect
		{
			int type = 0;						// 0 snow on top, 1 growth on the sides, 2 wear / dirt at the bottom
			float colour[3] = { 0.95f, 0.97f, 1.0f };
			std::string stamp;
			float stampScale = 1.0f;
			float amount = 1.0f, threshold = 0.45f, softness = 0.25f, noise = 0.5f, noiseScale = 12.0f;
			int dir = 0;						// side growth: 0 any, 1 north, 2 east, 3 south, 4 west
			float height = 0.35f;				// bottom wear: how much of the model height
			float width = 1.5f;					// edge wear / crevice dirt: how far from the edge, world units
		} fx;
		std::string fxResult;

		float vn(float x, float y)
		{
			auto h = [](int xi, int yi) { unsigned int v = (unsigned int)(xi * 374761393 + yi * 668265263); v = (v ^ (v >> 13)) * 1274126177u; v ^= v >> 16; return (v & 0xFFFFFF) / (float)0xFFFFFF; };
			int xi = (int)std::floor(x), yi = (int)std::floor(y);
			float fx_ = x - xi, fy_ = y - yi;
			fx_ = fx_ * fx_ * (3 - 2 * fx_); fy_ = fy_ * fy_ * (3 - 2 * fy_);
			float a = h(xi, yi), b = h(xi + 1, yi), c = h(xi, yi + 1), d = h(xi + 1, yi + 1);
			return (a + (b - a) * fx_) + ((c + (d - c) * fx_) - (a + (b - a) * fx_)) * fy_;
		}
		float sstep(float a, float b, float x) { if (b <= a) return x >= b ? 1.0f : 0.0f; x = std::clamp((x - a) / (b - a), 0.0f, 1.0f); return x * x * (3 - 2 * x); }

		void applyEffect(MapView* mv, Rsm* rsm)
		{
			fxResult.clear();
			Node* inst = nullptr;
			for (auto n : mv->map->selectedNodes)
				if (auto m = n->getComponent<RswModel>()) if ("data\\model\\" + util::utf8_to_iso_8859_1(m->fileName) == srcModel) { inst = n; break; }
			if (!inst)
				mv->map->rootNode->traverse([&](Node* n) { if (inst) return; if (auto m = n->getComponent<RswModel>()) if ("data\\model\\" + util::utf8_to_iso_8859_1(m->fileName) == srcModel) inst = n; });
			auto rr = inst ? inst->getComponent<RsmRenderer>() : nullptr;
			if (!rr) { fxResult = "No instance of this model in the map to read its direction from."; return; }

			// sharp edges of the shape (for edge wear and crevice dirt): 1 = convex, 2 = concave
			std::vector<std::array<int, 3>> edgeFlag;
			if (fx.type >= 3)
			{
				struct FaceInfo { glm::vec3 w[3]; glm::vec3 n, c; };
				std::vector<FaceInfo> faces;
				rsm->rootMesh->foreach([&](Rsm::Mesh* m)
					{
						if (m->index >= rr->renderInfo.size()) return;
						glm::mat4 mat = rr->matrixCache * rr->renderInfo[m->index].matrix;
						for (auto& f : m->faces)
						{
							FaceInfo fi;
							for (int k = 0; k < 3; k++) fi.w[k] = glm::vec3(mat * glm::vec4(m->vertices[f.vertexIds[k]], 1));
							glm::vec3 g = glm::cross(fi.w[1] - fi.w[0], fi.w[2] - fi.w[0]);
							fi.n = glm::length(g) > 1e-9f ? glm::normalize(g) : glm::vec3(0, 1, 0);
							fi.c = (fi.w[0] + fi.w[1] + fi.w[2]) / 3.0f;
							faces.push_back(fi);
						}
					});
				auto q = [](const glm::vec3& p) { return std::make_tuple((long long)std::lround(p.x * 50), (long long)std::lround(p.y * 50), (long long)std::lround(p.z * 50)); };
				typedef std::tuple<long long, long long, long long> Q;
				std::map<std::pair<Q, Q>, std::vector<int>> edges;
				auto ekey = [&](const glm::vec3& a, const glm::vec3& b) { Q qa = q(a), qb = q(b); return qa < qb ? std::make_pair(qa, qb) : std::make_pair(qb, qa); };
				for (int i = 0; i < (int)faces.size(); i++)
					for (int k = 0; k < 3; k++) edges[ekey(faces[i].w[k], faces[i].w[(k + 1) % 3])].push_back(i);
				float cosT = std::cos(glm::radians(15.0f + fx.threshold * 70.0f));
				edgeFlag.assign(faces.size(), { 0, 0, 0 });
				for (int i = 0; i < (int)faces.size(); i++)
					for (int k = 0; k < 3; k++)
					{
						auto& lst = edges[ekey(faces[i].w[k], faces[i].w[(k + 1) % 3])];
						int best = 0;
						for (int j : lst)
						{
							if (j == i) continue;
							if (glm::dot(faces[i].n, faces[j].n) > cosT) continue;
							// the other face's middle is behind this face's plane on a convex edge
							int kind = glm::dot(faces[j].c - faces[i].c, faces[i].n) < 0 ? 1 : 2;
							best = best == 0 ? kind : best;
						}
						edgeFlag[i][k] = best;
					}
			}
			int faceCounter = -1;

			// world height range
			float ymin = 1e30f, ymax = -1e30f;
			rsm->rootMesh->foreach([&](Rsm::Mesh* m)
				{
					if (m->index >= rr->renderInfo.size()) return;
					glm::mat4 mat = rr->matrixCache * rr->renderInfo[m->index].matrix;
					for (auto& v : m->vertices) { float y = (mat * glm::vec4(v, 1)).y; ymin = std::min(ymin, y); ymax = std::max(ymax, y); }
				});
			if (ymax <= ymin) { fxResult = "The model has no size."; return; }

			glm::vec3 side(0);
			if (fx.dir == 1) side = glm::vec3(0, 0, -1); else if (fx.dir == 2) side = glm::vec3(1, 0, 0); else if (fx.dir == 3) side = glm::vec3(0, 0, 1); else if (fx.dir == 4) side = glm::vec3(-1, 0, 0);
			const advtex::Img* st = fx.stamp.empty() ? nullptr : advtex::getImg(fx.stamp);

			std::set<std::string> touchedTex;
			rsm->rootMesh->foreach([&](Rsm::Mesh* m)
				{
					if (m->index >= rr->renderInfo.size()) return;
					glm::mat4 mat = rr->matrixCache * rr->renderInfo[m->index].matrix;
					glm::mat3 nmat = glm::transpose(glm::inverse(glm::mat3(mat)));
					for (auto& f : m->faces)
					{
						faceCounter++;
						if (f.texId < 0 || f.texId >= (int)m->textures.size()) continue;
						int ti = m->textures[f.texId];
						if (ti < 0 || ti >= (int)rsm->textures.size()) continue;
						if (f.texCoordIds[0] >= m->texCoords.size() || f.texCoordIds[1] >= m->texCoords.size() || f.texCoordIds[2] >= m->texCoords.size()) continue;
						auto pt = getPaint(rsm, rsm->textures[ti], ti);
						if (!pt) continue;
						if (!touchedTex.count(rsm->textures[ti])) { pt->undo.push_back(pt->overlay); if (pt->undo.size() > 15) pt->undo.erase(pt->undo.begin()); touchedTex.insert(rsm->textures[ti]); }
						glm::vec3 wv[3], wn[3];
						glm::vec3 geo = glm::cross(glm::vec3(mat * glm::vec4(m->vertices[f.vertexIds[1]], 1)) - glm::vec3(mat * glm::vec4(m->vertices[f.vertexIds[0]], 1)), glm::vec3(mat * glm::vec4(m->vertices[f.vertexIds[2]], 1)) - glm::vec3(mat * glm::vec4(m->vertices[f.vertexIds[0]], 1)));
						glm::vec3 geoN = glm::length(geo) > 1e-9f ? glm::normalize(geo) : glm::vec3(0, 1, 0);
						for (int k = 0; k < 3; k++)
						{
							wv[k] = glm::vec3(mat * glm::vec4(m->vertices[f.vertexIds[k]], 1));
							glm::vec3 n = f.vertexNormals[k];
							if (glm::length(n) < 0.1f) n = f.normal;
							wn[k] = glm::length(n) > 0.1f ? glm::normalize(nmat * n) : geoN;
						}
						glm::vec2 uv[3];
						for (int k = 0; k < 3; k++) uv[k] = m->texCoords[f.texCoordIds[k]] * glm::vec2((float)pt->w, (float)pt->h);
						int x0 = std::max(0, (int)std::floor(std::min({ uv[0].x, uv[1].x, uv[2].x })) - 1), x1 = std::min(pt->w - 1, (int)std::ceil(std::max({ uv[0].x, uv[1].x, uv[2].x })) + 1);
						int y0 = std::max(0, (int)std::floor(std::min({ uv[0].y, uv[1].y, uv[2].y })) - 1), y1 = std::min(pt->h - 1, (int)std::ceil(std::max({ uv[0].y, uv[1].y, uv[2].y })) + 1);
						float den = (uv[1].y - uv[2].y) * (uv[0].x - uv[2].x) + (uv[2].x - uv[1].x) * (uv[0].y - uv[2].y);
						if (std::fabs(den) < 1e-6f) continue;
						for (int y = y0; y <= y1; y++)
							for (int x = x0; x <= x1; x++)
							{
								float px = x + 0.5f, py = y + 0.5f;
								float l1 = ((uv[1].y - uv[2].y) * (px - uv[2].x) + (uv[2].x - uv[1].x) * (py - uv[2].y)) / den;
								float l2 = ((uv[2].y - uv[0].y) * (px - uv[2].x) + (uv[0].x - uv[2].x) * (py - uv[2].y)) / den;
								float l3 = 1 - l1 - l2;
								if (l1 < -0.05f || l2 < -0.05f || l3 < -0.05f) continue;
								size_t i = (size_t)y * pt->w + x;
								if (isKey(&pt->orig[i * 4])) continue;
								glm::vec3 n = glm::normalize(wn[0] * l1 + wn[1] * l2 + wn[2] * l3 + glm::vec3(1e-6f));
								float wy = wv[0].y * l1 + wv[1].y * l2 + wv[2].y * l3;
								float w = 0;
								float nz = (vn(px / fx.noiseScale, py / fx.noiseScale) * 0.65f + vn(px / fx.noiseScale * 2.3f + 11.0f, py / fx.noiseScale * 2.3f + 5.0f) * 0.35f) - 0.5f;		// -0.5..0.5
								if (fx.type == 0)
									w = sstep(fx.threshold - fx.softness * 0.5f, fx.threshold + fx.softness * 0.5f, n.y + nz * fx.noise);
								else if (fx.type == 1)
								{
									float vertical = 1.0f - std::fabs(n.y);			// 1 on walls
									float facing = glm::length(side) < 0.5f ? 1.0f : std::clamp(glm::dot(glm::normalize(glm::vec3(n.x, 0, n.z) + glm::vec3(1e-6f)), side) * 0.5f + 0.5f, 0.0f, 1.0f);
									w = sstep(fx.threshold - fx.softness * 0.5f, fx.threshold + fx.softness * 0.5f, vertical * facing + nz * fx.noise);
								}
								else if (fx.type >= 3)
								{
									float lb[3] = { l1, l2, l3 };
									int want = fx.type == 3 ? 1 : 2;
									float area2 = glm::length(glm::cross(wv[1] - wv[0], wv[2] - wv[0]));
									for (int k = 0; k < 3; k++)
									{
										if (faceCounter < 0 || faceCounter >= (int)edgeFlag.size() || edgeFlag[faceCounter][k] != want) continue;
										float elen = glm::length(wv[(k + 1) % 3] - wv[k]);
										if (elen < 1e-6f) continue;
										float dist = lb[(k + 2) % 3] * (area2 / elen);
										float wd = fx.width;
										w = std::max(w, 1.0f - sstep(0.0f, wd, dist + nz * fx.noise * wd));
									}
								}
								else
								{
									float rel = (wy - ymin) / (ymax - ymin);
									w = 1.0f - sstep(fx.height * (1.0f - fx.softness), fx.height + 1e-4f, rel + nz * fx.noise * 0.5f);
								}
								float a = std::clamp(w * fx.amount, 0.0f, 1.0f);
								if (a <= 0.003f) continue;
								float col[3];
								if (st) for (int c = 0; c < 3; c++) col[c] = stampSample(*st, px / pt->w * fx.stampScale * 4.0f, py / pt->h * fx.stampScale * 4.0f, c);
								else for (int c = 0; c < 3; c++) col[c] = fx.colour[c] * 255.0f;
								unsigned char* v = &pt->overlay[i * 4];
								float oa = v[3] / 255.0f, na = a + oa * (1 - a);
								for (int c = 0; c < 3; c++) v[c] = (unsigned char)std::clamp((int)std::lround((col[c] * a + v[c] * oa * (1 - a)) / na), 0, 255);
								v[3] = (unsigned char)std::lround(na * 255.0f);
							}
					}
				});
			for (auto& n : touchedTex)
			{
				auto& pt = paintTex[n];
				pt.hasPaint = true;
				compositeRange(pt, 0, 0, pt.w, pt.h);
				pt.dirtyView = pt.dirtyModel = true;
			}
			fxResult = "Applied to " + std::to_string(touchedTex.size()) + " texture(s). Use Undo in the paint section to take it back.";
		}
	}

	// ---------------------------------------------------------------- painting in the 3D view (Advanced Texture mode)
	namespace
	{
		struct Hit { bool ok = false; float t = 1e30f; glm::vec3 world, normal; std::string tex; glm::vec2 px; float wpp = 1.0f; Rsm* rsm = nullptr; int texIndex = -1; };

		Hit pickModel(MapView* mv)
		{
			Hit best;
			mv->map->rootNode->traverse([&](Node* n)
				{
					auto m = n->getComponent<RswModel>();
					auto rsm = n->getComponent<Rsm>();
					auto rr = n->getComponent<RsmRenderer>();
					if (!m || !rsm || !rr || !rsm->loaded || !rsm->rootMesh) return;
					if ("data\\model\\" + util::utf8_to_iso_8859_1(m->fileName) != srcModel) return;
					if (!m->aabb.hasRayCollision(mv->mouseRay, 0, 10000000)) return;
					rsm->rootMesh->foreach([&](Rsm::Mesh* mesh)
						{
							if (mesh->index >= rr->renderInfo.size()) return;
							glm::mat4 mat = rr->matrixCache * rr->renderInfo[mesh->index].matrix;
							glm::mat4 inv = glm::inverse(mat);
							math::Ray r(mv->mouseRay * inv);
							for (auto& f : mesh->faces)
							{
								if (f.texId < 0 || f.texId >= (int)mesh->textures.size()) continue;
								glm::vec3 v0 = mesh->vertices[f.vertexIds[0]], v1 = mesh->vertices[f.vertexIds[1]], v2 = mesh->vertices[f.vertexIds[2]];
								glm::vec3 e1 = v1 - v0, e2 = v2 - v0, pv = glm::cross(r.dir, e2);
								float det = glm::dot(e1, pv);
								if (std::fabs(det) < 1e-9f) continue;
								float inv_det = 1.0f / det;
								glm::vec3 tv = r.origin - v0;
								float b1 = glm::dot(tv, pv) * inv_det;
								if (b1 < 0 || b1 > 1) continue;
								glm::vec3 qv = glm::cross(tv, e1);
								float b2 = glm::dot(r.dir, qv) * inv_det;
								if (b2 < 0 || b1 + b2 > 1) continue;
								float t = glm::dot(e2, qv) * inv_det;
								if (t <= 0) continue;
								glm::vec3 wp = glm::vec3(mat * glm::vec4(r.origin + t * r.dir, 1.0f));
								float wt = glm::length(wp - mv->mouseRay.origin);
								if (wt >= best.t) continue;
								if (f.texCoordIds[0] >= mesh->texCoords.size() || f.texCoordIds[1] >= mesh->texCoords.size() || f.texCoordIds[2] >= mesh->texCoords.size()) continue;
								glm::vec2 uv0 = mesh->texCoords[f.texCoordIds[0]], uv1 = mesh->texCoords[f.texCoordIds[1]], uv2 = mesh->texCoords[f.texCoordIds[2]];
								int ti = mesh->textures[f.texId];
								if (ti < 0 || ti >= (int)rsm->textures.size()) continue;
								glm::vec3 w0 = glm::vec3(mat * glm::vec4(v0, 1)), w1 = glm::vec3(mat * glm::vec4(v1, 1)), w2 = glm::vec3(mat * glm::vec4(v2, 1));
								best.ok = true; best.t = wt; best.world = wp; best.rsm = rsm; best.texIndex = ti; best.tex = rsm->textures[ti];
								glm::vec3 nrm = glm::cross(w1 - w0, w2 - w0);
								float worldArea = glm::length(nrm) * 0.5f;
								best.normal = glm::length(nrm) > 1e-9f ? glm::normalize(nrm) : glm::vec3(0, 1, 0);
								glm::vec2 uvp = (1 - b1 - b2) * uv0 + b1 * uv1 + b2 * uv2;
								best.px = uvp;		// converted to pixels by the caller
								float uvArea = std::fabs((uv1.x - uv0.x) * (uv2.y - uv0.y) - (uv2.x - uv0.x) * (uv1.y - uv0.y)) * 0.5f;
								best.wpp = uvArea > 1e-9f ? std::sqrt(worldArea / uvArea) : 0.0f;		// world units per uv unit; divided by the texture size below
							}
						});
				});
			return best;
		}

		void drawRing(MapView* mv, const glm::vec3& c, const glm::vec3& n, float radius)
		{
			glm::vec3 a = std::fabs(n.y) < 0.9f ? glm::normalize(glm::cross(n, glm::vec3(0, 1, 0))) : glm::normalize(glm::cross(n, glm::vec3(1, 0, 0)));
			glm::vec3 b = glm::cross(n, a);
			std::vector<float> v;
			for (int i = 0; i < 40; i++)
			{
				float an = i / 40.0f * 6.2831853f;
				glm::vec3 p = c + (a * std::cos(an) + b * std::sin(an)) * radius + n * 0.3f;
				v.insert(v.end(), { p.x, p.y, p.z, 0, 0 });
			}
			mv->simpleShader->use();
			mv->simpleShader->setUniform(SimpleShader::Uniforms::projectionMatrix, mv->nodeRenderContext.projectionMatrix);
			mv->simpleShader->setUniform(SimpleShader::Uniforms::viewMatrix, mv->nodeRenderContext.viewMatrix);
			mv->simpleShader->setUniform(SimpleShader::Uniforms::modelMatrix, glm::mat4(1.0f));
			mv->simpleShader->setUniform(SimpleShader::Uniforms::textureFac, 0.0f);
			mv->simpleShader->setUniform(SimpleShader::Uniforms::color, glm::vec4(0.2f, 0.9f, 1.0f, 1.0f));
			mv->fbo->bind();
			glBindBuffer(GL_ARRAY_BUFFER, 0);
			glDisable(GL_TEXTURE_2D);
			glDisable(GL_DEPTH_TEST);
			glEnableVertexAttribArray(0); glEnableVertexAttribArray(1);
			glDisableVertexAttribArray(2); glDisableVertexAttribArray(3); glDisableVertexAttribArray(4);
			glVertexAttribPointer(0, 3, GL_FLOAT, false, 5 * sizeof(float), &v[0]);
			glVertexAttribPointer(1, 2, GL_FLOAT, false, 5 * sizeof(float), &v[3]);
			glLineWidth(2);
			glDrawArrays(GL_LINE_LOOP, 0, (int)(v.size() / 5));
			glLineWidth(1);
			glEnable(GL_DEPTH_TEST);
			mv->fbo->unbind();
		}
	}

	// true while the brush owns the 3D view (Advanced Texture mode, "Paint in the 3D view" ticked)
	bool postRender(MapView* mv, BrowEdit* be)
	{
		if (!visible || !paint3d || srcModel.empty() || !mv->map) return false;
		auto& io = ImGui::GetIO();
		Hit h = mv->hovered ? pickModel(mv) : Hit();
		if (h.ok)
		{
			auto pt = getPaint(h.rsm, h.tex, h.texIndex);
			if (pt)
			{
				h.px = glm::vec2(h.px.x * pt->w, h.px.y * pt->h);
				float wpp = h.wpp / (float)std::max(pt->w, pt->h);
				drawRing(mv, h.world, h.normal, std::max(0.05f, brushSize * 0.5f * wpp));
				bool lDown = ImGui::IsMouseDown(ImGuiMouseButton_Left), rDown = ImGui::IsMouseDown(ImGuiMouseButton_Right);
				if ((lDown || rDown) && !io.WantCaptureMouse)
				{
					bool erase = rDown || io.KeyCtrl;
					const bool firstPress = !painting;
					if (!painting) { painting = true; pt->undo.push_back(pt->overlay); if (pt->undo.size() > 15) pt->undo.erase(pt->undo.begin()); }
					paintCur = h.tex;
					if (brushMode != 2 || erase || firstPress) stamp(*pt, h.px.x, h.px.y, erase);
				}
				else painting = false;
				if (pt->dirtyModel && livePreview) { uploadToModelTexture(h.tex, pt->comp, pt->w, pt->h); previewActive = true; pt->dirtyModel = false; }
			}
		}
		else painting = false;
		return true;
	}

	void showWindow(BrowEdit* be)
	{
		gBe = be;
		if (!visible) { if (previewActive) restorePreview(); return; }
		ImGui::SetNextWindowSize(ImVec2(520, 640), ImGuiCond_FirstUseEver);
		if (!ImGui::Begin("Model variants", &visible)) { ImGui::End(); return; }
		auto mv = be->activeMapView;
		if (!mv || !mv->map) { ImGui::TextUnformatted("Open a map first."); ImGui::End(); return; }
		Map* map = mv->map;

		// source: the first selected model, or typed in
		if (ImGui::Button("Use selected model"))
			for (auto n : map->selectedNodes)
				if (auto m = n->getComponent<RswModel>()) { resetPaint(); srcModel = "data\\model\\" + util::utf8_to_iso_8859_1(m->fileName); thumbsDirty = true; enabled.clear(); break; }
		ImGui::SameLine();
		ImGui::TextWrapped("%s", srcModel.empty() ? "Select a model in the map, then press the button" : util::iso_8859_1_to_utf8(srcModel).c_str());
		if (srcModel.empty()) { ImGui::End(); return; }

		auto rsm = util::ResourceManager<Rsm>::load(srcModel);
		if (!rsm || !rsm->loaded) { ImGui::TextUnformatted("Could not load that model."); ImGui::End(); return; }
		ImGui::Text("Model version %d.%d, %d textures", rsm->version >> 8, rsm->version & 0xFF, (int)rsm->textures.size());
		ImGui::Separator();

		bool changed = false;
		changed |= ImGui::SliderFloat("Hue shift", &params.hue, -180.0f, 180.0f, "%.0f deg");
		changed |= ImGui::SliderFloat("Saturation", &params.sat, 0.0f, 2.0f, "%.2f");
		changed |= ImGui::SliderFloat("Brightness", &params.bright, 0.2f, 2.0f, "%.2f");
		changed |= ImGui::SliderFloat("Contrast", &params.contrast, 0.5f, 1.8f, "%.2f");
		changed |= ImGui::SliderFloat("Tint amount", &params.tintAmount, 0.0f, 1.0f, "%.2f");
		changed |= ImGui::ColorEdit3("Tint colour", params.tint, ImGuiColorEditFlags_NoInputs);
		ImGui::TextUnformatted("Presets:");
		struct Preset { const char* n; Params p; };
		static const Preset presets[] = {
			{ "Reset", Params() },
			{ "Snow", { 0, 0.35f, 1.35f, 1.0f, 0.55f, { 0.85f, 0.95f, 1.1f } } },
			{ "Autumn", { -35, 1.1f, 1.0f, 1.05f, 0.25f, { 1.0f, 0.65f, 0.3f } } },
			{ "Night", { 10, 0.8f, 0.55f, 1.05f, 0.45f, { 0.35f, 0.45f, 0.9f } } },
			{ "Rusty", { -15, 0.9f, 0.9f, 1.1f, 0.4f, { 0.75f, 0.4f, 0.2f } } },
			{ "Ice", { 150, 0.7f, 1.15f, 1.0f, 0.4f, { 0.6f, 0.9f, 1.1f } } },
			{ "Dead", { 0, 0.3f, 0.75f, 1.1f, 0.3f, { 0.6f, 0.5f, 0.35f } } },
		};
		for (auto& pr : presets) { ImGui::SameLine(); if (ImGui::SmallButton(pr.n)) { params = pr.p; changed = true; } }
		if (changed)
		{
			thumbsDirty = true;
			if (livePreview)
				for (size_t i = 0; i < rsm->textures.size(); i++)
					if (auto pt = getPaint(rsm, rsm->textures[i], (int)i)) { compositeRange(*pt, 0, 0, pt->w, pt->h); pt->dirtyView = pt->dirtyModel = true; }
		}
		ImGui::NewLine();
		ImGui::Separator();

		if (thumbsDirty && !(thumbParams == params && false)) { thumbParams = params; }
		ImGui::Text("Textures (tick the ones to recolour)");
		for (size_t i = 0; i < rsm->textures.size(); i++)
		{
			const std::string& t = rsm->textures[i];
			if (!enabled.count(t)) enabled[t] = true;
			ImGui::PushID((int)i);
			ImGui::Checkbox("##on", &enabled[t]);
			ImGui::SameLine();
			auto im = advtex::getImg(t);
			auto& th = thumbs[t];
			if (im && (thumbsDirty || !th.after))
			{
				if (!th.before) updateThumb(th.before, *im, nullptr);
				updateThumb(th.after, *im, enabled[t] ? &params : nullptr);
			}
			if (th.before) { ImGui::Image((ImTextureID)(intptr_t)th.before->id(), ImVec2(48, 48)); ImGui::SameLine(); }
			if (th.after) { ImGui::Image((ImTextureID)(intptr_t)th.after->id(), ImVec2(48, 48)); ImGui::SameLine(); }
			ImGui::TextWrapped("%s -> %s", util::iso_8859_1_to_utf8(t).c_str(), util::iso_8859_1_to_utf8(newTextureName(t)).c_str());
			ImGui::PopID();
		}
		thumbsDirty = false;
		for (auto& kv : paintTex)
			if (kv.second.dirtyModel && livePreview) { uploadToModelTexture(kv.first, kv.second.comp, kv.second.w, kv.second.h); previewActive = true; kv.second.dirtyModel = false; }
		ImGui::Separator();
		if (ImGui::CollapsingHeader("Automatic effects (snow, growth, wear)"))
		{
			const char* names[] = { "Snow on top", "Growth on the sides (moss, vines)", "Wear / dirt at the bottom", "Edge wear (worn convex edges)", "Crevice dirt (inner corners)" };
			int before = fx.type;
			ImGui::Combo("Effect", &fx.type, names, 5);
			if (before != fx.type)
			{
				if (fx.type == 0) { fx.colour[0] = 0.95f; fx.colour[1] = 0.97f; fx.colour[2] = 1.0f; fx.threshold = 0.45f; }
				if (fx.type == 1) { fx.colour[0] = 0.25f; fx.colour[1] = 0.45f; fx.colour[2] = 0.15f; fx.threshold = 0.5f; }
				if (fx.type == 2) { fx.colour[0] = 0.25f; fx.colour[1] = 0.18f; fx.colour[2] = 0.12f; fx.height = 0.3f; }
				if (fx.type == 3) { fx.colour[0] = 0.82f; fx.colour[1] = 0.78f; fx.colour[2] = 0.68f; fx.threshold = 0.4f; fx.width = 1.2f; }
				if (fx.type == 4) { fx.colour[0] = 0.08f; fx.colour[1] = 0.06f; fx.colour[2] = 0.04f; fx.threshold = 0.4f; fx.width = 2.0f; }
			}
			ImGui::ColorEdit3("Colour", fx.colour, ImGuiColorEditFlags_NoInputs);
			texturePicker("or stamp texture", fx.stamp);
			ImGui::SliderFloat("Amount", &fx.amount, 0.05f, 1.0f, "%.2f");
			if (fx.type < 2) ImGui::SliderFloat("Threshold", &fx.threshold, 0.0f, 1.0f, "%.2f");
			else if (fx.type == 2) ImGui::SliderFloat("Height covered", &fx.height, 0.05f, 1.0f, "%.2f");
			else { ImGui::SliderFloat("Sharpness (how sharp an edge counts)", &fx.threshold, 0.0f, 1.0f, "%.2f"); ImGui::SliderFloat("Reach from the edge (world units)", &fx.width, 0.2f, 8.0f, "%.1f"); }
			ImGui::SliderFloat("Soft edge", &fx.softness, 0.02f, 1.0f, "%.2f");
			ImGui::SliderFloat("Noise", &fx.noise, 0.0f, 1.0f, "%.2f");
			ImGui::SliderFloat("Noise size (px)", &fx.noiseScale, 2.0f, 64.0f, "%.0f");
			if (fx.type == 1) ImGui::Combo("Side", &fx.dir, "Any\0North\0East\0South\0West\0");
			if (ImGui::Button("Apply effect")) applyEffect(mv, rsm);
			if (!fxResult.empty()) ImGui::TextWrapped("%s", fxResult.c_str());
		}
		if (ImGui::CollapsingHeader("Paint on the textures (brush)"))
			drawPaintUi(rsm);
		ImGui::Separator();

		ImGui::InputText("Name suffix", &suffix);
		ImGui::InputText("Texture folder (below data\\texture)", &texFolder);
		ImGui::InputText("Model folder (below data\\model, empty = same as the original)", &modelFolder);
		ImGui::TextUnformatted("In the map after generating:");
		ImGui::RadioButton("Leave the instances", &swapMode, 0); ImGui::SameLine();
		ImGui::RadioButton("Swap the selected ones", &swapMode, 1); ImGui::SameLine();
		ImGui::RadioButton("Swap all using this model", &swapMode, 2);

		if (ImGui::Button("Generate variant"))
		{
			result.clear();
			std::string err;
			std::map<std::string, std::string> names;
			bool ok = true;
			for (auto& t : rsm->textures)
				if (enabled[t] || (paintTex.count(t) && paintTex[t].hasPaint))
				{
					std::string nn = newTextureName(t);
					if (rsm->version < 0x0202 && nn.size() > 39) { result = "Texture name too long for model version 1.x (39 characters): " + nn + "\nUse a shorter texture folder or suffix."; ok = false; break; }
					if (!writeTexture(be, t, nn, err)) { result = err; ok = false; break; }
					names[t] = nn;
				}
			std::string newModel;
			if (ok)
			{
				std::string rel = srcModel.substr(11);		// below data\model
				std::string dir = ""; auto sl = rel.find_last_of("\\/");
				if (sl != std::string::npos) dir = rel.substr(0, sl + 1);
				std::string ext; auto base = stripName(rel, ext);
				std::string folder = modelFolder;
				std::replace(folder.begin(), folder.end(), '/', '\\');
				if (!folder.empty() && folder.back() != '\\') folder += "\\";
				newModel = "data\\model\\" + (modelFolder.empty() ? dir : folder) + base + suffix + ext;
				std::string path = be->config.ropath + newModel;
				std::error_code ec;
				std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
				ok = rsm->saveWithTextures(path, names, err);
				if (!ok) result = err;
			}
			if (ok)
			{
				restorePreview();
				result = "Saved " + util::iso_8859_1_to_utf8(newModel) + " and " + std::to_string(names.size()) + " texture(s).";
				if (swapMode > 0)
				{
					auto ga = new GroupAction();
					int count = 0;
					std::vector<Node*> targets;
					if (swapMode == 1) targets = map->selectedNodes;
					else map->rootNode->traverse([&](Node* n) { targets.push_back(n); });
					for (auto n : targets)
					{
						auto m = n->getComponent<RswModel>();
						if (!m || "data\\model\\" + util::utf8_to_iso_8859_1(m->fileName) != srcModel) continue;
						ga->addAction(new ModelChangeAction(n, newModel));
						count++;
					}
					if (count > 0) { map->doAction(ga, be); result += " Swapped " + std::to_string(count) + " instance(s)."; }
					else { delete ga; result += " No matching instances to swap."; }
				}
			}
		}
		if (!result.empty()) ImGui::TextWrapped("%s", result.c_str());
		ImGui::End();
	}
}
