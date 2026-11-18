// Overworld biome layers translated from BDS 1.6.0.15 (Linux).
//
// Every fillArea fills its parent's area into LayerData +0 (the parent swaps
// its result there), writes its own result into +8 and swaps. The values only
// depend on the coordinates and the seeds, so the buffers are plain vectors
// here (the original uses two 0x900-byte arrays).
#include "bds/world/biome.hpp"
#include <algorithm>
#include <cstring>
#include <map>

namespace bds {
namespace {
using namespace biome_id;
constexpr std::int64_t kMul = 0x5851f42d4c957f2dLL, kAdd = 0x14057b7ef767814fLL;
std::int64_t mix(std::int64_t s, std::int64_t salt) {
    // s * (s * 6364136223846793005 + 1442695040888963407) + salt, wrapping.
    const auto u = static_cast<std::uint64_t>(s);
    return static_cast<std::int64_t>(u * (u * static_cast<std::uint64_t>(kMul) + static_cast<std::uint64_t>(kAdd)) +
                                     static_cast<std::uint64_t>(salt));
}

// Layer::_isOcean 0x1d45850 (no deep warm ocean) and _isShallowOcean 0x1d45970.
bool is_ocean(int id) {
    return id == Ocean || id == OceanCold || id == OceanFrozen || id == OceanLukewarm || id == OceanWarm ||
           id == OceanDeep || id == OceanColdDeep || id == OceanFrozenDeep || id == OceanLukewarmDeep;
}
bool is_shallow_ocean(int id) {
    return id == Ocean || id == OceanCold || id == OceanFrozen || id == OceanLukewarm || id == OceanWarm;
}
// Layer::_isSame 0x1d45750.
bool is_same(int a, int b) {
    if (a == b)
        return true;
    if (a == MesaRock || a == MesaClearRock)
        return b == MesaRock || b == MesaClearRock;
    const auto *x = biome(a), *y = biome(b);
    return x && y && biome_same(*x, *y);
}
int type_of(int id) { return biome(id)->type; }

constexpr std::size_t kBuffer = 1 << 16;
} // namespace

const BiomeData *biome(int id) {
    static const auto index = [] {
        std::map<int, const BiomeData *> out;
        for (const auto &b : biome_registry())
            out[b.id] = &b;
        return out;
    }();
    const auto found = index.find(id);
    return found == index.end() ? nullptr : found->second;
}
bool biome_same(const BiomeData &a, const BiomeData &b) {
    if (a.kind == BiomeClass::Mutated)
        return biome_same(*biome(a.parent), b);
    return a.type == b.type;
}
int temperature_category(const BiomeData &b) {
    switch (b.kind) {
    case BiomeClass::Ocean:
    case BiomeClass::OceanCold:
    case BiomeClass::OceanWarm:
    case BiomeClass::OceanFrozen:
    case BiomeClass::OceanLukewarm: return 0;
    case BiomeClass::Mutated: return temperature_category(*biome(b.parent));
    default:
        if (0.2f > b.temperature)
            return 1;
        if (1.0f > b.temperature)
            return 2;
        return 3;
    }
}

LayerData::LayerData() : a(kBuffer), b(kBuffer), in(a.data()), out(b.data()) {}

Layer::Layer(std::uint32_t salt) {
    base_seed_ = static_cast<std::int64_t>(salt);
    for (int i = 0; i < 3; ++i)
        base_seed_ = mix(base_seed_, static_cast<std::int64_t>(salt));
}
void Layer::init(std::int64_t world_seed) {
    world_seed_ = world_seed;
    if (parent)
        parent->init(world_seed);
    for (int i = 0; i < 3; ++i)
        world_seed_ = mix(world_seed_, base_seed_);
}
void Layer::init_random(std::int64_t x, std::int64_t z) {
    chunk_seed_ = world_seed_;
    chunk_seed_ = mix(chunk_seed_, x);
    chunk_seed_ = mix(chunk_seed_, z);
    chunk_seed_ = mix(chunk_seed_, x);
    chunk_seed_ = mix(chunk_seed_, z);
}
int Layer::next_random(int n) {
    int r = static_cast<int>((chunk_seed_ >> 24) % static_cast<std::int64_t>(n));
    if (r < 0)
        r += n;
    chunk_seed_ = mix(chunk_seed_, world_seed_);
    return r;
}
int Layer::mode_or_random(int a, int b, int c, int d) {
    if (b == c && c == d)
        return b;
    if (a == b && a == c)
        return a;
    if (a == b && a == d)
        return a;
    if (a == c && a == d)
        return a;
    if (a == b && c != d)
        return a;
    if (a == c && b != d)
        return a;
    if (a == d && b != c)
        return a;
    if (b == c && a != d)
        return b;
    if (b == d && a != c)
        return b;
    if (c == d && a != b)
        return c;
    const int values[4] = {a, b, c, d};
    return random_of(values, 4);
}

namespace {
// IslandLayer::fillArea 0x1d44c20.
class IslandLayer : public Layer {
  public:
    const char *name() const override { return "IslandLayer"; }
    explicit IslandLayer(std::uint32_t salt) : Layer(salt) {}
    void fill(LayerData &data, int x, int z, int w, int h) override {
        for (int j = 0; j < h; ++j)
            for (int i = 0; i < w; ++i) {
                init_random(x + i, z + j);
                data.out[i + j * w] = next_random(10) == 0 ? 1 : 0;
            }
        if (x > -w && x <= 0 && z > -h && z <= 0)
            data.out[-x + -z * w] = 1;
        data.swap();
    }
};

// ZoomLayer::fillArea 0x1d48d30; FuzzyZoomLayer overrides _modeOrRandom
// (0x1d47e00: one of the four at random).
class ZoomLayer : public Layer {
  public:
    const char *name() const override { return fuzzy_ ? "FuzzyZoomLayer" : "ZoomLayer"; }
    ZoomLayer(std::uint32_t salt, std::shared_ptr<Layer> p, bool fuzzy = false) : Layer(salt), fuzzy_(fuzzy) {
        parent = std::move(p);
    }
    int mode_or_random(int a, int b, int c, int d) override {
        if (fuzzy_) {
            const int values[4] = {a, b, c, d};
            return random_of(values, 4);
        }
        return Layer::mode_or_random(a, b, c, d);
    }
    void fill(LayerData &data, int x, int z, int w, int h) override {
        const int px = x >> 1, pz = z >> 1, pw = (w >> 1) + 2, ph = (h >> 1) + 2;
        parent->fill(data, px, pz, pw, ph);
        const int tw = (pw - 1) << 1;
        std::vector<int> tmp(static_cast<std::size_t>(tw * ((ph - 1) << 1)));
        for (int j = 0; j < ph - 1; ++j) {
            int index = (j << 1) * tw;
            int a = data.in[0 + j * pw], b = data.in[0 + (j + 1) * pw];
            for (int i = 0; i < pw - 1; ++i) {
                init_random(static_cast<std::int64_t>((i + px) << 1), static_cast<std::int64_t>((j + pz) << 1));
                const int c = data.in[i + 1 + j * pw], d = data.in[i + 1 + (j + 1) * pw];
                tmp[static_cast<std::size_t>(index)] = a;
                const int ab[2] = {a, b};
                tmp[static_cast<std::size_t>(index + tw)] = random_of(ab, 2);
                ++index;
                const int ac[2] = {a, c};
                tmp[static_cast<std::size_t>(index)] = random_of(ac, 2);
                tmp[static_cast<std::size_t>(index + tw)] = mode_or_random(a, c, b, d);
                ++index;
                a = c;
                b = d;
            }
        }
        for (int j = 0; j < h; ++j)
            std::memcpy(data.out + j * w, tmp.data() + (j + (z & 1)) * tw + (x & 1),
                        sizeof(int) * static_cast<std::size_t>(w));
        data.swap();
    }

