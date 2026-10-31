using System;
using System.Collections.Generic;
using System.IO;
using System.Text;

namespace PlanetaryExplorer.Authoring
{
    public readonly struct GlobeVertex
    {
        public readonly double X;
        public readonly double Y;
        public readonly double Z;
        public readonly double NormalX;
        public readonly double NormalY;
        public readonly double NormalZ;
        public readonly double U;
        public readonly double V;

        public GlobeVertex(double x, double y, double z, double normalX, double normalY, double normalZ,
            double u, double v)
        {
            X = x;
            Y = y;
            Z = z;
            NormalX = normalX;
            NormalY = normalY;
            NormalZ = normalZ;
            U = u;
            V = v;
        }
    }

    public sealed class GlobeMesh
    {
        public GlobeVertex[] Vertices { get; }
        public int[] Indices { get; }
        public double Radius { get; }
        public double AnchorHeight { get; }

        public GlobeMesh(GlobeVertex[] vertices, int[] indices, double radius, double anchorHeight)
        {
            Vertices = vertices;
            Indices = indices;
            Radius = radius;
            AnchorHeight = anchorHeight;
        }

        public void WriteObj(string path)
        {
            using var writer = new StreamWriter(path, false, new UTF8Encoding(false));
            writer.WriteLine("# Sample globe, metres. Import through Keire.Mesh to apply handedness and UV conversion.");
            foreach (GlobeVertex vertex in Vertices)
            {
                writer.WriteLine(FormattableString.Invariant($"v {vertex.X:G17} {vertex.Y:G17} {vertex.Z:G17}"));
            }
            foreach (GlobeVertex vertex in Vertices)
            {
                writer.WriteLine(FormattableString.Invariant($"vt {vertex.U:G17} {vertex.V:G17}"));
            }
            foreach (GlobeVertex vertex in Vertices)
            {
                writer.WriteLine(FormattableString.Invariant($"vn {vertex.NormalX:G17} {vertex.NormalY:G17} {vertex.NormalZ:G17}"));
            }
            for (int index = 0; index < Indices.Length; index += 3)
            {
                int a = Indices[index] + 1;
                int b = Indices[index + 1] + 1;
                int c = Indices[index + 2] + 1;
                writer.WriteLine(FormattableString.Invariant($"f {a}/{a}/{a} {b}/{b}/{b} {c}/{c}/{c}"));
            }
        }
    }

    public static class PlanetGlobeMeshGenerator
    {
        public static GlobeMesh Create(double radius, double capDegrees, int rings, int longitudes, bool polarCap)
        {
            if (!double.IsFinite(radius) || radius <= 0.0 || !double.IsFinite(capDegrees) ||
                capDegrees <= 0.0 || capDegrees >= 90.0 || rings < 2 || rings > 1024 ||
                longitudes < 8 || longitudes > 1024)
            {
                throw new ArgumentOutOfRangeException(nameof(radius), "Invalid globe dimensions or tessellation.");
            }

            double boundary = capDegrees * Math.PI / 180.0;
            double first = polarCap ? 0.0 : boundary;
            double last = polarCap ? boundary : Math.PI;
            double anchor = polarCap ? radius : 0.0;
            var vertices = new GlobeVertex[(rings + 1) * (longitudes + 1)];
            for (int ring = 0; ring <= rings; ++ring)
            {
                double theta = ring == rings ? last : first + (last - first) * ring / rings;
                double sine = Math.Sin(theta);
                double cosine = Math.Cos(theta);
                // Compute the small local height in double before mesh vertices become floats.
                double y = polarCap ? -2.0 * radius * Math.Pow(Math.Sin(theta / 2.0), 2.0) : radius * cosine;
                for (int longitude = 0; longitude <= longitudes; ++longitude)
                {
                    double u = (double)longitude / longitudes;
                    double phi = longitude == longitudes ? 0.0 : 2.0 * Math.PI * u;
                    double nx = sine * Math.Cos(phi);
                    // Keire's OBJ import flips handedness and V. Compensate here to preserve built-in globe UVs.
                    double nz = -sine * Math.Sin(phi);
                    vertices[ring * (longitudes + 1) + longitude] =
                        new GlobeVertex(radius * nx, y, radius * nz, nx, cosine, nz, u, 1.0 - theta / Math.PI);
                }
            }

            var indices = new List<int>(rings * longitudes * 6);
            for (int ring = 0; ring < rings; ++ring)
            {
                for (int longitude = 0; longitude < longitudes; ++longitude)
                {
                    int a = ring * (longitudes + 1) + longitude;
                    int b = a + 1;
                    int c = a + longitudes + 1;
                    int d = c + 1;
                    if (!polarCap || ring != 0)
                    {
                        indices.Add(a);
                        indices.Add(c);
                        indices.Add(b);
                    }
                    if (polarCap || ring != rings - 1)
                    {
                        indices.Add(b);
                        indices.Add(c);
                        indices.Add(d);
                    }
                }
            }
            return new GlobeMesh(vertices, indices.ToArray(), radius, anchor);
        }

        public static double Validate(GlobeMesh mesh)
        {
            double maximumSag = 0.0;
            foreach (GlobeVertex vertex in mesh.Vertices)
            {
                double y = vertex.Y + mesh.AnchorHeight;
                double radial = Math.Sqrt(vertex.X * vertex.X + y * y + vertex.Z * vertex.Z);
                if (Math.Abs(radial - mesh.Radius) > 0.00001 || vertex.U < 0.0 || vertex.U > 1.0 ||
                    vertex.V < 0.0 || vertex.V > 1.0)
                {
                    throw new InvalidOperationException("Globe vertex is off the sphere or outside its UV domain.");
                }
            }
            for (int index = 0; index < mesh.Indices.Length; index += 3)
            {
                GlobeVertex a = mesh.Vertices[mesh.Indices[index]];
                GlobeVertex b = mesh.Vertices[mesh.Indices[index + 1]];
                GlobeVertex c = mesh.Vertices[mesh.Indices[index + 2]];
                double abx = b.X - a.X;
                double aby = b.Y - a.Y;
                double abz = b.Z - a.Z;
                double acx = c.X - a.X;
                double acy = c.Y - a.Y;
                double acz = c.Z - a.Z;
                double x = (a.X + b.X + c.X) / 3.0;
                double y = (a.Y + b.Y + c.Y) / 3.0 + mesh.AnchorHeight;
                double z = (a.Z + b.Z + c.Z) / 3.0;
                double winding = (aby * acz - abz * acy) * x + (abz * acx - abx * acz) * y +
                    (abx * acy - aby * acx) * z;
                if (!(winding > 0.0))
                {
                    throw new InvalidOperationException("Globe triangle is degenerate or inward-facing.");
                }
                maximumSag = Math.Max(maximumSag, mesh.Radius - Math.Sqrt(x * x + y * y + z * z));
            }
            return maximumSag;
        }
    }
}
