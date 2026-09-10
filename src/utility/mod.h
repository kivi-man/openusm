#pragma once
#include <d3d9.h>

#include <cstdint>

#include <map>
#include <filesystem>
#include <vector>
#include <algorithm>
#include <cctype>
#include <fstream>

struct Mod {
    std::filesystem::path Path;
    int Type;       // 1 = tex, 2 = mesh, 3 = (custom) mesh, 4 = fdf
    std::vector<uint8_t> Data;
    bool IsLoaded = false;
};

struct modGenericMesh {
    Mod* mod;
    std::vector<float> vertices;
    std::vector<uint16_t> indices;
    IDirect3DVertexBuffer9* vertexBuffer = nullptr;
    IDirect3DIndexBuffer9* indexBuffer = nullptr;
    UINT stride = 16;
    UINT numVertices = 0;
    UINT numIndices = 0;
};


extern std::map<uint32_t, Mod> Mods;
extern Mod* dbgReplaceMesh;


[[maybe_unused]] static bool hasMod(uint32_t hash) {
    return Mods.find(hash) != Mods.end();
}

[[maybe_unused]] static Mod* getMod(uint32_t hash, int type = -1) {
    if (type == -1) {
        auto it = Mods.find(hash);
        if (it != Mods.end())
            return &it->second;
    }
    else {
        for (auto& [ihash, mod] : Mods)
            if (hash == ihash && mod.Type == type)
                return &mod;
    }
    return nullptr;
}

[[maybe_unused]] static uint8_t* getModDataByHash(uint32_t hash) {
    auto it = Mods.find(hash);
    if (it == Mods.end())
        return nullptr;

    Mod& mod = it->second;
    if (mod.Type != 1 && mod.Type != 2 && mod.Type != 3)
        return nullptr;

    if (!mod.IsLoaded) {
        std::ifstream file(mod.Path, std::ios::binary);
        if (file) {
            file.seekg(0, std::ios::end);
            std::streamsize sz = file.tellg();
            file.seekg(0, std::ios::beg);
            mod.Data.resize(sz);
            file.read(reinterpret_cast<char*>(mod.Data.data()), sz);
            mod.IsLoaded = true;
        }
    }

    if (!mod.Data.empty())
        return mod.Data.data();

    return nullptr;
}

[[maybe_unused]] static std::string transformToLower(const std::string& name)
{
    std::string res = name;
    std::transform(res.begin(), res.end(), res.begin(), [](unsigned char c) { return std::tolower(c); });
    return res;
}

// this is O(n) (don't use this unless necessary!)
[[maybe_unused]] static Mod* getModByFilemame(const std::string& name) {
    std::string search = transformToLower(name);
    for (auto& [hash, mod] : Mods) {
        std::string filename = transformToLower(mod.Path.filename().string());
        if (filename == search)
            return &mod;
    }
    return nullptr;
}