  private:
    bool fuzzy_;
};
// ZoomLayer::zoom 0x1d491e0: `count` ZoomLayers, each made with salt + 1
// (the same salt for all of them; confirmed by the layers' base seeds read
// from the original, tools/re/gdb_layer_oracle.py).
std::shared_ptr<Layer> zoom(std::uint32_t salt, std::shared_ptr<Layer> layer, int count) {
    for (int i = 0; i < count; ++i)
        layer = std::make_shared<ZoomLayer>(salt + 1, layer);
    return layer;
}

// AddIslandLayer::fillArea 0x1d41750.
class AddIslandLayer : public Layer {
  public:
    const char *name() const override { return "AddIslandLayer"; }
    AddIslandLayer(std::uint32_t salt, std::shared_ptr<Layer> p) : Layer(salt) { parent = std::move(p); }
    void fill(LayerData &data, int x, int z, int w, int h) override {
        parent->fill(data, x - 1, z - 1, w + 2, h + 2);
        const int pw = w + 2;
        for (int j = 0; j < h; ++j)
            for (int i = 0; i < w; ++i) {
                const int nw = data.in[i + 0 + (j + 0) * pw], ne = data.in[i + 2 + (j + 0) * pw];
                const int sw = data.in[i + 0 + (j + 2) * pw], se = data.in[i + 2 + (j + 2) * pw];
                const int c = data.in[i + 1 + (j + 1) * pw];
                init_random(x + i, z + j);
                int out = c;
                if (is_shallow_ocean(c) && (!is_shallow_ocean(nw) || !is_shallow_ocean(ne) ||
                                            !is_shallow_ocean(sw) || !is_shallow_ocean(se))) {
                    int n = 1, pick = 1;
                    if (!is_shallow_ocean(nw) && next_random(n++) == 0)
                        pick = nw;
                    if (!is_shallow_ocean(ne) && next_random(n++) == 0)
                        pick = ne;
                    if (!is_shallow_ocean(sw) && next_random(n++) == 0)
                        pick = sw;
                    if (!is_shallow_ocean(se) && next_random(n++) == 0)
                        pick = se;
                    if (next_random(3) == 0)
                        out = pick;
                    else
                        out = pick == 4 ? 4 : c;
                } else if (!is_shallow_ocean(c) && (is_shallow_ocean(nw) || is_shallow_ocean(ne) ||
                                                    is_shallow_ocean(sw) || is_shallow_ocean(se))) {
                    if (next_random(5) == 0) {
                        if (is_shallow_ocean(nw))
                            out = c == 4 ? 4 : nw;
                        else if (is_shallow_ocean(ne))
                            out = c == 4 ? 4 : ne;
                        else if (is_shallow_ocean(sw))
                            out = c == 4 ? 4 : sw;
                        else if (is_shallow_ocean(se))
                            out = c == 4 ? 4 : se;
                    }
                }
                data.out[i + j * w] = out;
            }
        data.swap();
    }
};

// A layer reading the four cross neighbours (x + 1, z), (x + 2, z + 1),
// (x, z + 1), (x + 1, z + 2) around the centre (x + 1, z + 1).
struct Cross {
    int n, e, w, s, c;
};
template <class F> void each_cross(LayerData &data, int w, int h, F f) {
    const int pw = w + 2;
    for (int j = 0; j < h; ++j)
        for (int i = 0; i < w; ++i) {
            const Cross c{data.in[i + 1 + (j + 0) * pw], data.in[i + 2 + (j + 1) * pw], data.in[i + 0 + (j + 1) * pw],
                          data.in[i + 1 + (j + 2) * pw], data.in[i + 1 + (j + 1) * pw]};
            data.out[i + j * w] = f(i, j, c);
        }
    data.swap();
}

// RemoveTooMuchOceanLayer::fillArea 0x1d43d80.
class RemoveTooMuchOceanLayer : public Layer {
  public:
    const char *name() const override { return "RemoveTooMuchOceanLayer"; }
    RemoveTooMuchOceanLayer(std::uint32_t salt, std::shared_ptr<Layer> p) : Layer(salt) { parent = std::move(p); }
    void fill(LayerData &data, int x, int z, int w, int h) override {
        parent->fill(data, x - 1, z - 1, w + 2, h + 2);
        each_cross(data, w, h, [&](int i, int j, const Cross &q) {
            init_random(x + i, z + j);
            if (is_shallow_ocean(q.c) && is_shallow_ocean(q.n) && is_shallow_ocean(q.e) && is_shallow_ocean(q.w) &&
                is_shallow_ocean(q.s) && next_random(2) == 0)
                return 1;
            return q.c;
        });
    }
};

// AddOceanTemperatureLayer::fillArea 0x1d44e70: the parent is filled (with a
// border) but not read; nextRandom(100) / 100 picks the ocean temperature.
class AddOceanTemperatureLayer : public Layer {
  public:
    const char *name() const override { return "AddOceanTemperatureLayer"; }
    AddOceanTemperatureLayer(std::uint32_t salt, std::shared_ptr<Layer> p) : Layer(salt) { parent = std::move(p); }
    void fill(LayerData &data, int x, int z, int w, int h) override {
        parent->fill(data, x - 1, z - 1, w + 2, h + 2);
        for (int j = 0; j < h; ++j)
            for (int i = 0; i < w; ++i) {
                init_random(x + i, z + j);
                const float t = static_cast<float>(next_random(100)) / 100.0f;
                int out = OceanFrozen;
                if (0.075f > t)
                    out = OceanWarm;
                else if (0.4f > t)
                    out = OceanLukewarm;
                else if (0.675f > t)
                    out = Ocean;
                else if (0.95f > t)
                    out = OceanCold;
                data.out[i + j * w] = out;
            }
        data.swap();
    }
};

// AddOceanEdgeLayer::fillArea 0x1d45ea0: warm next to frozen (or frozen next
// to warm) becomes the normal ocean. initRandom is called but nothing drawn.
class AddOceanEdgeLayer : public Layer {
  public:
    const char *name() const override { return "AddOceanEdgeLayer"; }
    AddOceanEdgeLayer(std::uint32_t salt, std::shared_ptr<Layer> p) : Layer(salt) { parent = std::move(p); }
    void fill(LayerData &data, int x, int z, int w, int h) override {
        parent->fill(data, x - 1, z - 1, w + 2, h + 2);
        each_cross(data, w, h, [&](int i, int j, const Cross &q) {
            init_random(x + i, z + j);
            if (q.c == OceanWarm &&
                (q.n == OceanFrozen || q.e == OceanFrozen || q.w == OceanFrozen || q.s == OceanFrozen))
                return Ocean;
            if (q.c == OceanFrozen && (q.n == OceanWarm || q.e == OceanWarm || q.w == OceanWarm || q.s == OceanWarm))
                return Ocean;
            return q.c;
        });
    }
};

// AddSnowLayer::fillArea 0x1d442b0.
class AddSnowLayer : public Layer {
  public:
    const char *name() const override { return "AddSnowLayer"; }
    AddSnowLayer(std::uint32_t salt, std::shared_ptr<Layer> p) : Layer(salt) { parent = std::move(p); }
    void fill(LayerData &data, int x, int z, int w, int h) override {
        parent->fill(data, x - 1, z - 1, w + 2, h + 2);
        each_cross(data, w, h, [&](int i, int j, const Cross &q) {
            init_random(x + i, z + j);
            if (is_shallow_ocean(q.c))
                return q.c;
            const int r = next_random(6);
            return r == 0 ? 4 : r <= 1 ? 3 : 1;
        });
    }
};

// AddEdgeLayer::fillArea 0x1d42d20: mode 0 fillCoolWarm 0x1d42df0, 1
// fillHeatIce 0x1d43150, 2 fillIntroduceSpecial 0x1d43490.
class AddEdgeLayer : public Layer {
  public:
    const char *name() const override { return "AddEdgeLayer"; }
    AddEdgeLayer(std::uint32_t salt, std::shared_ptr<Layer> p, int mode) : Layer(salt), mode_(mode) {
        parent = std::move(p);
    }
    void fill(LayerData &data, int x, int z, int w, int h) override {
        if (mode_ == 2) {
            parent->fill(data, x, z, w, h);
            for (int j = 0; j < h; ++j)
                for (int i = 0; i < w; ++i) {
                    init_random(x + i, z + j);
                    int v = data.in[i + j * w];
                    if (!is_shallow_ocean(v) && next_random(13) == 0)
                        v |= ((next_random(15) + 1) << 8) & 0xf00;
                    data.out[i + j * w] = v;
                }
            data.swap();
            return;
        }
        parent->fill(data, x - 1, z - 1, w + 2, h + 2);
        each_cross(data, w, h, [&](int i, int j, const Cross &q) {
            init_random(x + i, z + j);
            if (mode_ == 0) {
                if (q.c == 1 && (q.n == 3 || q.e == 3 || q.w == 3 || q.s == 3 || q.n == 4 || q.e == 4 || q.w == 4 ||
                                 q.s == 4))
                    return 2;
                return q.c;
            }
            if (q.c == 4 && (q.n == 2 || q.e == 2 || q.w == 2 || q.s == 2 || q.n == 1 || q.e == 1 || q.w == 1 ||
                             q.s == 1))
                return 3;
            return q.c;
        });
    }

