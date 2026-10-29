// Box-UV entity texture painter for U18 cosmetics (loaded by tools/textures/cosmetics.ps1).
// Every texture is computed from the cube layout of the vanilla geometry (sizes and UV origins only)
// and procedural rules (colours, seeded noise, simple shapes). Nothing is traced from game art.
using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Imaging;

namespace LeptonTex {

public class Cube {
    public string Bone;
    public int Index;              // index inside its bone (0 = first cube)
    public double OX, OY, OZ;      // origin (model pixels)
    public int W, H, D;            // size
    public int U, V;               // box UV origin
    public Cube(string bone, int index, double ox, double oy, double oz, int w, int h, int d, int u, int v) {
        Bone = bone; Index = index; OX = ox; OY = oy; OZ = oz; W = w; H = h; D = d; U = u; V = v;
    }
}

/// One texel of one cube face.
public class Px {
    public Cube Cube;
    public string Face;            // up, down, north (front), south (back), east, west
    public int I, J, FW, FH;       // texel inside the face (I left->right as seen from outside, J top->bottom)
    public double X, Y, Z;         // model-space centre of the texel
    public double FX { get { return (I + 0.5) / FW; } }
    public double FY { get { return (J + 0.5) / FH; } }
    public bool Is(string bone) { return Cube.Bone == bone; }
    public bool Is(string bone, int index) { return Cube.Bone == bone && Cube.Index == index; }
    public bool Side { get { return Face == "east" || Face == "west"; } }
}

public delegate Color PaintFn(Px p);

public static class Col {
    public static Color Hex(string s) {
        s = s.TrimStart('#');
        if (s.Length == 6) s = "FF" + s;
        return Color.FromArgb(Convert.ToInt32(s, 16));
    }
    static int Cl(double v) { return (int)Math.Max(0, Math.Min(255, Math.Round(v))); }
    public static Color Mix(Color a, Color b, double t) {
        t = Math.Max(0, Math.Min(1, t));
        return Color.FromArgb(Cl(a.A + (b.A - a.A) * t), Cl(a.R + (b.R - a.R) * t), Cl(a.G + (b.G - a.G) * t), Cl(a.B + (b.B - a.B) * t));
    }
    public static Color Shade(Color c, double f) {
        if (f <= 1) return Color.FromArgb(c.A, Cl(c.R * f), Cl(c.G * f), Cl(c.B * f));
        double t = f - 1;
        return Color.FromArgb(c.A, Cl(c.R + (255 - c.R) * t), Cl(c.G + (255 - c.G) * t), Cl(c.B + (255 - c.B) * t));
    }
    public static Color Alpha(Color c, int a) { return Color.FromArgb(a, c.R, c.G, c.B); }
    public static readonly Color Clear = Color.FromArgb(0, 0, 0, 0);
}

public static class Noise {
    static double Hash(int x, int y, int z, int seed) {
        unchecked {
            uint h = (uint)(x * 374761393 + y * 668265263 + z * 1274126177 + seed * 1442695041);
            h = (h ^ (h >> 13)) * 1103515245u;
            h ^= h >> 16;
            return (h & 0xFFFFFF) / 16777215.0;
        }
    }
    static double S(double t) { return t * t * (3 - 2 * t); }
    public static double Value(double x, double y, double z, int seed) {
        int x0 = (int)Math.Floor(x), y0 = (int)Math.Floor(y), z0 = (int)Math.Floor(z);
        double fx = S(x - x0), fy = S(y - y0), fz = S(z - z0);
        double r = 0;
        for (int dz = 0; dz < 2; dz++)
            for (int dy = 0; dy < 2; dy++)
                for (int dx = 0; dx < 2; dx++) {
                    double w = (dx == 1 ? fx : 1 - fx) * (dy == 1 ? fy : 1 - fy) * (dz == 1 ? fz : 1 - fz);
                    r += w * Hash(x0 + dx, y0 + dy, z0 + dz, seed);
                }
        return r;
    }
    public static double Fbm(double x, double y, double z, int seed, double scale, int oct) {
        double sum = 0, amp = 1, norm = 0, f = 1 / scale;
        for (int o = 0; o < oct; o++) {
            sum += amp * Value(x * f, y * f, z * f, seed + o * 31);
            norm += amp; amp *= 0.5; f *= 2;
        }
        return sum / norm;
    }
    /// Per-texel jitter in -1..1 (texture grain).
    public static double Grain(Px p, int seed) { return Hash((int)Math.Floor(p.X * 7 + 1000), (int)Math.Floor(p.Y * 7 + 1000), (int)Math.Floor(p.Z * 7 + 1000) + p.Face.Length * 977, seed) * 2 - 1; }
}

public static class Painter {
    public static Bitmap New(int w, int h) {
        var b = new Bitmap(w, h, PixelFormat.Format32bppArgb);
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++) b.SetPixel(x, y, Color.FromArgb(0, 0, 0, 0));
        return b;
    }

