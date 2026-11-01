using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;

namespace PlanetaryExplorer.Authoring
{
    public static class SpaceportCapCutout
    {
        private sealed class Vertex
        {
            public double[] Values;

            public Vertex(double[] values)
            {
                Values = values;
            }

            public static Vertex Lerp(Vertex a, Vertex b, double amount)
            {
                return new Vertex(a.Values.Zip(b.Values, (x, y) => x + (y - x) * amount).ToArray());
            }
        }

        private const double HalfWidth = 8000.0;
        private const double TerrainStep = 100.0;

        private static List<Vertex> Clip(List<Vertex> polygon, int axis, double limit, bool less)
        {
            var result = new List<Vertex>();
            if (polygon.Count == 0)
            {
                return result;
            }
            Vertex previous = polygon[polygon.Count - 1];
            bool previousInside = less ? previous.Values[axis] <= limit : previous.Values[axis] >= limit;
            foreach (Vertex current in polygon)
            {
                bool currentInside = less ? current.Values[axis] <= limit : current.Values[axis] >= limit;
                if (previousInside != currentInside)
                {
                    double amount = (limit - previous.Values[axis]) / (current.Values[axis] - previous.Values[axis]);
                    Vertex crossing = Vertex.Lerp(previous, current, amount);
                    crossing.Values[axis] = limit;
                    result.Add(crossing);
                }
                if (currentInside)
                {
                    result.Add(current);
                }
                previous = current;
                previousInside = currentInside;
            }
            return result;
        }

        private static double AreaSquared(Vertex a, Vertex b, Vertex c)
        {
            double x = b.Values[0] - a.Values[0];
            double y = b.Values[1] - a.Values[1];
            double z = b.Values[2] - a.Values[2];
            double u = c.Values[0] - a.Values[0];
            double v = c.Values[1] - a.Values[1];
            double w = c.Values[2] - a.Values[2];
            return Math.Pow(y * w - z * v, 2) + Math.Pow(z * u - x * w, 2) + Math.Pow(x * v - y * u, 2);
        }

        private static void AddPolygon(List<Vertex> polygon, List<Vertex> output)
        {
            for (int i = 1; i + 1 < polygon.Count; i++)
            {
                if (AreaSquared(polygon[0], polygon[i], polygon[i + 1]) > 1e-12)
                {
                    output.Add(polygon[0]);
                    output.Add(polygon[i]);
                    output.Add(polygon[i + 1]);
                }
            }
        }

        private static double TerrainEdgeHeight(double coordinate)
        {
            double low = Math.Floor((coordinate + HalfWidth) / TerrainStep) * TerrainStep - HalfWidth;
            double high = Math.Min(HalfWidth, low + TerrainStep);
            double fraction = high == low ? 0.0 : (coordinate - low) / (high - low);
            double lowHeight = 0.5 - (HalfWidth * HalfWidth + low * low) / 12720000.0;
            double highHeight = 0.5 - (HalfWidth * HalfWidth + high * high) / 12720000.0;
            return lowHeight + (highHeight - lowHeight) * fraction;
        }

        private static double ProjectedArea(Vertex a, Vertex b, Vertex c)
        {
            return Math.Abs((b.Values[0] - a.Values[0]) * (c.Values[2] - a.Values[2]) -
                (b.Values[2] - a.Values[2]) * (c.Values[0] - a.Values[0])) * 0.5;
        }