  private:
    int mode_;
};

// AddMushroomIslandLayer::fillArea 0x1d4eb50 (diagonal neighbours).
class AddMushroomIslandLayer : public Layer {
  public:
    const char *name() const override { return "AddMushroomIslandLayer"; }
    AddMushroomIslandLayer(std::uint32_t salt, std::shared_ptr<Layer> p) : Layer(salt) { parent = std::move(p); }
    void fill(LayerData &data, int x, int z, int w, int h) override {
        parent->fill(data, x - 1, z - 1, w + 2, h + 2);
        const int pw = w + 2;
        for (int j = 0; j < h; ++j)
            for (int i = 0; i < w; ++i) {
                const int nw = data.in[i + j * pw], ne = data.in[i + 2 + j * pw], sw = data.in[i + (j + 2) * pw],
                          se = data.in[i + 2 + (j + 2) * pw], c = data.in[i + 1 + (j + 1) * pw];
                init_random(x + i, z + j);
                int out = c;
                if (is_shallow_ocean(c) && is_shallow_ocean(nw) && is_shallow_ocean(ne) && is_shallow_ocean(sw) &&
                    is_shallow_ocean(se) && next_random(100) == 0)
                    out = MushroomIsland;
                data.out[i + j * w] = out;
            }
        data.swap();
    }
};

// AddDeepOceanLayer::fillArea 0x1d4a170 (no initRandom).
class AddDeepOceanLayer : public Layer {
  public:
    const char *name() const override { return "AddDeepOceanLayer"; }
    AddDeepOceanLayer(std::uint32_t salt, std::shared_ptr<Layer> p) : Layer(salt) { parent = std::move(p); }
    void fill(LayerData &data, int x, int z, int w, int h) override {
        parent->fill(data, x - 1, z - 1, w + 2, h + 2);
        each_cross(data, w, h, [&](int, int, const Cross &q) {
            int count = 0;
            for (int v : {q.n, q.e, q.w, q.s})
                if (is_shallow_ocean(v))
                    ++count;
            if (is_shallow_ocean(q.c) && count > 3) {
                if (q.c == OceanWarm)
                    return OceanWarmDeep;
                if (q.c == OceanLukewarm)
                    return OceanLukewarmDeep;
                if (q.c == Ocean)
                    return OceanDeep;
                if (q.c == OceanFrozen)
                    return OceanFrozenDeep;
                if (q.c == OceanCold)
                    return OceanColdDeep;
            }
            return q.c;
        });
    }
};

// RiverInitLayer::fillArea 0x1d439c0.
class RiverInitLayer : public Layer {
  public:
    const char *name() const override { return "RiverInitLayer"; }
    RiverInitLayer(std::uint32_t salt, std::shared_ptr<Layer> p) : Layer(salt) { parent = std::move(p); }
    void fill(LayerData &data, int x, int z, int w, int h) override {
        parent->fill(data, x, z, w, h);
        for (int j = 0; j < h; ++j)
            for (int i = 0; i < w; ++i) {
                init_random(x + i, z + j);
                const int v = data.in[i + j * w];
                data.out[i + j * w] = is_shallow_ocean(v) ? v : next_random(299999) + 2;
            }
        data.swap();
    }
};

// BiomeInitLayer 0x1d46430 / fillArea 0x1d46610.
class BiomeInitLayer : public Layer {
  public:
    const char *name() const override { return "BiomeInitLayer"; }
    BiomeInitLayer(std::uint32_t salt, std::shared_ptr<Layer> p, int generator) : Layer(salt) {
        parent = std::move(p);
        medium_ = {Forest};
        if (generator == 1)
            medium_.push_back(RoofedForest);
        for (int b : {ExtremeHills, Plains, Plains, Plains, BirchForest, Swampland})
            medium_.push_back(b);
    }
    void fill(LayerData &data, int x, int z, int w, int h) override {
        parent->fill(data, x, z, w, h);
        static constexpr int warm[6] = {Desert, Desert, Desert, Savanna, Savanna, Plains};
        static constexpr int cold[4] = {Forest, ExtremeHills, Taiga, Plains};
        static constexpr int icy[4] = {IceFlats, IceFlats, IceFlats, TaigaCold};
        for (int j = 0; j < h; ++j)
            for (int i = 0; i < w; ++i) {
                init_random(x + i, z + j);
                int v = data.in[i + j * w];
                const int special = (v & 0xf00) >> 8;
                v &= ~0xf00;
                int out;
                if (is_ocean(v) || v == MushroomIsland)
                    out = v;
                else if (v == 1)
                    out = special > 0 ? (next_random(3) == 0 ? MesaClearRock : MesaRock) : warm[next_random(6)];
                else if (v == 2)
                    out = special > 0 ? Jungle : medium_[static_cast<std::size_t>(
                                                     next_random(static_cast<int>(medium_.size())))];
                else if (v == 3)
                    out = special > 0 ? RedwoodTaiga : cold[next_random(4)];
                else if (v == 4)
                    out = icy[next_random(4)];
                else
                    out = MushroomIsland;
                data.out[i + j * w] = out;
            }
        data.swap();
    }

