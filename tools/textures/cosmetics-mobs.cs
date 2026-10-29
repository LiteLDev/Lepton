// U18 cosmetics: animal variant skins, pride bees, villager clothes, icons. Original procedural art.
using System;
using System.Collections.Generic;
using System.Drawing;

namespace LeptonTex {

public static class Mobs {
    static Color H(string s) { return Col.Hex(s); }
    static Cube C(string bone, int idx, double ox, double oy, double oz, int w, int h, int d, int u, int v) {
        return new Cube(bone, idx, ox, oy, oz, w, h, d, u, v);
    }

    // ---- geometry layouts (cube sizes and UV origins of the vanilla models) ------------------------

    public static readonly Cube[] CowGeo = {
        C("head", 0, -4, 16, -15, 8, 8, 6, 0, 0), C("head", 1, -3, 16, -16, 6, 3, 1, 1, 33),
        C("head", 2, -5, 22, -14, 1, 3, 1, 22, 0), C("head", 3, 4, 22, -14, 1, 3, 1, 22, 0),
        C("body", 0, -6, 29, 14, 12, 18, 10, 18, 4), C("body", 1, -2, 29, 13, 4, 6, 1, 52, 0),
        C("leg", 0, -6, 0, 4, 4, 12, 4, 0, 16),
    };
    public static readonly Cube[] PigGeo = {
        C("head", 0, -4, 8, -15, 8, 8, 8, 0, 0), C("head", 1, -2, 9, -16, 4, 3, 1, 16, 16),
        C("body", 0, -5, 2, -5, 10, 16, 8, 28, 32), C("body", 1, -5, 2, -5, 10, 16, 8, 28, 8),
        C("leg", 0, -5, 0, 4, 4, 6, 4, 0, 16),
    };
    public static readonly Cube[] ChickenGeo = {
        C("body", 0, -3, 4, -3, 6, 8, 6, 0, 9), C("head", 0, -2, 9, -6, 4, 6, 3, 0, 0),
        C("comb", 0, -1, 9, -7, 2, 2, 2, 14, 4), C("beak", 0, -2, 11, -8, 4, 2, 2, 14, 0),
        C("leg", 0, -3, 0, -2, 3, 5, 3, 26, 0), C("wing", 0, -4, 7, -3, 1, 4, 6, 24, 13),
    };
    public static readonly Cube[] AllayGeo = {
        C("head", 0, -2.5, 5, -2.5, 5, 5, 5, 0, 0), C("body", 0, -1.5, 1, -1, 3, 4, 2, 0, 10),
        C("body", 1, -1.5, 0, -1, 3, 5, 2, 0, 16), C("arm", 0, -2.5, 1, -1, 1, 4, 2, 23, 0),
        C("arm", 1, 1.5, 1, -1, 1, 4, 2, 23, 6), C("wing", 0, 0.5, -1, 1, 0, 5, 8, 16, 14),
    };
    public static readonly Cube[] RabbitGeo = {
        C("body", 0, -4, 1, -5, 8, 6, 10, 0, 0), C("tail", 0, -2, 5, 3, 4, 4, 4, 20, 16),
        C("head", 0, -2.5, 4.3, -8.1, 5, 5, 5, 0, 16), C("ear", 0, 0.5, 9.3, -5.1, 2, 5, 1, 32, 0),
        C("ear", 1, -2.5, 9.3, -5.1, 2, 5, 1, 26, 0), C("frontleg", 0, -2.9, -2.4, -2.8, 2, 4, 2, 36, 18),
        C("frontleg", 1, 1, -2.4, -2.8, 2, 4, 2, 44, 18), C("backleg", 0, -4, 0, -1, 2, 1, 6, 20, 24),
        C("backleg", 1, 2, 0, -1, 2, 1, 6, 36, 24),
    };
    public static readonly Cube[] LlamaGeo = {
        C("snout", 0, -2, 27, -16, 4, 4, 9, 0, 0), C("head", 0, -4, 15, -12, 8, 18, 6, 0, 14),
        C("ear", 0, -4, 33, -10, 3, 3, 2, 17, 0), C("chest", 0, -11.5, 13, 3, 8, 8, 3, 45, 28),
        C("chest", 1, 2.5, 13, 3, 8, 8, 3, 45, 41), C("body", 0, -6, 11, -5, 12, 18, 10, 29, 0),
        C("leg", 0, -5.5, 0, 4, 4, 14, 4, 29, 29),
    };
    public static readonly Cube[] DolphinGeo = {
        C("body", 0, -4, 0, -3, 8, 7, 13, 0, 13), C("head", 0, -4, 0, -9, 8, 7, 6, 0, 0),
        C("nose", 0, -1, 0, -13, 2, 2, 4, 0, 13), C("tail", 0, -2, 0, 10, 4, 5, 11, 0, 33),
        C("tailfin", 0, -5, 2, 19, 10, 1, 6, 0, 49), C("backfin", 0, -0.5, 6.25, 1, 1, 5, 4, 29, 0),
        C("fin", 0, 3, 1, -2.5, 8, 1, 4, 40, 0), C("fin", 1, -11, 1, -2.5, 8, 1, 4, 40, 6),
    };
    public static readonly Cube[] SlimeGeo = {
        C("outer", 0, -4, 0, -4, 8, 8, 8, 0, 0), C("cube", 0, -3, 1, -3, 6, 6, 6, 0, 16),
        C("eye", 0, -3.3, 4, -3.5, 2, 2, 2, 32, 0), C("eye", 1, 1.3, 4, -3.5, 2, 2, 2, 32, 4),
        C("mouth", 0, 0, 2, -3.5, 1, 1, 1, 32, 8),
    };
    public static readonly Cube[] ArmadilloGeo = {
        C("shell", 0, -4, 2, -6, 8, 8, 12, 0, 20), C("body", 0, -4, 2, -6, 8, 8, 12, 0, 40),
        C("tail", 0, -0.5, 0, 5, 1, 6, 1, 44, 53), C("head", 0, -1.5, 1, -8, 3, 5, 2, 43, 15),
        C("ear", 0, -3.5, 4, -7.6, 2, 5, 0, 43, 10), C("ear", 1, 1.5, 4, -7.6, 2, 5, 0, 47, 10),
        C("leg", 0, -3, 0, 3, 2, 3, 2, 51, 31), C("leg", 1, 1, 0, 3, 2, 3, 2, 42, 31),
        C("leg", 2, -3, 0, -5, 2, 3, 2, 51, 43), C("leg", 3, 1, 0, -5, 2, 3, 2, 42, 43),
        C("rolled", 0, -5, 0, -6, 10, 10, 10, 0, 0),
    };
    public static readonly Cube[] FrogGeo = {
        C("body", 0, -3.5, 1, -4, 7, 3, 9, 3, 1), C("bodytop", 0, -3.5, 3, -4, 7, 0, 9, 23, 22),
        C("headtop", 0, -3.5, 5, -4, 7, 0, 9, 23, 13), C("head", 0, -3.5, 3, -4, 7, 3, 9, 0, 13),
        C("eye", 0, -3.5, 6, -3, 3, 2, 3, 0, 0), C("eye", 1, 0.5, 6, -3, 3, 2, 3, 0, 5),
        C("croak", 0, -3.5, 1.1, -3.9, 7, 2, 3, 26, 5), C("tongue", 0, -2, 3.1, -2.1, 4, 0, 7, 17, 13),
        C("arm", 0, 3, 0, -3.5, 2, 3, 3, 0, 32), C("hand", 0, 0, 0, -7.5, 8, 0, 8, 18, 40),
        C("arm", 1, -5, 0, -3.5, 2, 3, 3, 0, 38), C("hand", 1, -8, 0, -7.5, 8, 0, 8, 2, 40),
        C("leg", 0, 2.5, 0, 2, 3, 3, 4, 14, 25), C("foot", 0, 1.5, 0, 0, 8, 0, 8, 2, 32),
        C("leg", 1, -5.5, 0, 2, 3, 3, 4, 0, 25), C("foot", 1, -9.5, 0, 0, 8, 0, 8, 18, 32),
    };
    public static readonly Cube[] BeeGeo = {
        C("body", 0, -3, 2, -5, 7, 7, 10, 0, 0), C("antenna", 0, 2, 7, -8, 1, 2, 3, 2, 0),
        C("antenna", 1, -2, 7, -8, 1, 2, 3, 2, 3), C("stinger", 0, 0.5, 5, 5, 0, 1, 2, 26, 7),
        C("wing", 0, -10, 9, -3, 9, 0, 6, 0, 18), C("wing", 1, 2, 9, -3, 9, 0, 6, 9, 24),
        C("leg", 0, -3, 0, -2, 7, 2, 0, 26, 1), C("leg", 1, -3, 0, 0, 7, 2, 0, 26, 3),
        C("leg", 2, -3, 0, 2, 7, 2, 0, 26, 5),
    };
    public static readonly Cube[] VillagerGeo = {
        C("helmet", 0, -4, 24, -4, 8, 10, 8, 32, 0), C("brim", 0, -8, 16, -6, 16, 16, 1, 30, 47),
        C("body", 0, -4, 12, -3, 8, 12, 6, 16, 20), C("robe", 0, -4, 6, -3, 8, 18, 6, 0, 38),
        C("arms", 0, -4, 16, -2, 8, 4, 4, 40, 38), C("arms", 1, -8, 16, -2, 4, 8, 4, 44, 22),
        C("leg", 0, -4, 0, -2, 4, 12, 4, 0, 22),
    };
    public static readonly Cube[] ZombieVillagerGeo = {
        C("helmet", 0, -4, 24, -4, 8, 10, 8, 32, 0), C("brim", 0, -8, 16, -6, 16, 16, 1, 30, 47),
        C("body", 0, -4, 12, -3, 8, 12, 6, 16, 20), C("robe", 0, -4, 6, -3, 8, 18, 6, 0, 38),
        C("arms", 1, -8, 12, -2, 4, 12, 4, 44, 22), C("leg", 0, -4, 0, -2, 4, 12, 4, 0, 22),
    };
    public static readonly Cube[] BabyVillagerGeo = {
        C("body", 0, -2, 3, -1.5, 4, 5, 3, 0, 15), C("robe", 0, -2, 2, -1.5, 4, 6, 3, 16, 21),
        C("helmet", 0, -4, 8, -3.5, 8, 8, 7, 0, 30), C("brim", 0, -7, 12, -6, 14, 1, 12, 0, 45),
        C("arms", 0, -2, 4.6, -2.8, 4, 2, 2, 24, 17), C("arms", 1, 2, 3.6, -2.8, 2, 4, 2, 16, 15),
        C("arms", 2, -4, 3.6, -2.8, 2, 4, 2, 36, 15), C("leg", 0, -2, 0, -1, 2, 3, 2, 8, 23),
        C("leg", 1, 0, 0, -1, 2, 3, 2, 0, 23),
    };
    public static readonly Cube[] BabyZombieVillagerGeo = {
        C("body", 0, -2, 3, -1.5, 4, 5, 3, 0, 15), C("robe", 0, -2, 2, -1.5, 4, 6, 3, 16, 22),
        C("helmet", 0, -4, 8, -3.5, 8, 8, 7, 0, 31), C("brim", 0, -7, 12, -6, 14, 1, 12, 0, 46),
        C("arms", 1, -4, 2.75, -1, 2, 5, 2, 24, 15), C("arms", 2, 2, 2.75, -1, 2, 5, 2, 16, 15),
        C("leg", 0, -2, 0, -1, 2, 3, 2, 8, 23), C("leg", 1, 0, 0, -1, 2, 3, 2, 0, 23),
    };

