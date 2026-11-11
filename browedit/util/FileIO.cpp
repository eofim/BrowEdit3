#include "Util.h"
#include "FileIO.h"

#include <iostream>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <algorithm>
#include <mutex>
#include <thread>
#include <chrono>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif


namespace util
{
	std::vector<FileIO::Source*> FileIO::sources;

	void FileIO::begin()
	{
		for (auto& source : sources)
		{
			source->close();
			delete source;
		}
		sources.clear();
	}

	std::istream* FileIO::open(const std::string& fileName)
	{
		for (auto& source : sources)
		{
			if (source->exists(fileName))
				return source->open(fileName);
		}
		return nullptr;
	}
	std::string FileIO::getSrc(const std::string& fileName)
	{
		for (auto& source : sources)
		{
			if (source->exists(fileName))
				return source->toString();
		}
		return "No Src: " + fileName;
	}

	bool FileIO::exists(const std::string& fileName)
	{
		for (auto& source : sources)
			if (source->exists(fileName))
				return true;
		return false;
	}

	std::vector<std::string> FileIO::listFiles(const std::string& directory)
	{
		std::vector<std::string> files;
		for (auto& source : sources)
			source->listFiles(directory, files);
		return files;
	}

	std::vector<std::string> FileIO::listAllFiles()
	{
		std::vector<std::string> files;
		for (auto& source : sources)
			source->listAllFiles(files);
		return files;
	}


	void FileIO::addDirectory(const std::string& directory)
	{
		sources.push_back(new DirSource(directory));
	}

	void FileIO::addGrf(const std::string& grf)
	{
		sources.push_back(new GrfSource(grf));
	}


	void FileIO::end()
	{
		if (rootNode)
			delete rootNode;
		std::cout << "FileIO: building tree" << std::endl;
		rootNode = new Node("", nullptr);
		std::vector<std::string> files = FileIO::listAllFiles();

		std::map<std::string, Node*> cache;
		for (const auto& file : files)
		{
			auto isDir = file.rfind("\\");
			if(isDir == std::string::npos)
				rootNode->addFile(file);
			else
			{
				std::string dir = file.substr(0, isDir);
				auto cacheHit = cache.find(dir);
				if (cacheHit == cache.end())
					cache[dir] = rootNode->addFile(file);
				else
					cacheHit->second->addFile(file.substr(isDir+1));
			}
		}
		std::cout << "FileIO: done building tree" << std::endl;
	}

	void FileIO::reload(const std::string& path)
	{
		auto files = listFiles(path);
		std::map<std::string, Node*> cache;
		for (const auto& file : files)
		{
			auto isDir = file.rfind("\\");
			if (isDir == std::string::npos)
				rootNode->addFile(file);
			else
			{
				std::string dir = file.substr(0, isDir);
				auto cacheHit = cache.find(dir);
				if (cacheHit == cache.end())
					cache[dir] = rootNode->addFile(file);
				else
					cacheHit->second->addFile(file.substr(isDir + 1));
			}
		}
	}


	// ------------------------------------------------------------------ live file watching
	namespace
	{
		struct WatchEvent { double t; bool removed; };
		std::mutex* watchMutex = new std::mutex();									// never freed: the watcher threads outlive main()
		std::map<std::string, WatchEvent>* watchPending = new std::map<std::string, WatchEvent>();
		bool watchOverflow = false;
		double nowSec() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

		std::string lowerStr(std::string s) { for (auto& c : s) c = (char)::tolower((unsigned char)c); return s; }