  private:
    std::vector<int> medium_; // +0x70
};

// BiomeEdgeLayer::fillArea 0x1d4c010 with checkEdge 0x1d4c850,
// checkEdgeStrict 0x1d4ca10 and isValidTemperatureEdge 0x1d4cbb0.
class BiomeEdgeLayer : public Layer {
  public:
    const char *name() const override { return "BiomeEdgeLayer"; }
    BiomeEdgeLayer(std::uint32_t salt, std::shared_ptr<Layer> p) : Layer(salt) { parent = std::move(p); }
    void fill(LayerData &data, int x, int z, int w, int h) override {
        parent->fill(data, x - 1, z - 1, w + 2, h + 2);
        each_cross(data, w, h, [&](int i, int j, const Cross &q) {
            init_random(x + i, z + j);
            int out;
            if (check_edge(q, ExtremeHills, SmallerExtremeHills, out) || check_strict(q, MesaRock, Mesa, out) ||
                check_strict(q, MesaClearRock, Mesa, out) || check_strict(q, RedwoodTaiga, Taiga, out))
                return out;
            if (q.c == Desert) {
                if (q.n == IceFlats || q.e == IceFlats || q.w == IceFlats || q.s == IceFlats)
                    return ExtremeHillsWithTrees;
                return q.c;
            }
            if (q.c == Swampland) {
                for (int v : {q.n, q.e, q.w, q.s})
                    if (v == Desert || v == TaigaCold || v == IceFlats)
                        return Plains;
                for (int v : {q.n, q.e, q.w, q.s})
                    if (v == Jungle)
                        return JungleEdge;
            }
            return q.c;
        });
    }