    // ---- helpers ------------------------------------------------------------------------------

    static bool Spot(Px p, int seed, double scale, double threshold) {
        return Noise.Fbm(p.X + 50, p.Y + 50, p.Z + 50, seed, scale, 2) > threshold;
    }
    static Color Eye(Color white, Color pupil, bool pupilHere) { return pupilHere ? pupil : white; }
    static readonly Color Black = Col.Hex("#1a1414");
    static readonly Color EyeWhite = Col.Hex("#f2f0ea");

    static void Emit(string root, string rel, int w, int h, Cube[] geo, PaintFn fn) {
        var b = Painter.New(w, h);
        Painter.Paint(b, geo, fn);
        Painter.Save(b, System.IO.Path.Combine(root, rel));
        b.Dispose();
    }

    // ---- cow ----------------------------------------------------------------------------------

    class CowStyle {
        public Color Coat, Spot, Muzzle, Horn, Hoof, Udder;
        public double SpotScale = 5, SpotThreshold = 2; // > 1 = no spots
        public bool Belt, Blaze;
        public int Seed;
    }

    static Color PaintCow(Px p, CowStyle s) {
        Color coat = s.Coat;
        bool spot = Spot(p, s.Seed, s.SpotScale, s.SpotThreshold);
        if (spot) coat = s.Spot;
        if (s.Belt && p.Is("body", 0) && p.Y >= p.Cube.OY + 7 && p.Y < p.Cube.OY + 12) coat = s.Spot;
        if (p.Is("head", 0)) {
            if (p.Face == "north") {
                if (s.Blaze && p.I >= 3 && p.I <= 4 && p.J <= 5) coat = s.Spot;
                if (p.J == 3 && (p.I <= 1 || p.I >= 6)) return Eye(EyeWhite, Black, p.I == 1 || p.I == 6);
                if (p.J == 2 && (p.I == 1 || p.I == 6)) return Col.Shade(coat, 0.7); // brow
            }
            return Painter.Fur(coat, p, s.Seed, 0.18);
        }
        if (p.Is("head", 1)) {
            if (p.Face == "north" && p.J == 1 && (p.I == 1 || p.I == 4)) return Col.Shade(s.Muzzle, 0.55);
            return Painter.Fur(s.Muzzle, p, s.Seed + 3, 0.1);
        }
        if (p.Is("head", 2) || p.Is("head", 3)) return Painter.Lit(p.J == 0 || p.Face == "up" ? Col.Shade(s.Horn, 0.75) : s.Horn, p);
        if (p.Is("body", 1)) return Painter.Lit((p.I == 0 || p.I == 3) && p.J >= 4 ? Col.Shade(s.Udder, 0.8) : s.Udder, p);
        if (p.Is("leg")) {
            if (p.Face == "down" || p.J >= 10) return Painter.Fur(s.Hoof, p, s.Seed + 5, 0.08);
            return Painter.Fur(coat, p, s.Seed, 0.16);
        }
        if (p.Is("body", 0) && p.Face == "south" && !spot && !s.Belt) coat = Col.Shade(coat, 1.06); // belly
        return Painter.Fur(coat, p, s.Seed, 0.16);
    }