		void pruneNode(FileIO::Node* n, const std::string& kr)
		{
			for (auto it = n->files.begin(); it != n->files.end();)
			{
				if (!FileIO::exists(kr + utf8_to_iso_8859_1(*it))) it = n->files.erase(it); else ++it;
			}
			for (auto& d : n->directories) pruneNode(d.second, kr + d.first + "\\");
		}

#ifdef _WIN32
		void watchLoop(std::string dir)
		{
			if (!dir.empty() && dir.back() != '\\') dir += "\\";
			dir += "data\\";		// live reading only applies to the data folder
			HANDLE h = CreateFileA(dir.c_str(), FILE_LIST_DIRECTORY, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
			if (h == INVALID_HANDLE_VALUE) return;
			std::vector<char> buf(256 * 1024);
			while (true)
			{
				DWORD bytes = 0;
				if (!ReadDirectoryChangesW(h, buf.data(), (DWORD)buf.size(), TRUE, FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_SIZE, &bytes, nullptr, nullptr))
					break;
				std::lock_guard<std::mutex> lock(*watchMutex);
				if (bytes == 0) { watchOverflow = true; continue; }		// too many changes at once: rescan
				char* p = buf.data();
				for (;;)
				{
					auto* fni = (FILE_NOTIFY_INFORMATION*)p;
					int len = WideCharToMultiByte(CP_ACP, 0, fni->FileName, fni->FileNameLength / 2, nullptr, 0, nullptr, nullptr);
					std::string name(len, 0);
					if (len > 0) WideCharToMultiByte(CP_ACP, 0, fni->FileName, fni->FileNameLength / 2, &name[0], len, nullptr, nullptr);
					name = "data\\" + name;
					std::string l = lowerStr(name);
					bool ours = l.rfind("data\\", 0) == 0 && !(l.size() > 4 && l.substr(l.size() - 4) == ".tmp");
					if (ours)
						(*watchPending)[name] = WatchEvent{ nowSec(), fni->Action == FILE_ACTION_REMOVED || fni->Action == FILE_ACTION_RENAMED_OLD_NAME };
					if (!fni->NextEntryOffset) break;
					p += fni->NextEntryOffset;
				}
			}
			CloseHandle(h);
		}
#endif
	}

	void FileIO::startWatching()
	{
#ifdef _WIN32
		static bool started = false;
		if (started) return;
		started = true;
		std::set<std::string> seen;
		for (auto s : sources)
		{
			auto ds = dynamic_cast<DirSource*>(s);
			if (!ds) continue;
			std::error_code ec;
			std::string abs = std::filesystem::absolute(ds->dir(), ec).string();
			if (ec || !seen.insert(lowerStr(abs)).second) continue;
			std::thread([abs]() { watchLoop(abs); }).detach();
		}
#endif
	}

	std::vector<std::string> FileIO::pollChanges()
	{
		std::vector<std::string> changed;
		std::vector<std::pair<std::string, bool>> ready;
		bool overflow = false;
		{
			std::lock_guard<std::mutex> lock(*watchMutex);
			double t = nowSec();
			for (auto it = watchPending->begin(); it != watchPending->end();)
			{
				if (t - it->second.t > 0.35) { ready.emplace_back(it->first, it->second.removed); it = watchPending->erase(it); }	// let a copy finish first
				else ++it;
			}
			overflow = watchOverflow; watchOverflow = false;
		}
		if (!rootNode) return changed;
		if (overflow) reload("");
		for (auto& ev : ready)
		{
			const std::string& path = ev.first;
			std::string full;
			for (auto s : sources)
				if (auto ds = dynamic_cast<DirSource*>(s))
				{
					std::error_code ec;
					if (std::filesystem::exists(ds->dir() + path, ec)) { full = ds->dir() + path; break; }
				}
			if (full.empty())
			{
				if (FileIO::exists(path)) continue;		// still provided by a GRF or another folder
				// gone: take it out of the tree (nodes stay, so selected folders in the pickers remain valid)
				Node* n = rootNode;
				size_t start = 0;
				bool found = true;
				while (true)
				{
					size_t p = path.find('\\', start);
					std::string comp = path.substr(start, p == std::string::npos ? std::string::npos : p - start);
					if (p == std::string::npos)
					{
						n->files.erase(iso_8859_1_to_utf8(comp));
						auto d = n->directories.find(comp);
						if (d != n->directories.end()) pruneNode(d->second, path + "\\");
						break;
					}
					auto d = n->directories.find(comp);
					if (d == n->directories.end()) { found = false; break; }
					n = d->second; start = p + 1;
				}
				(void)found;
				continue;
			}
			std::error_code ec;
			if (std::filesystem::is_directory(full, ec))
			{
				Node* n = rootNode;
				size_t start = 0;
				while (start <= path.size())
				{
					size_t p = path.find('\\', start);
					std::string comp = path.substr(start, p == std::string::npos ? std::string::npos : p - start);
					if (!comp.empty() && n->directories.find(comp) == n->directories.end()) n->directories[comp] = new Node(iso_8859_1_to_utf8(comp), n);
					if (!comp.empty()) n = n->directories[comp];
					if (p == std::string::npos) break;
					start = p + 1;
				}
				reload(path);		// a whole folder was dropped in
			}
			else
			{
				rootNode->addFile(path);
				changed.push_back(path);
			}
		}
		return changed;
	}