  private:
    static bool valid_temperature_edge(int a, int b) {
        if (is_same(a, b))
            return true;
        const auto *x = biome(a), *y = biome(b);
        if (!x || !y)
            return false;
        const int ca = temperature_category(*x), cb = temperature_category(*y);
        return ca == cb || ca == 2 || cb == 2;
    }
    static bool check_edge(const Cross &q, int match, int edge, int &out) {
        if (!is_same(q.c, match))
            return false;
        out = valid_temperature_edge(q.n, match) && valid_temperature_edge(q.e, match) &&
                      valid_temperature_edge(q.w, match) && valid_temperature_edge(q.s, match)
                  ? q.c
                  : edge;
        return true;
    }
    static bool check_strict(const Cross &q, int match, int edge, int &out) {
        if (q.c != match)
            return false;
        out = is_same(q.n, match) && is_same(q.e, match) && is_same(q.w, match) && is_same(q.s, match) ? q.c : edge;
        return true;
    }
};

// RegionHillsLayer::fillArea 0x1d4ce90: the biome parent and the river-noise
// parent, both with a border.
class RegionHillsLayer : public Layer {
  public:
    const char *name() const override { return "RegionHillsLayer"; }
    std::vector<Layer *> parents() const override { return {parent.get(), rivers_.get()}; }
    RegionHillsLayer(std::uint32_t salt, std::shared_ptr<Layer> p, std::shared_ptr<Layer> rivers)
        : Layer(salt), rivers_(std::move(rivers)) {
        parent = std::move(p);
    }
    // RegionHillsLayer has no init override: only the biome parent (+0x30) is
    // initialised, so the river-noise zooms (+0x40) keep world seed 0 (read
    // from the original's layer objects).
    void fill(LayerData &data, int x, int z, int w, int h) override {
        rivers_->fill(data, x - 1, z - 1, w + 2, h + 2);
        const std::vector<int> noise(data.in, data.in + (w + 2) * (h + 2));
        parent->fill(data, x - 1, z - 1, w + 2, h + 2);
        const int pw = w + 2;
        for (int j = 0; j < h; ++j)
            for (int i = 0; i < w; ++i) {
                init_random(x + i, z + j);
                const int b = data.in[i + 1 + (j + 1) * pw];
                const int r = noise[static_cast<std::size_t>(i + 1 + (j + 1) * pw)];
                const bool mutate = (r - 2) % 29 == 0;
                int out = b;
                if (!is_shallow_ocean(b) && r >= 2 && (r - 2) % 29 == 1 && b < 128) {
                    out = biome(b + 128) ? b + 128 : b;
                } else if (next_random(3) == 0 || mutate) {
                    int l = b;
                    if (b == Desert)
                        l = DesertHills;
                    else if (b == Forest)
                        l = ForestHills;
                    else if (b == BirchForest)
                        l = BirchForestHills;
                    else if (b == RoofedForest)
                        l = Plains;
                    else if (b == Taiga)
                        l = TaigaHills;
                    else if (b == RedwoodTaiga)
                        l = RedwoodTaigaHills;
                    else if (b == TaigaCold)
                        l = TaigaColdHills;
                    else if (b == Plains)
                        l = next_random(3) == 0 ? ForestHills : Forest;
                    else if (b == IceFlats)
                        l = IceMountains;
                    else if (b == Jungle)
                        l = JungleHills;
                    else if (b == Ocean)
                        l = OceanDeep;
                    else if (b == OceanLukewarm)
                        l = OceanLukewarmDeep;
                    else if (b == OceanCold)
                        l = OceanColdDeep;
                    else if (b == OceanFrozen)
                        l = OceanFrozenDeep;
                    else if (b == ExtremeHills)
                        l = ExtremeHillsWithTrees;
                    else if (b == Savanna)
                        l = SavannaRock;
                    else if (is_same(b, MesaRock))
                        l = Mesa;
                    else if (is_ocean(b) && !is_shallow_ocean(b) && next_random(3) == 0)
                        l = next_random(2) == 0 ? Plains : Forest;
                    if (mutate && l != b)
                        l = biome(l + 128) ? l + 128 : b;
                    if (l != b) {
                        int same = 0;
                        if (is_same(data.in[i + 1 + (j + 0) * pw], b))
                            ++same;
                        if (is_same(data.in[i + 2 + (j + 1) * pw], b))
                            ++same;
                        if (is_same(data.in[i + 0 + (j + 1) * pw], b))
                            ++same;
                        if (is_same(data.in[i + 1 + (j + 2) * pw], b))
                            ++same;
                        if (same >= 3)
                            out = l;
                    }
                }
                data.out[i + j * w] = out;
            }
        data.swap();
    }