    // ---- pig ----------------------------------------------------------------------------------

    class PigStyle {
        public Color Skin, Spot, Snout, Hoof;
        public double SpotScale = 4, SpotThreshold = 2;
        public bool Belt;
        public int Seed;
    }

    static Color PaintPig(Px p, PigStyle s) {
        Color skin = s.Skin;
        if (Spot(p, s.Seed, s.SpotScale, s.SpotThreshold)) skin = s.Spot;
        if (s.Belt && p.Is("body") && p.Y >= p.Cube.OY + 3 && p.Y < p.Cube.OY + 7) skin = s.Spot;
        if (p.Is("head", 0) && p.Face == "north") {
            if (p.J == 3 && (p.I <= 1 || p.I >= 6)) return Eye(EyeWhite, Black, p.I == 1 || p.I == 6);
        }
        if (p.Is("head", 1)) {
            if (p.Face == "north" && p.J == 1 && (p.I == 1 || p.I == 2)) return Col.Shade(s.Snout, 0.5);
            return Painter.Fur(s.Snout, p, s.Seed + 3, 0.08);
        }
        if (p.Is("leg") && (p.Face == "down" || p.J >= 5)) return Painter.Fur(s.Hoof, p, s.Seed + 5, 0.06);
        return Painter.Fur(skin, p, s.Seed, 0.14);
    }

    // ---- chicken ------------------------------------------------------------------------------

    class ChickenStyle {
        public Color Feather, Fleck, Comb, Beak, Legs;
        public double FleckScale = 1.6, FleckThreshold = 2;
        public int Seed;
    }

    static Color PaintChicken(Px p, ChickenStyle s) {
        if (p.Is("leg")) {
            // A thin leg in the middle column and three toes on the ground; the rest is empty.
            bool column = (p.Face == "north" || p.Face == "south" || p.Side) && p.I == 1 && p.J < 4;
            bool toes = (p.Face == "north" || p.Face == "south" || p.Side) && p.J == 4;
            bool ground = p.Face == "down" && (p.I == 1 || p.J == 1);
            if (column || toes || ground) return Painter.Lit(p.J == 4 ? Col.Shade(s.Legs, 0.85) : s.Legs, p);
            return Col.Clear;
        }
        if (p.Is("beak")) return Painter.Lit(p.Face == "down" ? Col.Shade(s.Beak, 0.8) : s.Beak, p);
        if (p.Is("comb")) return Painter.Fur(s.Comb, p, s.Seed + 9, 0.12);
        Color f = s.Feather;
        if (Spot(p, s.Seed, s.FleckScale, s.FleckThreshold)) f = s.Fleck;
        if (p.Is("head") && p.J == 1 && ((p.Face == "north" && (p.I == 0 || p.I == 3)) || (p.Side && p.I == 0)))
            return Black;
        if (p.Is("wing") && p.J >= 3) f = Col.Shade(f, 0.85); // flight feathers
        return Painter.Fur(f, p, s.Seed, 0.2);
    }