	FileIO::Node* FileIO::Node::addFile(const std::string& fileName)
	{
		if (fileName.find("\\") == std::string::npos)
		{
			files.insert(iso_8859_1_to_utf8(fileName));
			return this;
		}
		else
		{
			std::string dir = fileName.substr(0, fileName.find("\\"));
			std::string rest = fileName.substr(fileName.find("\\")+1);
			auto dirNode = directories.find(dir);
			if (dirNode == directories.end())
			{
				directories[dir] = new Node(iso_8859_1_to_utf8(dir), this);
				dirNode = directories.find(dir);
			}
			return dirNode->second->addFile(rest);
		}
	}

	FileIO::Node* FileIO::Node::getDirectory(const std::string& directory)
	{
		if (directory == "")
			return this;
		std::string dir = directory;
		std::string rest = "";
		if (dir.find("\\") != std::string::npos)
		{
			dir = directory.substr(0, directory.find("\\"));
			rest = directory.substr(directory.find("\\") + 1);
		}
		if (directories.find(dir) == directories.end())
			return nullptr;
		return directories[dir]->getDirectory(rest);
	}

	FileIO::Node* FileIO::directoryNode(const std::string& directory)
	{
		return rootNode->getDirectory(directory);
	}

	////////GRF
	std::string FileIO::GrfSource::sanitizeFileName(std::string fileName)
	{
		std::transform(fileName.begin(), fileName.end(), fileName.begin(), ::tolower);
		std::replace(fileName.begin(), fileName.end(), '/', '\\');

		std::size_t index;
		while ((index = fileName.find("\\\\")) != std::string::npos)
			fileName = fileName.substr(0, index) + fileName.substr(index + 1);
		return fileName;
	}


	FileIO::GrfSource::GrfSource(const std::string& fileName) : grfFile(fileName)
	{
		grfFileName = fileName;
		if (grfFileName.find("/") != std::string::npos)
			grfFileName = grfFileName.substr(grfFileName.rfind("/") + 1);
		if (grfFileName.find("\\") != std::string::npos)
			grfFileName = grfFileName.substr(grfFileName.rfind("\\") + 1);
		std::cout << "GRF: Loading " << fileName << std::endl;
		GrfError error;
		grf = grf_open(fileName.c_str(), "rb", &error);
		if (grf == NULL)
		{
			std::cerr << "Error opening GRF file: " << grfFile << std::endl;
			return;
		}
		for (unsigned int i = 0; i < grf->nfiles; i++)
			lookup[sanitizeFileName(grf->files[i].name)] = i;
		std::cout << "GRF: " << lookup.size() << " files loaded" << std::endl;
	}

	void FileIO::GrfSource::close()
	{
		grf_close(grf);
		grf = nullptr;
	}

	std::istream* FileIO::GrfSource::open(const std::string& fileName)
	{
		if (!grf)
			throw "error";
		auto it = lookup.find(sanitizeFileName(fileName));
		if (it == lookup.end())
			throw "error";

		GrfError error;
		unsigned int size = 0;
		char* data = (char*)grf_index_get(grf, it->second, &size, &error);
		auto ss = new std::istringstream(std::string(data, size));
		//todo: check if data should really not be freed
		return ss;
	}

	bool FileIO::GrfSource::exists(const std::string& fileName)
	{
		return lookup.find(sanitizeFileName(fileName)) != lookup.end();
	}

	void FileIO::GrfSource::listFiles(const std::string& directory, std::vector<std::string>& files)
	{
		for (auto kv : lookup)
		{
			auto filename = kv.first;
			if (filename.substr(0, directory.size()) == directory)
			{
				filename = filename.substr(directory.size());
				if (filename[0] == '\\')
					filename = filename.substr(1);
				if (filename.find("\\") == std::string::npos)
					if (std::find(files.begin(), files.end(), kv.first) == files.end())
						files.push_back(kv.first);
			}
		}
	}