  private:
    std::shared_ptr<Layer> rivers_;
};

// RareBiomeSpotLayer::fillArea 0x1d4b1b0.
class RareBiomeSpotLayer : public Layer {
  public:
    const char *name() const override { return "RareBiomeSpotLayer"; }
    RareBiomeSpotLayer(std::uint32_t salt, std::shared_ptr<Layer> p) : Layer(salt) { parent = std::move(p); }
    void fill(LayerData &data, int x, int z, int w, int h) override {
        parent->fill(data, x - 1, z - 1, w + 2, h + 2);
        const int pw = w + 2;
        for (int j = 0; j < h; ++j)
            for (int i = 0; i < w; ++i) {
                init_random(x + i, z + j);
                const int v = data.in[i + 1 + (j + 1) * pw];
                data.out[i + j * w] = next_random(57) == 0 && v == Plains ? Plains + 128 : v;
            }
        data.swap();
    }
};

// ShoreLayer::fillArea 0x1d4dc50 with _isJungleCompatible 0x1d4e670,
// _replaceIfNeighborOcean 0x1d4e780 and _isMesa 0x1d4e910.
class ShoreLayer : public Layer {
  public:
    const char *name() const override { return "ShoreLayer"; }
    ShoreLayer(std::uint32_t salt, std::shared_ptr<Layer> p) : Layer(salt) { parent = std::move(p); }
    void fill(LayerData &data, int x, int z, int w, int h) override {
        parent->fill(data, x - 1, z - 1, w + 2, h + 2);
        each_cross(data, w, h, [&](int i, int j, const Cross &q) {
            init_random(x + i, z + j);
            const int k = q.c;
            const auto *b = biome(k);
            const auto any = [&](auto pred) { return pred(q.n) || pred(q.e) || pred(q.w) || pred(q.s); };
            if (k == MushroomIsland)
                return any(is_shallow_ocean) ? MushroomIslandShore : k;
            if (b && b->type == type_of(Jungle)) {
                if (!jungle_compatible(q.n) || !jungle_compatible(q.e) || !jungle_compatible(q.w) ||
                    !jungle_compatible(q.s))
                    return JungleEdge;
                if (any(is_ocean))
                    return Beaches;
                return k;
            }
            if (k == ExtremeHills || k == ExtremeHillsWithTrees || k == SmallerExtremeHills)
                return replace_if_ocean(q, StoneBeach);
            if (b && b->snow)
                return replace_if_ocean(q, ColdBeach);
            if (k == Mesa || k == MesaRock) {
                if (!any(is_ocean)) {
                    if (!is_mesa(q.n) || !is_mesa(q.e) || !is_mesa(q.w) || !is_mesa(q.s))
                        return Desert;
                }
                return k;
            }
            if (!is_ocean(k) && k != River && k != Swampland && any(is_ocean))
                return Beaches;
            return k;
        });
    }