    // ---- allay ----------------------------------------------------------------------------------

    static Color PaintAllay(Px p, Color body, Color core, Color eye) {
        if (p.Is("head")) {
            if (p.Face == "north" && p.J == 2 && (p.I == 1 || p.I == 3)) return eye;
            return Col.Alpha(Painter.Fur(Col.Shade(body, 1.1), p, 21, 0.1), 235);
        }
        if (p.Is("body", 1)) return Painter.Lit(core, p);                 // the glowing core
        if (p.Is("body", 0)) return Col.Alpha(Painter.Fur(body, p, 22, 0.12), 150);
        if (p.Is("arm")) return Col.Alpha(Painter.Lit(body, p), 190);
        if (p.Is("wing")) {
            bool edge = p.J == 0 || p.I == 0 || p.I == p.FW - 1;
            return Col.Alpha(Col.Shade(body, edge ? 1.25 : 1.12), edge ? 200 : 120);
        }
        return Col.Clear;
    }

    static Color Hsl(double h, double s, double l) {
        h = ((h % 360) + 360) % 360 / 360.0;
        Func<double, double, double, double> hue = (pp, q, t) => {
            if (t < 0) t += 1; if (t > 1) t -= 1;
            if (t < 1.0 / 6) return pp + (q - pp) * 6 * t;
            if (t < 0.5) return q;
            if (t < 2.0 / 3) return pp + (q - pp) * (2.0 / 3 - t) * 6;
            return pp;
        };
        double r, g, b;
        if (s == 0) r = g = b = l;
        else {
            double q = l < 0.5 ? l * (1 + s) : l + s - l * s, pp = 2 * l - q;
            r = hue(pp, q, h + 1.0 / 3); g = hue(pp, q, h); b = hue(pp, q, h - 1.0 / 3);
        }
        return Color.FromArgb(255, (int)Math.Round(r * 255), (int)Math.Round(g * 255), (int)Math.Round(b * 255));
    }

    // ---- bees ---------------------------------------------------------------------------------

    public static readonly Dictionary<string, string[]> BeeFlags = new Dictionary<string, string[]> {
        { "acebee", new[] { "#1c1c1c", "#a3a3a3", "#ffffff", "#7e1f86" } },
        { "agenbee", new[] { "#1c1c1c", "#bcc4c7", "#ffffff", "#b7f684", "#ffffff", "#bcc4c7", "#1c1c1c" } },
        { "arobee", new[] { "#3da542", "#a7d379", "#ffffff", "#a9a9a9", "#1c1c1c" } },
        { "beefluid", new[] { "#ff76a4", "#ffffff", "#c011d7", "#1c1c1c", "#2f3cbe" } },
        { "beesexual", new[] { "#d60270", "#d60270", "#9b4f96", "#0038a8", "#0038a8" } },
        { "beequeer", new[] { "#b57edc", "#ffffff", "#4a8123" } },
        { "enbee", new[] { "#fcf434", "#ffffff", "#9c59d1", "#2c2c2c" } },
        { "gaybee", new[] { "#e40303", "#ff8c00", "#ffed00", "#008026", "#24408e", "#732982" } },
        { "interbee", new[] { "#ffd800", "#ffd800", "#7902aa", "#ffd800", "#ffd800" } },
        { "lesbeean", new[] { "#d52d00", "#ff9a56", "#ffffff", "#d362a4", "#a30262" } },
        { "panbee", new[] { "#ff218c", "#ffd800", "#21b1ff" } },
        { "polysexbee", new[] { "#f61cb9", "#07d569", "#1c92f6" } },
        { "transbee", new[] { "#5bcefa", "#f5a9b8", "#ffffff", "#f5a9b8", "#5bcefa" } },
        { "helen", new[] { "#c8a2e8", "#f4ecff", "#c8a2e8", "#f4ecff" } },
        { "mlbeem", new[] { "#078d70", "#26ceaa", "#98e8c1", "#ffffff", "#7bade2", "#5049cc", "#3d1a78" } },
        { "polyamorbee", new[] { "#1438c8", "#d7141e", "#1c1c1c" } },
        { "salmacibee", new[] { "#ff6fb5", "#ffd6ea", "#b8f2ff", "#4cc9f0" } },
    };