        public static string Generate(string source, string destination, string terrainPath)
        {
            var positions = new List<double[]>();
            var normals = new List<double[]>();
            var uvs = new List<double[]>();
            var triangles = new List<Vertex>();
            double originalArea = 0.0;
            foreach (string line in File.ReadLines(source))
            {
                string[] fields = line.Split(' ', StringSplitOptions.RemoveEmptyEntries);
                if (fields.Length == 0)
                {
                    continue;
                }
                if (fields[0] == "v" || fields[0] == "vn" || fields[0] == "vt")
                {
                    double[] values = fields.Skip(1).Select(value => double.Parse(value, CultureInfo.InvariantCulture)).ToArray();
                    if (fields[0] == "v")
                    {
                        positions.Add(values);
                    }
                    if (fields[0] == "vn")
                    {
                        normals.Add(values);
                    }
                    if (fields[0] == "vt")
                    {
                        uvs.Add(values);
                    }
                }
                if (fields[0] != "f")
                {
                    continue;
                }
                var remaining = new List<Vertex>();
                foreach (string field in fields.Skip(1))
                {
                    int[] indices = field.Split('/').Select(int.Parse).ToArray();
                    double[] p = positions[indices[0] - 1];
                    double[] uv = uvs[indices[1] - 1];
                    double[] n = normals[indices[2] - 1];
                    remaining.Add(new Vertex(new[] { p[0], p[1], p[2], uv[0], uv[1], n[0], n[1], n[2] }));
                }
                if (remaining.Count != 3)
                {
                    throw new InvalidDataException("Expected triangular authored globe source.");
                }
                originalArea += ProjectedArea(remaining[0], remaining[1], remaining[2]);
                // Partition each triangle into disjoint outside strips. Only the final inside polygon is removed.
                foreach (var plane in new[] { (0, -HalfWidth, true), (0, HalfWidth, false), (2, -HalfWidth, true), (2, HalfWidth, false) })
                {
                    AddPolygon(Clip(remaining, plane.Item1, plane.Item2, plane.Item3), triangles);
                    remaining = Clip(remaining, plane.Item1, plane.Item2, !plane.Item3);
                }
            }
            double retainedArea = 0.0;
            for (int i = 0; i < triangles.Count; i += 3)
            {
                Vertex a = triangles[i];
                Vertex b = triangles[i + 1];
                Vertex c = triangles[i + 2];
                bool outside = false;
                foreach (int axis in new[] { 0, 2 })
                {
                    outside |= a.Values[axis] <= -HalfWidth && b.Values[axis] <= -HalfWidth && c.Values[axis] <= -HalfWidth;
                    outside |= a.Values[axis] >= HalfWidth && b.Values[axis] >= HalfWidth && c.Values[axis] >= HalfWidth;
                }
                if (!outside)
                {
                    throw new InvalidDataException("Cutout left a triangle overlapping the terrain footprint.");
                }
                retainedArea += ProjectedArea(a, b, c);
            }
            double removedArea = originalArea - retainedArea;
            if (Math.Abs(removedArea - 4.0 * HalfWidth * HalfWidth) > 1.0)
            {
                throw new InvalidDataException("Cutout projected area is not conserved.");
            }
            using (var writer = new StreamWriter(destination))
            {
                foreach (Vertex vertex in triangles)
                {
                    double[] p = vertex.Values;
                    writer.WriteLine(FormattableString.Invariant($"v {p[0]:R} {p[1]:R} {p[2]:R}"));
                }
                foreach (Vertex vertex in triangles)
                {
                    writer.WriteLine(FormattableString.Invariant($"vt {vertex.Values[3]:R} {vertex.Values[4]:R}"));
                }
                foreach (Vertex vertex in triangles)
                {
                    double[] p = vertex.Values;
                    double length = Math.Sqrt(p[5] * p[5] + p[6] * p[6] + p[7] * p[7]);
                    writer.WriteLine(FormattableString.Invariant($"vn {p[5] / length:R} {p[6] / length:R} {p[7] / length:R}"));
                }
                for (int i = 1; i <= triangles.Count; i += 3)
                {
                    writer.WriteLine($"f {i}/{i}/{i} {i + 1}/{i + 1}/{i + 1} {i + 2}/{i + 2}/{i + 2}");
                }
            }
            int terrainVertices = File.ReadLines(terrainPath).Count(line => line.StartsWith("v "));
            int skirtTriangles = 0;
            double boundaryLength = 0.0;
            using (var writer = new StreamWriter(terrainPath, true))
            {
                for (int i = 0; i < triangles.Count; i += 3)
                {
                    for (int edge = 0; edge < 3; edge++)
                    {
                        Vertex a = triangles[i + edge];
                        Vertex b = triangles[i + (edge + 1) % 3];
                        foreach (int axis in new[] { 0, 2 })
                        {
                            int varying = axis == 0 ? 2 : 0;
                            if (Math.Abs(Math.Abs(a.Values[axis]) - HalfWidth) > 1e-8 ||
                                Math.Abs(a.Values[axis] - b.Values[axis]) > 1e-8 ||
                                Math.Abs(a.Values[varying]) > HalfWidth + 1e-8 || Math.Abs(b.Values[varying]) > HalfWidth + 1e-8 ||
                                Math.Abs(a.Values[varying] - b.Values[varying]) < 1e-8)
                            {
                                continue;
                            }
                            boundaryLength += Math.Abs(a.Values[varying] - b.Values[varying]);
                            var cuts = new List<double> { 0.0, 1.0 };
                            for (double grid = -HalfWidth; grid <= HalfWidth; grid += TerrainStep)
                            {
                                double t = (grid - a.Values[varying]) / (b.Values[varying] - a.Values[varying]);
                                if (t > 1e-9 && t < 1.0 - 1e-9)
                                {
                                    cuts.Add(t);
                                }
                            }
                            cuts.Sort();
                            for (int cut = 0; cut + 1 < cuts.Count; cut++)
                            {
                                Vertex bottomA = Vertex.Lerp(a, b, cuts[cut]);
                                Vertex bottomB = Vertex.Lerp(a, b, cuts[cut + 1]);
                                bottomA.Values[axis] = Math.CopySign(HalfWidth, a.Values[axis]);
                                bottomB.Values[axis] = bottomA.Values[axis];
                                Vertex topA = new Vertex((double[])bottomA.Values.Clone());
                                Vertex topB = new Vertex((double[])bottomB.Values.Clone());
                                topA.Values[1] = TerrainEdgeHeight(topA.Values[varying]);
                                topB.Values[1] = TerrainEdgeHeight(topB.Values[varying]);
                                foreach (Vertex vertex in new[] { bottomA, bottomB, topB, topA })
                                {
                                    writer.WriteLine(FormattableString.Invariant($"v {vertex.Values[0]:R} {vertex.Values[1]:R} {vertex.Values[2]:R}"));
                                }
                                double edgeX = bottomA.Values[0] - bottomB.Values[0];
                                double edgeZ = bottomA.Values[2] - bottomB.Values[2];
                                double rise = topA.Values[1] - bottomA.Values[1];
                                double outward = axis == 0 ? -edgeZ * rise : edgeX * rise;
                                if (rise <= 0.0 || outward * Math.Sign(bottomA.Values[axis]) <= 0.0)
                                {
                                    throw new InvalidDataException("Stitched terrain edge is inverted or below the globe cap.");
                                }
                                int first = terrainVertices + 1;
                                // The cap edge runs opposite the hole boundary; reverse the shared bottom edge.
                                writer.WriteLine($"f {first + 1} {first} {first + 3}");
                                writer.WriteLine($"f {first + 1} {first + 3} {first + 2}");
                                terrainVertices += 4;
                                skirtTriangles += 2;
                            }
                        }
                    }
                }
            }
            if (Math.Abs(boundaryLength - 8.0 * HalfWidth) > 0.001)
            {
                throw new InvalidDataException($"Cutout boundary coverage has a gap or duplicate segment: {boundaryLength:R}.");
            }
            return $"Cutout: {triangles.Count / 3} cap triangles; {skirtTriangles} stitched edge triangles.";
        }
    }
}