  private:
    static bool jungle_compatible(int id) {
        if (const auto *b = biome(id); b && b->type == type_of(Jungle))
            return true;
        return id == JungleEdge || id == Jungle || id == JungleHills || id == Forest || id == Taiga || is_ocean(id);
    }
    static bool is_mesa(int id) {
        const auto *b = biome(id);
        return b && b->type == type_of(Mesa);
    }
    static int replace_if_ocean(const Cross &q, int replacement) {
        if (is_ocean(q.c))
            return q.c;
        if (is_ocean(q.n) || is_ocean(q.e) || is_ocean(q.w) || is_ocean(q.s))
            return replacement;
        return q.c;
    }
};

// RiverLayer::fillArea 0x1d44710 / riverFilter 0x1d44a10 (no randomness).
class RiverLayer : public Layer {
  public:
    const char *name() const override { return "RiverLayer"; }
    RiverLayer(std::uint32_t salt, std::shared_ptr<Layer> p) : Layer(salt) { parent = std::move(p); }
    void fill(LayerData &data, int x, int z, int w, int h) override {
        parent->fill(data, x - 1, z - 1, w + 2, h + 2);
        const auto filter = [](int v) { return v >= 2 ? 2 + (v & 1) : v; };
        each_cross(data, w, h, [&](int, int, const Cross &q) {
            const int c = filter(q.c);
            if (c == filter(q.w) && c == filter(q.n) && c == filter(q.e) && c == filter(q.s))
                return -1;
            return River;
        });
    }
};

// SmoothLayer::fillArea 0x1d47820 (initRandom only when both pairs match).
class SmoothLayer : public Layer {
  public:
    const char *name() const override { return "SmoothLayer"; }
    SmoothLayer(std::uint32_t salt, std::shared_ptr<Layer> p) : Layer(salt) { parent = std::move(p); }
    void fill(LayerData &data, int x, int z, int w, int h) override {
        parent->fill(data, x - 1, z - 1, w + 2, h + 2);
        each_cross(data, w, h, [&](int i, int j, const Cross &q) {
            int c = q.c;
            if (q.w == q.e && q.n == q.s) {
                init_random(x + i, z + j);
                c = next_random(2) == 0 ? q.w : q.n;
            } else {
                if (q.w == q.e)
                    c = q.w;
                if (q.n == q.s)
                    c = q.n;
            }
            return c;
        });
    }
};

// RiverMixerLayer::fillArea 0x1d4bae0 / init 0x1d4c...: the biome parent and
// the river parent at the same area.
class RiverMixerLayer : public Layer {
  public:
    const char *name() const override { return "RiverMixerLayer"; }
    std::vector<Layer *> parents() const override { return {parent.get(), rivers_.get()}; }
    RiverMixerLayer(std::uint32_t salt, std::shared_ptr<Layer> biomes, std::shared_ptr<Layer> rivers)
        : Layer(salt), rivers_(std::move(rivers)) {
        parent = std::move(biomes);
    }
    void init(std::int64_t seed) override {
        rivers_->init(seed);
        Layer::init(seed);
    }
    void fill(LayerData &data, int x, int z, int w, int h) override {
        rivers_->fill(data, x, z, w, h);
        const std::vector<int> rivers(data.in, data.in + w * h);
        parent->fill(data, x, z, w, h);
        for (int i = 0; i < w * h; ++i) {
            const int b = data.in[i], r = rivers[static_cast<std::size_t>(i)];
            int out = b;
            if (!is_ocean(b) && r == River) {
                if (b == IceFlats)
                    out = FrozenRiver;
                else if (b == MushroomIsland || b == MushroomIslandShore)
                    out = MushroomIslandShore;
                else
                    out = r & 255;
            }
            data.out[i] = out;
        }
        data.swap();
    }

  private:
    std::shared_ptr<Layer> rivers_;
};

// OceanMixerLayer::fillArea 0x1d4a8d0: biomes (+0x40) and ocean temperatures
// (+0x50) at the same area.
class OceanMixerLayer : public Layer {
  public:
    const char *name() const override { return "OceanMixerLayer"; }
    std::vector<Layer *> parents() const override { return {biomes_.get(), oceans_.get()}; }
    OceanMixerLayer(std::uint32_t salt, std::shared_ptr<Layer> biomes, std::shared_ptr<Layer> oceans)
        : Layer(salt), biomes_(std::move(biomes)), oceans_(std::move(oceans)) {}
    void init(std::int64_t seed) override {
        biomes_->init(seed);
        oceans_->init(seed);
        Layer::init(seed);
    }
    void fill(LayerData &data, int x, int z, int w, int h) override {
        biomes_->fill(data, x, z, w, h);
        LayerData temperature;
        oceans_->fill(temperature, x, z, w, h);
        for (int i = 0; i < w * h; ++i) {
            const int b = data.in[i], o = temperature.in[i];
            int out = b;
            if (is_ocean(b) && is_ocean(o)) {
                if (b == OceanDeep) {
                    out = o == OceanLukewarm ? OceanLukewarmDeep
                          : o == Ocean       ? OceanDeep
                          : o == OceanCold   ? OceanColdDeep
                          : o == OceanFrozen ? OceanFrozenDeep
                                             : o;
                } else
                    out = o;
            }
            data.out[i] = out;
        }
        data.swap();
    }