    static Color PaintBee(Px p, string name, bool angry, bool nectar) {
        string[] flag = BeeFlags[name];
        if (p.Is("wing")) {
            bool vein = p.J == 2 || (p.I % 4 == 0 && p.J < 4);
            return vein ? Col.Hex("#b4d8ec") : Col.Alpha(Col.Hex("#e8f6ff"), 150);
        }
        if (p.Is("antenna")) return Col.Hex("#2a2016");
        if (p.Is("stinger")) return p.J == 0 || p.I == 1 ? Col.Hex("#d6d2c8") : Col.Clear;
        if (p.Is("leg")) return (p.I % 3 == 1) ? Col.Hex("#2a2016") : Col.Clear;
        // Body: flag bands from front to back.
        double t = (p.Z - p.Cube.OZ) / p.Cube.D;
        int band = Math.Max(0, Math.Min(flag.Length - 1, (int)Math.Floor(t * flag.Length)));
        Color c = H(flag[band]);
        if (name == "helen" && Spot(p, 77, 1.2, 0.68)) c = H("#ffffff");
        if (name == "polyamorbee" && p.Face == "north") c = H("#f2c230");
        if (name == "interbee" && band == 2 && (p.Face == "east" || p.Face == "west" || p.Face == "up") && p.FY > 0.25 && p.FY < 0.75) c = H("#ffd800");
        if (p.Face == "north") {
            // Face: two big eyes, a mouth.
            bool eyeL = p.I >= 1 && p.I <= 2 && p.J >= 1 && p.J <= 2, eyeR = p.I >= 4 && p.I <= 5 && p.J >= 1 && p.J <= 2;
            if (eyeL || eyeR) {
                bool inner = (eyeL && p.I == 2) || (eyeR && p.I == 4);
                if (angry && inner && p.J == 2) return H("#c81818");
                if (!angry && p.J == 1 && (p.I == 1 || p.I == 4)) return H("#f2f0ea");
                return H("#16121c");
            }
            if (angry && p.J == 0 && (p.I == 2 || p.I == 4)) return H("#16121c"); // frown
            if (p.J == 4 && p.I >= 2 && p.I <= 4) return Col.Shade(c, 0.55);
        }
        if (nectar && (p.Face == "down" || p.Side) && Noise.Grain(p, 404) > 0.45) return H(p.Face == "down" ? "#f6c836" : "#ffd75e");
        if (p.Face == "down") c = Col.Shade(c, 0.85);
        return Painter.Fur(c, p, 31, 0.12);
    }

    // ---- villager clothes --------------------------------------------------------------------

    static Color Brim(Px p, Color straw, Color band, bool baby) {
        // A round straw brim on the horizontal brim plane.
        double cx = p.FW / 2.0, cy = p.FH / 2.0;
        double dx = p.I + 0.5 - cx, dy = p.J + 0.5 - cy;
        double r = Math.Sqrt(dx * dx + dy * dy), rmax = Math.Min(cx, cy);
        if (r > rmax) return Col.Clear;
        Color c = (p.I + p.J) % 3 == 0 ? Col.Shade(straw, 0.88) : straw;
        if (r > rmax - 1) c = Col.Shade(straw, 0.8);
        return c;
    }

    static bool BrimFace(Px p, bool baby) { return baby ? p.Face == "up" || p.Face == "down" : p.Face == "north" || p.Face == "south"; }

    static Color Beach(Px p, bool baby, bool zombie) {
        Color straw = H("#e7c47c"), band = H("#3f8fd0"), shirt = H("#f1e7cc"), stripe = H("#6cb6e0"), sash = H("#21a39a"), sandal = H("#7a4a26");
        if (zombie) { straw = H("#b9a476"); band = H("#4d7891"); shirt = H("#c9c3a6"); stripe = H("#6f97a3"); sash = H("#3d7f6f"); }
        string b = p.Cube.Bone;
        if (b == "brim") return BrimFace(p, baby) ? Brim(p, straw, band, baby) : Col.Clear;
        if (b == "helmet") {
            int rows = baby ? 3 : 3;
            if (p.Face == "up") return Painter.Lit((p.I + p.J) % 3 == 0 ? Col.Shade(straw, 0.88) : straw, p);
            if (p.Face == "down") return Col.Clear;
            if (p.J < rows - 1) return Painter.Lit((p.I + p.J) % 3 == 0 ? Col.Shade(straw, 0.88) : straw, p);
            if (p.J == rows - 1) return Painter.Lit(band, p);
            return Col.Clear;
        }
        if (b == "body" || b == "robe") {
            int len = p.Cube.H;
            bool isRobe = b == "robe";
            if (isRobe && p.J >= (baby ? 5 : 15)) return Col.Clear; // bare knees below the hem
            int sashRow = baby ? 2 : 6;
            if (p.J == sashRow && p.Face != "up" && p.Face != "down") return Painter.Lit(sash, p);
            Color c = (p.I % 3 == 1 && p.Face != "up" && p.Face != "down") ? stripe : shirt;
            if (zombie && Noise.Grain(p, 91) > 0.75) return Col.Clear; // torn
            return Painter.Fur(c, p, 40, 0.08);
        }
        if (b == "arms") {
            if (p.Face == "up" || p.Face == "down") return Col.Clear;
            int sleeve = baby ? 1 : 3;
            if (p.Cube.Index == 0 && !baby) return Col.Clear;
            return p.J < sleeve ? Painter.Fur(shirt, p, 41, 0.08) : Col.Clear;
        }
        if (b == "leg") {
            int last = p.Cube.H - 1;
            if (p.Face == "down" || (p.J == last && p.Face != "up")) return Painter.Lit(sandal, p);
            if (p.J == last - 1 && (p.Face == "north") && (p.I == 1 || p.I == p.FW - 2)) return Painter.Lit(sandal, p);
            return Col.Clear;
        }
        return Col.Clear;
    }

