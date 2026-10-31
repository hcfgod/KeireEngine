using System;

namespace PlanetaryExplorer
{
    public readonly struct PlanetFlightSample
    {
        public readonly string Name;
        public readonly double CenterX;
        public readonly double CenterY;
        public readonly double Radius;
        public readonly double Altitude;
        public readonly double UpX;
        public readonly double UpY;
        public readonly double UpZ;

        public PlanetFlightSample(string name, double centerX, double centerY, double radius,
            double x, double y, double z)
        {
            Name = name;
            CenterX = centerX;
            CenterY = centerY;
            Radius = radius;
            x -= centerX;
            y -= centerY;
            double length = Math.Sqrt(x * x + y * y + z * z);
            Altitude = length - radius;
            UpX = length > 0.001 ? x / length : 0.0;
            UpY = length > 0.001 ? y / length : 1.0;
            UpZ = length > 0.001 ? z / length : 0.0;
        }
    }

    public static class PlanetaryFlightGeometry
    {
        public static float CameraNearPlane(double altitude)
        {
            if (!double.IsFinite(altitude))
            {
                throw new ArgumentOutOfRangeException(nameof(altitude));
            }

            double height = Math.Clamp((altitude - 12.0) / 5000.0, 0.0, 1.0);
            double smoothHeight = height * height * (3.0 - 2.0 * height);
            return (float)(0.2 + 9.8 * smoothHeight);
        }

        public static PlanetFlightSample Nearest(double x, double y, double z)
        {
            if (!double.IsFinite(x) || !double.IsFinite(y) || !double.IsFinite(z) ||
                Math.Abs(x) > 1e100 || Math.Abs(y) > 1e100 || Math.Abs(z) > 1e100)
            {
                throw new ArgumentOutOfRangeException(nameof(x), "Flight coordinates must be finite and within the demo range.");
            }
            var earth = new PlanetFlightSample("Earth", 0.0, -6360000.0, 6360000.0, x, y, z);
            var mars = new PlanetFlightSample("Mars", 40000000.0, -3389500.0, 3389500.0, x, y, z);
            // A point inside a planet must be corrected against that planet, not the other surface.
            if (earth.Altitude < 0.0)
            {
                return earth;
            }
            if (mars.Altitude < 0.0)
            {
                return mars;
            }
            return earth.Altitude <= mars.Altitude ? earth : mars;
        }
    }
}
