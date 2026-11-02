// osgrid.h - Ordnance Survey National Grid: WGS84 lat/lon <-> grid metres, grid references,
// distance and bearing. Ported unchanged from MESHDECK 1.x (checked against the OS worked example).
#pragma once
#include <string>

namespace meshdeck {
namespace osgrid {

// Ordnance Survey National Grid conversions, following "A guide to coordinate systems in Great
// Britain" (Ordnance Survey): WGS84 <-> OSGB36 by a 7-parameter Helmert transform (good to about
// 5 m), then the Transverse Mercator projection on the Airy 1830 ellipsoid.

// WGS84 latitude/longitude (degrees) to grid easting/northing (metres). False outside the grid.
bool toGrid(double lat, double lon, double& e, double& n);
// Grid easting/northing back to WGS84 latitude/longitude.
bool toLatLon(double e, double n, double& lat, double& lon);

// "NT 12345 67890" (digits = 2..10 in total, i.e. 1 to 5 per axis). "" outside the grid.
std::string format(double e, double n, int digits = 10);
// Accepts "NT1234567890", "NT 123 456", "nt 12345 67890" and all-number "651409 313177".
// Returns the south-west corner of the square the reference names.
bool parse(const std::string& text, double& e, double& n);
// Accepts "54.12345, -2.34567" or "54.12345 -2.34567" (WGS84 decimal degrees).
bool parseLatLon(const std::string& text, double& lat, double& lon);

double distanceM(double lat1, double lon1, double lat2, double lon2);   // great circle
double bearingDeg(double lat1, double lon1, double lat2, double lon2);  // 0-360, true north

// Projection only, on OSGB36 coordinates (exposed for the OS worked-example test).
void projectOsgb36(double lat, double lon, double& e, double& n);

} // namespace osgrid
} // namespace meshdeck