    static Color Ocean(Px p, bool baby, bool zombie) {
        Color navy = H("#25365f"), white = H("#eef1f5"), red = H("#c23b3b"), gold = H("#e2b13c"), boot = H("#2a2420");
        if (zombie) { navy = H("#2f3d52"); white = H("#bfc6c2"); red = H("#8a4343"); gold = H("#a88d4b"); }
        string b = p.Cube.Bone;
        if (b == "brim") return Col.Clear;
        if (b == "helmet") {
            // A knotted bandana over the top of the head.
            if (p.Face == "down") return Col.Clear;
            if (p.Face == "up") return Painter.Lit(Noise.Grain(p, 12) > 0.6 ? white : red, p);
            if (p.J < 2) return Painter.Lit(Noise.Grain(p, 12) > 0.6 ? white : red, p);
            if (p.Face == "south" && p.J < 4 && p.I >= p.FW / 2 - 1 && p.I <= p.FW / 2) return Painter.Lit(red, p);
            return Col.Clear;
        }
        if (b == "body") {
            // Breton shirt.
            Color c = (p.J % 2 == 0) ? white : navy;
            return Painter.Fur(c, p, 50, 0.06);
        }
        if (b == "robe") {
            if (zombie && Noise.Grain(p, 93) > 0.72) return Col.Clear;
            Color c = navy;
            int collar = baby ? 1 : 3;
            if (p.J < collar && (p.Face == "north" || p.Face == "south" || p.Face == "up")) c = white;
            if (p.Face == "north" && !baby && p.J < collar + 2 && p.I >= 3 && p.I <= 4) c = white; // collar V
            if (p.Face == "north" && !baby && (p.J == 6 || p.J == 9 || p.J == 12) && (p.I == 2 || p.I == 5)) c = gold;
            if (p.J == p.Cube.H - 1 && p.Face != "up") c = white;
            return Painter.Fur(c, p, 51, 0.07);
        }
        if (b == "arms") {
            if (p.Face == "up" || p.Face == "down") return Col.Clear;
            if (p.Cube.Index == 0 && !baby) return Painter.Fur(navy, p, 52, 0.07);
            int len = baby ? 3 : (zombie ? 8 : 6);
            if (p.J < len) return Painter.Fur(p.J == len - 1 ? white : navy, p, 52, 0.07);
            return Col.Clear;
        }
        if (b == "leg") {
            int boots = baby ? 1 : 3;
            if (p.Face == "down" || p.J >= p.Cube.H - boots) return Painter.Lit(boot, p);
            return Painter.Fur(navy, p, 53, 0.06);
        }
        return Col.Clear;
    }

    // ---- shiny-only animals ------------------------------------------------------------------

    static Color PaintRabbit(Px p) {
        Color fur = H("#f3e6a8"), belly = H("#fffaf0"), inner = H("#f2a8c8"), eye = H("#c0103a"), nose = H("#e87a9a");
        if (p.Is("head") && p.Face == "north") {
            if (p.J == 1 && (p.I == 0 || p.I == 4)) return eye;
            if (p.J == 3 && p.I == 2) return nose;
            if (p.J == 4 && (p.I == 1 || p.I == 3)) return belly;
        }
        if (p.Is("head") && p.Side && p.J == 1 && p.I == 1) return eye;
        if (p.Is("ear") && p.Face == "north") return p.I == 0 || p.J == 0 ? fur : inner;
        if (p.Is("tail")) return Painter.Fur(belly, p, 61, 0.08);
        if (p.Is("body") && p.Face == "down") return Painter.Fur(belly, p, 61, 0.08);
        if (Spot(p, 62, 3.5, 0.66)) fur = H("#e9cf7a");
        return Painter.Fur(fur, p, 61, 0.14);
    }

    static Color PaintLlama(Px p) {
        Color wool = H("#f4bfd6"), face = H("#fff4f8"), dark = H("#3a2230"), hoof = H("#e2b13c");
        if (p.Is("chest")) {
            Color wood = H("#9a6a3a");
            bool edge = p.I == 0 || p.J == 0 || p.I == p.FW - 1 || p.J == p.FH - 1;
            if (p.Face == "east" || p.Face == "west" || p.Face == "north" || p.Face == "south") {
                if (p.J == p.FH / 2 - 1 && p.I >= p.FW / 2 - 1 && p.I <= p.FW / 2) return H("#cfcfcf"); // latch
                if (p.J == 2) return Col.Shade(wood, 0.7);
            }
            return Painter.Lit(edge ? Col.Shade(wood, 0.78) : (Noise.Grain(p, 70) > 0.5 ? Col.Shade(wood, 0.92) : wood), p);
        }
        if (p.Is("snout")) {
            if (p.Face == "north" && p.J >= 2 && (p.I == 0 || p.I == 3) && p.J == 2) return dark;
            if (p.Face == "north" && p.J == 3 && p.I >= 1 && p.I <= 2) return Col.Shade(face, 0.85);
            return Painter.Fur(face, p, 71, 0.08);
        }
        if (p.Is("head")) {
            if (p.Face == "north" && p.J == 1 && (p.I == 1 || p.I == 6)) return dark; // eyes
            if (p.J < 4) return Painter.Fur(face, p, 71, 0.1);
        }
        if (p.Is("ear")) return Painter.Fur(face, p, 71, 0.08);
        if (p.Is("leg") && (p.Face == "down" || p.J >= 12)) return Painter.Lit(hoof, p);
        if (Spot(p, 72, 4, 0.64)) wool = H("#ffffff");
        return Painter.Fur(wool, p, 72, 0.16);
    }

    static Color PaintDolphin(Px p) {
        Color top = H("#e590ad"), belly = H("#fbd9e4"), eye = H("#2a1a22");
        if (p.Is("head") && p.Side && p.J == 3 && p.I == p.FW - 2) return eye;
        if (p.Is("head") && p.Side && p.J == 4 && p.I == p.FW - 2) return H("#ffffff");
        Color c = (p.Face == "down" || (p.Side && p.J >= p.FH - 2)) ? belly : top;
        if (p.Is("nose")) c = Col.Shade(top, 1.08);
        if ((p.Is("fin") || p.Is("tailfin") || p.Is("backfin"))) c = Col.Shade(top, 0.9);
        return Painter.Fur(c, p, 81, 0.1);
    }

    static Color PaintSlime(Px p) {
        Color outer = H("#f6d86a"), inner = H("#e3a930"), eye = H("#4a2e08");
        if (p.Is("outer")) {
            bool edge = (p.I == 0 || p.J == 0 || p.I == p.FW - 1 || p.J == p.FH - 1);
            return Col.Alpha(Col.Shade(outer, edge ? 1.1 : 1.0), edge ? 200 : 150);
        }
        if (p.Is("eye") || p.Is("mouth")) return eye;
        return Painter.Fur(inner, p, 91, 0.12);
    }