    /// Paints every face of every cube with `fn` (later cubes overwrite shared UV areas).
    public static void Paint(Bitmap bmp, IList<Cube> cubes, PaintFn fn) {
        foreach (var c in cubes) {
            int w = c.W, h = c.H, d = c.D, u = c.U, v = c.V;
            Face(bmp, c, fn, "up", u + d, v, w, d);
            Face(bmp, c, fn, "down", u + d + w, v, w, d);
            Face(bmp, c, fn, "east", u, v + d, d, h);
            Face(bmp, c, fn, "north", u + d, v + d, w, h);
            Face(bmp, c, fn, "west", u + d + w, v + d, d, h);
            Face(bmp, c, fn, "south", u + 2 * d + w, v + d, w, h);
        }
    }

    static void Face(Bitmap bmp, Cube c, PaintFn fn, string face, int x0, int y0, int fw, int fh) {
        if (fw <= 0 || fh <= 0) return;
        for (int j = 0; j < fh; j++)
            for (int i = 0; i < fw; i++) {
                int x = x0 + i, y = y0 + j;
                if (x < 0 || y < 0 || x >= bmp.Width || y >= bmp.Height) continue;
                var p = new Px { Cube = c, Face = face, I = i, J = j, FW = fw, FH = fh };
                double fx = i + 0.5, fy = j + 0.5;
                switch (face) {
                case "north": p.X = c.OX + c.W - fx; p.Y = c.OY + c.H - fy; p.Z = c.OZ; break;
                case "south": p.X = c.OX + fx; p.Y = c.OY + c.H - fy; p.Z = c.OZ + c.D; break;
                case "east": p.X = c.OX + c.W; p.Y = c.OY + c.H - fy; p.Z = c.OZ + fx; break;
                case "west": p.X = c.OX; p.Y = c.OY + c.H - fy; p.Z = c.OZ + c.D - fx; break;
                case "up": p.X = c.OX + c.W - fx; p.Y = c.OY + c.H; p.Z = c.OZ + fy; break;
                default: p.X = c.OX + c.W - fx; p.Y = c.OY; p.Z = c.OZ + fy; break;
                }
                bmp.SetPixel(x, y, fn(p));
            }
    }

    /// Simple face lighting so flat colours read as volumes.
    public static Color Lit(Color c, Px p) {
        switch (p.Face) {
        case "up": return Col.Shade(c, 1.08);
        case "down": return Col.Shade(c, 0.80);
        case "east": case "west": return Col.Shade(c, 0.93);
        default: return c;
        }
    }

    /// Base colour with soft noise variation and grain (fur / feathers / skin).
    public static Color Fur(Color c, Px p, int seed, double amount) {
        double n = Noise.Fbm(p.X + 100, p.Y + 100, p.Z + 100, seed, 3.0, 2) - 0.5;
        double g = Noise.Grain(p, seed + 7) * 0.5;
        return Lit(Col.Shade(c, 1 + (n * 0.6 + g * 0.4) * amount), p);
    }

    public static void Save(Bitmap b, string path) {
        var dir = System.IO.Path.GetDirectoryName(path);
        if (!string.IsNullOrEmpty(dir)) System.IO.Directory.CreateDirectory(dir);
        b.Save(path, ImageFormat.Png);
    }
}
}