	void FileIO::GrfSource::listAllFiles(std::vector<std::string>& files)
	{
		for (auto kv : lookup)
			files.push_back(kv.first);
	}

	std::string FileIO::GrfSource::toString()
	{
		return "Grf: " + this->grfFileName;
	}

	///////////////////////File
	FileIO::DirSource::DirSource(const std::string& dir) : directory(dir)
	{
		std::cout << "Using directory " << dir << std::endl;
	}
	void FileIO::DirSource::close()
	{
	}

	std::istream* FileIO::DirSource::open(const std::string& fileName)
	{
		return new std::ifstream(directory + fileName, std::ios_base::in | std::ios_base::binary);
	}

	bool FileIO::DirSource::exists(const std::string& fileName)
	{
		if (fileName == "")
			return false;
		std::error_code ec;
		try
		{
			bool exists = std::filesystem::exists(directory + fileName, ec); //TODO
			if (ec)
			{
				std::cerr << "Exception when checking if file exists: " << fileName << std::endl;
				std::cerr << ec << std::endl;
				std::cerr << ec.message() << std::endl;
			}
			return exists;
		}
		catch (...)
		{
			std::cerr << "Error checking if file exists" << std::endl;
			return false;
		}
	}

	void FileIO::DirSource::listFiles(const std::string& dir, std::vector<std::string>& files)
	{
		try
		{
			if (!std::filesystem::exists(directory + dir))
				return;

			for (const auto& entry : std::filesystem::directory_iterator(directory + dir, std::filesystem::directory_options::follow_directory_symlink | std::filesystem::directory_options::skip_permission_denied))
			{
				auto f = entry.path().string();
				if (f.size() > directory.size() && directory.size() > 0 && f.find(directory) == 0)
					f = f.substr(directory.size());
				if (entry.is_directory() && f.find("game\\") != std::string::npos)
					continue;
				if (entry.is_directory())
					listFiles(f, files);
				else
					files.push_back(f);
			}
		}
		catch (const std::system_error& exception)
		{
			std::cerr << directory + dir << ": " << exception.what() << " (" << exception.code() << ")" << std::endl;
		}
	}

	void FileIO::DirSource::listAllFiles(std::vector<std::string>& files)
	{
		listFiles("", files);
	}

	std::string FileIO::DirSource::toString()
	{
		return "Dir: " + this->directory;
	}

	std::string FileIO::readString(std::istream* is, int maxLength, int length)
	{
		char* buf = new char[maxLength];
		is->read(buf, maxLength);
		std::string ret(buf);
		if (length > -1)
			ret = std::string(buf, length);
		delete[] buf;
		return ret;
	}
	std::string FileIO::readStringDyn(std::istream* is)
	{
		int len;
		is->read(reinterpret_cast<char*>(&len), sizeof(int));
		if (len < 0 || len > 1024)
		{
			std::cout << "Error offset " << is->tellg() << std::endl;
			throw std::exception("Error loading string");
		}
		char* buf = new char[len];
		is->read(buf, len);
		std::string ret(buf, len);
		delete[] buf;
		return ret;
	}

	void FileIO::writeString(std::ostream& os, const std::string& data, int length)
	{
		char* buf = new char[length];
		memset(buf, 0, length);
		memcpy(buf, data.c_str(), std::min(data.length(), (std::size_t)length-1));
		os.write(buf, length);
		delete[] buf;
	}

	nlohmann::json FileIO::getJson(const std::string& fileName)
	{
		nlohmann::json ret;
		auto file = open(fileName);
		if (file)
		{
			*file >> ret;
			delete file;
		}
		return ret;
	}
	std::string FileIO::getString(const std::string& fileName)
	{
		std::string ret = "";
		auto file = open(fileName);
		if (file)
		{
			file->seekg(0, std::ios_base::end);
			auto length = file->tellg();
			file->seekg(0, std::ios_base::beg);
			char* buf = new char[length];
			file->read(buf, length);
			ret = std::string(buf, length);
			delete[] buf;
			delete file;
		}
		return ret;
	}


}