    static Color PaintArmadillo(Px p) {
        Color shell = H("#4fa8a6"), band = H("#2f7472"), skin = H("#f0d6c8"), dark = H("#2a1e1a");
        if (p.Is("shell") || p.Is("rolled")) {
            if (p.Face == "down" && p.Is("shell")) return Col.Clear;
            // Plates in bands across the length.
            double t = p.Is("rolled") ? (p.Y + p.Z) : p.Z;
            Color c = ((int)Math.Floor((t + 20) / 2.0)) % 2 == 0 ? shell : Col.Shade(shell, 0.9);
            if (((int)Math.Floor((t + 20) / 2.0)) % 3 == 0 && p.J % 2 == 0) c = band;
            return Painter.Fur(c, p, 101, 0.1);
        }
        if (p.Is("head") && p.Face == "north" && p.J == 1 && (p.I == 0 || p.I == 2)) return dark;
        if (p.Is("ear")) return Painter.Lit(H("#e7b8b0"), p);
        return Painter.Fur(skin, p, 102, 0.1);
    }

    static Color PaintFrog(Px p) {
        Color skin = H("#5fd0c8"), spot = H("#21655f"), belly = H("#e8fff6"), eye = H("#f4c430"), pupil = H("#101010");
        if (p.Is("eye")) {
            if (p.Face == "up" && p.I == 1 && p.J == 1) return pupil;
            return Painter.Lit(p.Face == "down" ? Col.Shade(skin, 0.9) : eye, p);
        }
        if (p.Is("tongue")) return H("#e46a8a");
        if (p.Is("croak")) return Painter.Fur(belly, p, 111, 0.06);
        if (p.Is("hand") || p.Is("foot")) {
            // Webbed toes on the ground plane around the limb.
            double cx = p.Cube.OX + p.Cube.W / 2.0, cz = p.Cube.OZ + p.Cube.D / 2.0 + (p.Is("hand") ? 1.5 : -1.0);
            double dx = p.X - cx, dz = p.Z - cz, r = Math.Sqrt(dx * dx + dz * dz);
            double ang = Math.Atan2(dx, -dz) * 180 / Math.PI; // 0 = forward
            if (r > 4.2 || r < 0.5) return Col.Clear;
            foreach (double a in new[] { -40.0, 0.0, 40.0 })
                if (Math.Abs(ang - a) < 10 && r > 1.2) return Painter.Lit(Col.Shade(skin, 0.85), p);
            if (Math.Abs(ang) < 40 && r < 3.0) return Col.Alpha(Col.Shade(skin, 1.1), 255);
            return Col.Clear;
        }
        if (p.Is("bodytop") || p.Is("headtop")) {
            if (p.Face != "up") return Col.Clear;
            return Spot(p, 112, 1.8, 0.62) ? Painter.Lit(spot, p) : Col.Clear;
        }
        Color c = p.Face == "down" ? belly : skin;
        if (p.Face != "down" && Spot(p, 112, 1.8, 0.62)) c = spot;
        return Painter.Fur(c, p, 113, 0.1);
    }

    // ---- outputs -----------------------------------------------------------------------------