  private:
    std::shared_ptr<Layer> biomes_, oceans_;
};

// VoronoiZoom::fillArea 0x1d42060.
class VoronoiZoom : public Layer {
  public:
    const char *name() const override { return "VoronoiZoom"; }
    VoronoiZoom(std::uint32_t salt, std::shared_ptr<Layer> p) : Layer(salt) { parent = std::move(p); }
    void fill(LayerData &data, int x, int z, int w, int h) override {
        x -= 2;
        z -= 2;
        const int px = x >> 2, pz = z >> 2, pw = (w >> 2) + 2, ph = (h >> 2) + 2;
        parent->fill(data, px, pz, pw, ph);
        const int tw = (pw - 1) << 2;
        std::vector<int> tmp(static_cast<std::size_t>(tw * ((ph - 1) << 2)));
        const auto offset = [&](float add) {
            return (static_cast<float>(next_random(1024)) / 1024.0f - 0.5f) * 3.6f + add;
        };
        for (int j = 0; j < ph - 1; ++j) {
            int a = data.in[0 + j * pw], b = data.in[0 + (j + 1) * pw];
            for (int i = 0; i < pw - 1; ++i) {
                init_random(static_cast<std::int64_t>((i + px) << 2), static_cast<std::int64_t>((j + pz) << 2));
                const float x0 = offset(0.0f), z0 = offset(0.0f);
                init_random(static_cast<std::int64_t>((i + px + 1) << 2), static_cast<std::int64_t>((j + pz) << 2));
                const float x1 = offset(4.0f), z1 = offset(0.0f);
                init_random(static_cast<std::int64_t>((i + px) << 2), static_cast<std::int64_t>((j + pz + 1) << 2));
                const float x2 = offset(0.0f), z2 = offset(4.0f);
                init_random(static_cast<std::int64_t>((i + px + 1) << 2),
                            static_cast<std::int64_t>((j + pz + 1) << 2));
                const float x3 = offset(4.0f), z3 = offset(4.0f);
                const int c = data.in[i + 1 + j * pw] & 255, d = data.in[i + 1 + (j + 1) * pw] & 255;
                for (int jz = 0; jz < 4; ++jz) {
                    int index = ((j << 2) + jz) * tw + (i << 2);
                    for (int jx = 0; jx < 4; ++jx) {
                        const float fz = static_cast<float>(jz), fx = static_cast<float>(jx);
                        const float e0 = (fz - z0) * (fz - z0) + (fx - x0) * (fx - x0);
                        const float e1 = (fz - z1) * (fz - z1) + (fx - x1) * (fx - x1);
                        const float e2 = (fz - z2) * (fz - z2) + (fx - x2) * (fx - x2);
                        const float e3 = (fz - z3) * (fz - z3) + (fx - x3) * (fx - x3);
                        int v;
                        if (e1 > e0 && e2 > e0 && e3 > e0)
                            v = a;
                        else if (e0 > e1 && e2 > e1 && e3 > e1)
                            v = c;
                        else if (e0 > e2 && e1 > e2 && e3 > e2)
                            v = b;
                        else
                            v = d;
                        tmp[static_cast<std::size_t>(index++)] = v;
                    }
                }
                a = c;
                b = d;
            }
        }
        for (int j = 0; j < h; ++j)
            std::memcpy(data.out + j * w, tmp.data() + (j + (z & 3)) * tw + (x & 3),
                        sizeof(int) * static_cast<std::size_t>(w));
        data.swap();
    }
};
} // namespace

BiomeLayers overworld_layers(std::uint32_t seed, int generator_type) {
    // OverworldDimension::createDefaultLayers 0x1d4f260.
    std::shared_ptr<Layer> l = std::make_shared<IslandLayer>(1);
    l = std::make_shared<ZoomLayer>(2000, l, true); // FuzzyZoomLayer
    l = std::make_shared<AddIslandLayer>(1, l);
    l = std::make_shared<ZoomLayer>(2001, l);
    l = std::make_shared<AddIslandLayer>(2, l);
    l = std::make_shared<AddIslandLayer>(50, l);
    l = std::make_shared<AddIslandLayer>(70, l);
    l = std::make_shared<RemoveTooMuchOceanLayer>(2, l);
    std::shared_ptr<Layer> oceans = l;
    oceans = std::make_shared<AddOceanTemperatureLayer>(2, oceans);
    oceans = std::make_shared<AddOceanEdgeLayer>(2, oceans);
    oceans = zoom(2001, oceans, 6);
    l = std::make_shared<AddSnowLayer>(2, l);
    l = std::make_shared<AddIslandLayer>(3, l);
    l = std::make_shared<AddEdgeLayer>(2, l, 0);
    l = std::make_shared<AddEdgeLayer>(2, l, 1);
    l = std::make_shared<AddEdgeLayer>(3, l, 2);
    l = std::make_shared<ZoomLayer>(2002, l);
    l = std::make_shared<ZoomLayer>(2003, l);
    l = std::make_shared<AddIslandLayer>(4, l);
    l = std::make_shared<AddMushroomIslandLayer>(5, l);
    l = std::make_shared<AddDeepOceanLayer>(4, l);
    l = zoom(1000, l, 0);
    const int size = generator_type == 0 ? 2 : 4;
    std::shared_ptr<Layer> rivers = zoom(1000, l, 0);
    rivers = std::make_shared<RiverInitLayer>(100, rivers);
    std::shared_ptr<Layer> biomes = l;
    biomes = std::make_shared<BiomeInitLayer>(200, biomes, generator_type);
    biomes = zoom(1000, biomes, 2);
    biomes = std::make_shared<BiomeEdgeLayer>(1000, biomes);
    std::shared_ptr<Layer> hills = zoom(1000, rivers, 2);
    biomes = std::make_shared<RegionHillsLayer>(1000, biomes, hills);
    rivers = zoom(1000, rivers, 2);
    rivers = zoom(1000, rivers, size);
    rivers = std::make_shared<RiverLayer>(1, rivers);
    rivers = std::make_shared<SmoothLayer>(1000, rivers);
    biomes = std::make_shared<RareBiomeSpotLayer>(1001, biomes);
    for (int i = 0; i < size; ++i) {
        biomes = std::make_shared<ZoomLayer>(1000 + static_cast<std::uint32_t>(i), biomes);
        if (i == 0)
            biomes = std::make_shared<AddIslandLayer>(3, biomes);
        if (i == 1)
            biomes = std::make_shared<ShoreLayer>(1000, biomes);
    }
    biomes = std::make_shared<SmoothLayer>(1000, biomes);
    biomes = std::make_shared<RiverMixerLayer>(100, biomes, rivers);
    biomes = std::make_shared<OceanMixerLayer>(100, biomes, oceans);
    std::shared_ptr<Layer> zoomed = std::make_shared<VoronoiZoom>(10, biomes);
    const auto world = static_cast<std::int64_t>(static_cast<std::uint64_t>(seed));
    biomes->init(world);
    zoomed->init(world);
    return {biomes, zoomed};
}

std::array<int, 100> raw_biomes(const BiomeLayers &layers, int x, int z) {
    LayerData data;
    layers.raw->fill(data, x, z, 10, 10);
    std::array<int, 100> out{};
    std::copy(data.in, data.in + 100, out.begin());
    return out;
}

std::array<int, 256> chunk_biomes(const BiomeLayers &layers, int chunk_x, int chunk_z) {
    LayerData data;
    layers.zoomed->fill(data, chunk_x * 16, chunk_z * 16, 16, 16);
    std::array<int, 256> out{};
    std::copy(data.in, data.in + 256, out.begin());
    return out;
}
} // namespace bds