    public static void Run(string rp) {
        string v = "textures/entity/lepton/variants/";
        var cows = new[] {
            new CowStyle { Coat = H("#6b4a32"), Spot = H("#ece6da"), SpotThreshold = 0.6, SpotScale = 5, Blaze = true, Muzzle = H("#e2b8a0"), Horn = H("#d8cfbd"), Hoof = H("#3a2e26"), Udder = H("#e8a6a0"), Seed = 1 },
            new CowStyle { Coat = H("#f0ece4"), Spot = H("#221e1e"), SpotThreshold = 0.52, SpotScale = 6, Muzzle = H("#e6b0a8"), Horn = H("#d8cfbd"), Hoof = H("#2e2a28"), Udder = H("#f0b2ac"), Seed = 2 },
            new CowStyle { Coat = H("#b5652b"), Spot = H("#d98a4c"), SpotThreshold = 0.62, SpotScale = 3, Muzzle = H("#d9a07a"), Horn = H("#efe6d0"), Hoof = H("#4a3424"), Udder = H("#e8a6a0"), Seed = 3 },
            new CowStyle { Coat = H("#24201f"), Spot = H("#f2eee6"), Belt = true, Muzzle = H("#3c3634"), Horn = H("#cfc6b4"), Hoof = H("#1a1616"), Udder = H("#d89a96"), Seed = 4 },
        };
        for (int i = 0; i < cows.Length; i++) { var s = cows[i]; Emit(rp, v + "cow" + (i + 1) + ".png", 64, 64, CowGeo, p => PaintCow(p, s)); }
        var shinyCow = new CowStyle { Coat = H("#c9b8f0"), Spot = H("#fff3c4"), SpotThreshold = 0.58, SpotScale = 4, Muzzle = H("#f2c4dc"), Horn = H("#ffd34d"), Hoof = H("#6a4fb0"), Udder = H("#f6b8d8"), Seed = 5 };
        Emit(rp, v + "cow_shiny.png", 64, 64, CowGeo, p => PaintCow(p, shinyCow));

        var pigs = new[] {
            new PigStyle { Skin = H("#eba7a2"), Spot = H("#3b2a2a"), SpotThreshold = 0.62, Snout = H("#f4c0bc"), Hoof = H("#8a5a56"), Seed = 11 },
            new PigStyle { Skin = H("#2b2525"), Spot = H("#ecb8b2"), Belt = true, Snout = H("#4a3c3c"), Hoof = H("#1a1616"), Seed = 12 },
            new PigStyle { Skin = H("#c46a3a"), Spot = H("#a85628"), SpotThreshold = 0.58, SpotScale = 2, Snout = H("#e09a78"), Hoof = H("#4a3022"), Seed = 13 },
        };
        for (int i = 0; i < pigs.Length; i++) { var s = pigs[i]; Emit(rp, v + "pig" + (i + 1) + ".png", 64, 64, PigGeo, p => PaintPig(p, s)); }
        var shinyPig = new PigStyle { Skin = H("#a8f0d0"), Spot = H("#ffffff"), SpotThreshold = 0.6, Snout = H("#d4fff0"), Hoof = H("#3f9c7c"), Seed = 14 };
        Emit(rp, v + "pig_shiny.png", 64, 64, PigGeo, p => PaintPig(p, shinyPig));

        Color comb = H("#d0302a"), beak = H("#e8a320");
        var chickens = new[] {
            new ChickenStyle { Feather = H("#8a5a32"), Fleck = H("#5e3a1e"), FleckThreshold = 0.62, Comb = comb, Beak = beak, Legs = beak, Seed = 21 },
            new ChickenStyle { Feather = H("#22222a"), Fleck = H("#2c4a46"), FleckThreshold = 0.6, Comb = comb, Beak = H("#3a3632"), Legs = H("#4a4642"), Seed = 22 },
            new ChickenStyle { Feather = H("#ece8e0"), Fleck = H("#2a2626"), FleckThreshold = 0.68, FleckScale = 1.1, Comb = comb, Beak = beak, Legs = beak, Seed = 23 },
            new ChickenStyle { Feather = H("#d9a24e"), Fleck = H("#e9bf72"), FleckThreshold = 0.6, Comb = comb, Beak = beak, Legs = beak, Seed = 24 },
            new ChickenStyle { Feather = H("#6f7f95"), Fleck = H("#4a5668"), FleckThreshold = 0.6, Comb = comb, Beak = H("#c8b8a0"), Legs = H("#7a8a96"), Seed = 25 },
            new ChickenStyle { Feather = H("#8e3a22"), Fleck = H("#5e2416"), FleckThreshold = 0.62, Comb = comb, Beak = beak, Legs = beak, Seed = 26 },
        };
        for (int i = 0; i < chickens.Length; i++) { var s = chickens[i]; Emit(rp, v + "chicken" + (i + 1) + ".png", 64, 32, ChickenGeo, p => PaintChicken(p, s)); }
        var shinyChicken = new ChickenStyle { Feather = H("#f6c6ea"), Fleck = H("#ffd34d"), FleckThreshold = 0.6, Comb = H("#8a3ad0"), Beak = H("#ffd34d"), Legs = H("#ffd34d"), Seed = 27 };
        Emit(rp, v + "chicken_shiny.png", 64, 32, ChickenGeo, p => PaintChicken(p, shinyChicken));

        double[] allayHues = { 340, 100, 42, 275, -1 };
        for (int i = 0; i < allayHues.Length; i++) {
            double h = allayHues[i];
            Color body = h < 0 ? H("#d6dbe2") : Hsl(h, 0.75, 0.62), core = h < 0 ? H("#ffffff") : Hsl(h, 0.9, 0.85);
            Emit(rp, v + "allay" + (i + 1) + ".png", 32, 32, AllayGeo, p => PaintAllay(p, body, core, H("#1c2236")));
        }
        Emit(rp, v + "allay_shiny.png", 32, 32, AllayGeo, p => PaintAllay(p, H("#2a2f4a"), H("#ffd34d"), H("#ffd34d")));

        Emit(rp, v + "rabbit_shiny.png", 64, 64, RabbitGeo, PaintRabbit);
        Emit(rp, v + "llama_shiny.png", 128, 64, LlamaGeo, PaintLlama);
        Emit(rp, v + "dolphin_shiny.png", 64, 64, DolphinGeo, PaintDolphin);
        Emit(rp, v + "slime_shiny.png", 64, 32, SlimeGeo, PaintSlime);
        Emit(rp, v + "armadillo_shiny.png", 64, 64, ArmadilloGeo, PaintArmadillo);
        Emit(rp, v + "frog_shiny.png", 48, 48, FrogGeo, PaintFrog);

        foreach (var kv in BeeFlags) {
            string name = kv.Key;
            foreach (var st in new[] { "normal", "nectar", "angry", "angry_nectar" }) {
                bool angry = st.StartsWith("angry"), nectar = st.EndsWith("nectar");
                Emit(rp, "textures/entity/lepton/bees/" + name + "/" + st + ".png", 64, 64, BeeGeo, p => PaintBee(p, name, angry, nectar));
            }
        }

        string vb = "textures/entity/lepton/villager/", zb = "textures/entity/lepton/zombie_villager/";
        Emit(rp, vb + "biome_beach.png", 64, 64, VillagerGeo, p => Beach(p, false, false));
        Emit(rp, vb + "biome_ocean.png", 64, 64, VillagerGeo, p => Ocean(p, false, false));
        Emit(rp, vb + "biome_beach_baby.png", 64, 64, BabyVillagerGeo, p => Beach(p, true, false));
        Emit(rp, vb + "biome_ocean_baby.png", 64, 64, BabyVillagerGeo, p => Ocean(p, true, false));
        Emit(rp, zb + "biome_beach.png", 64, 64, ZombieVillagerGeo, p => Beach(p, false, true));
        Emit(rp, zb + "biome_ocean.png", 64, 64, ZombieVillagerGeo, p => Ocean(p, false, true));
        Emit(rp, zb + "biome_beach_baby.png", 64, 64, BabyZombieVillagerGeo, p => Beach(p, true, true));
        Emit(rp, zb + "biome_ocean_baby.png", 64, 64, BabyZombieVillagerGeo, p => Ocean(p, true, true));
    }
}
}
