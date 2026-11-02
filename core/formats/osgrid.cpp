// osgrid.cpp - see osgrid.h.
#include "formats/osgrid.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

namespace meshdeck {
namespace osgrid {
namespace {

const double kPi = 3.14159265358979323846, kDeg = kPi / 180.0;

// Airy 1830 (OSGB36) and WGS84 ellipsoids.
const double kAiryA = 6377563.396, kAiryB = 6356256.909;
const double kWgsA = 6378137.0, kWgsB = 6356752.314245;

// National Grid projection constants.
const double kF0 = 0.9996012717, kLat0 = 49.0 * kDeg, kLon0 = -2.0 * kDeg, kE0 = 400000, kN0 = -100000;

struct Helmert { double tx, ty, tz, s, rx, ry, rz; };   // metres, ppm, arc-seconds
const Helmert kToOsgb = {-446.448, 125.157, -542.060, 20.4894, -0.1502, -0.2470, -0.8421};
const Helmert kToWgs = {446.448, -125.157, 542.060, -20.4894, 0.1502, 0.2470, 0.8421};

void toCartesian(double lat, double lon, double a, double b, double& x, double& y, double& z) {
    double e2 = 1 - (b * b) / (a * a);
    double nu = a / sqrt(1 - e2 * sin(lat) * sin(lat));
    x = nu * cos(lat) * cos(lon);
    y = nu * cos(lat) * sin(lon);
    z = (1 - e2) * nu * sin(lat);
}

void fromCartesian(double x, double y, double z, double a, double b, double& lat, double& lon) {
    double e2 = 1 - (b * b) / (a * a);
    double p = sqrt(x * x + y * y);
    lat = atan2(z, p * (1 - e2));
    for (int i = 0; i < 10; i++) {
        double nu = a / sqrt(1 - e2 * sin(lat) * sin(lat));
        double next = atan2(z + e2 * nu * sin(lat), p);
        if (fabs(next - lat) < 1e-12) { lat = next; break; }
        lat = next;
    }
    lon = atan2(y, x);
}

void transform(const Helmert& h, double& x, double& y, double& z) {
    double s = h.s * 1e-6, sec = kDeg / 3600.0;
    double rx = h.rx * sec, ry = h.ry * sec, rz = h.rz * sec;
    double x2 = h.tx + (1 + s) * x - rz * y + ry * z;
    double y2 = h.ty + rz * x + (1 + s) * y - rx * z;
    double z2 = h.tz - ry * x + rx * y + (1 + s) * z;
    x = x2; y = y2; z = z2;
}

double meridional(double lat) {
    double n = (kAiryA - kAiryB) / (kAiryA + kAiryB), n2 = n * n, n3 = n2 * n;
    double dl = lat - kLat0, sl = lat + kLat0;
    return kAiryB * kF0 * ((1 + n + 1.25 * n2 + 1.25 * n3) * dl - (3 * n + 3 * n2 + 2.625 * n3) * sin(dl) * cos(sl) +
                           (1.875 * n2 + 1.875 * n3) * sin(2 * dl) * cos(2 * sl) - (35.0 / 24.0) * n3 * sin(3 * dl) * cos(3 * sl));
}

} // namespace

void projectOsgb36(double latDeg, double lonDeg, double& e, double& n) {
    double lat = latDeg * kDeg, lon = lonDeg * kDeg;
    double a = kAiryA, b = kAiryB, e2 = 1 - (b * b) / (a * a);
    double s = sin(lat), c = cos(lat), t = tan(lat);
    double nu = a * kF0 / sqrt(1 - e2 * s * s);
    double rho = a * kF0 * (1 - e2) / pow(1 - e2 * s * s, 1.5);
    double eta2 = nu / rho - 1;
    double I = meridional(lat) + kN0;
    double II = nu / 2 * s * c;
    double III = nu / 24 * s * c * c * c * (5 - t * t + 9 * eta2);
    double IIIA = nu / 720 * s * pow(c, 5) * (61 - 58 * t * t + pow(t, 4));
    double IV = nu * c;
    double V = nu / 6 * c * c * c * (nu / rho - t * t);
    double VI = nu / 120 * pow(c, 5) * (5 - 18 * t * t + pow(t, 4) + 14 * eta2 - 58 * t * t * eta2);
    double dl = lon - kLon0;
    n = I + II * dl * dl + III * pow(dl, 4) + IIIA * pow(dl, 6);
    e = kE0 + IV * dl + V * pow(dl, 3) + VI * pow(dl, 5);
}

bool toGrid(double lat, double lon, double& e, double& n) {
    double x, y, z, la, lo;
    toCartesian(lat * kDeg, lon * kDeg, kWgsA, kWgsB, x, y, z);
    transform(kToOsgb, x, y, z);
    fromCartesian(x, y, z, kAiryA, kAiryB, la, lo);
    projectOsgb36(la / kDeg, lo / kDeg, e, n);
    return e >= 0 && e < 700000 && n >= 0 && n < 1300000;
}

bool toLatLon(double e, double n, double& lat, double& lon) {
    if (!(e >= 0 && e < 700000 && n >= 0 && n < 1300000)) return false;
    double a = kAiryA, b = kAiryB, e2 = 1 - (b * b) / (a * a);
    double phi = kLat0, m = 0;
    do {
        phi = (n - kN0 - m) / (a * kF0) + phi;
        m = meridional(phi);
    } while (fabs(n - kN0 - m) >= 0.00001);
    double s = sin(phi), c = cos(phi), t = tan(phi), sec = 1 / c;
    double nu = a * kF0 / sqrt(1 - e2 * s * s);
    double rho = a * kF0 * (1 - e2) / pow(1 - e2 * s * s, 1.5);
    double eta2 = nu / rho - 1;
    double VII = t / (2 * rho * nu);
    double VIII = t / (24 * rho * pow(nu, 3)) * (5 + 3 * t * t + eta2 - 9 * t * t * eta2);
    double IX = t / (720 * rho * pow(nu, 5)) * (61 + 90 * t * t + 45 * pow(t, 4));
    double X = sec / nu;
    double XI = sec / (6 * pow(nu, 3)) * (nu / rho + 2 * t * t);
    double XII = sec / (120 * pow(nu, 5)) * (5 + 28 * t * t + 24 * pow(t, 4));
    double XIIA = sec / (5040 * pow(nu, 7)) * (61 + 662 * t * t + 1320 * pow(t, 4) + 720 * pow(t, 6));
    double de = e - kE0;
    double la = phi - VII * de * de + VIII * pow(de, 4) - IX * pow(de, 6);
    double lo = kLon0 + X * de - XI * pow(de, 3) + XII * pow(de, 5) - XIIA * pow(de, 7);
    double x, y, z;
    toCartesian(la, lo, kAiryA, kAiryB, x, y, z);
    transform(kToWgs, x, y, z);
    fromCartesian(x, y, z, kWgsA, kWgsB, la, lo);
    lat = la / kDeg;
    lon = lo / kDeg;
    return true;
}

std::string format(double e, double n, int digits) {
    if (!(e >= 0 && e < 700000 && n >= 0 && n < 1300000) || digits < 2 || digits > 10 || digits % 2) return "";
    int e100 = (int)(e / 100000), n100 = (int)(n / 100000);
    int l1 = (19 - n100) - (19 - n100) % 5 + (e100 + 10) / 5;
    int l2 = (19 - n100) * 5 % 25 + e100 % 5;
    if (l1 > 7) l1++;   // the grid letters skip I
    if (l2 > 7) l2++;
    int per = digits / 2;
    long div = 1;
    for (int i = per; i < 5; i++) div *= 10;
    long ee = (long)fmod(e, 100000) / div, nn = (long)fmod(n, 100000) / div;
    char buf[24];
    snprintf(buf, sizeof buf, "%c%c %0*ld %0*ld", 'A' + l1, 'A' + l2, per, ee, per, nn);
    return buf;
}

bool parse(const std::string& text, double& e, double& n) {
    std::string letters, digits;
    for (char ch : text) {
        if (isalpha((unsigned char)ch)) { if (!digits.empty()) return false; letters += (char)toupper(ch); }
        else if (isdigit((unsigned char)ch)) digits += ch;
        else if (ch != ' ' && ch != ',') return false;
    }
    if (letters.empty()) {
        // All-number form "EEEEEE NNNNNNN": take the two space-separated numbers as metres.
        double a, b;
        if (sscanf(text.c_str(), "%lf %lf", &a, &b) != 2 && sscanf(text.c_str(), "%lf,%lf", &a, &b) != 2) return false;
        if (!(a >= 0 && a < 700000 && b >= 0 && b < 1300000)) return false;
        e = a; n = b;
        return true;
    }
    if (letters.size() != 2 || digits.size() < 2 || digits.size() > 10 || digits.size() % 2) return false;
    int l1 = letters[0] - 'A', l2 = letters[1] - 'A';
    if (letters[0] == 'I' || letters[1] == 'I' || l1 < 0 || l1 > 25 || l2 < 0 || l2 > 25) return false;
    if (l1 > 7) l1--;
    if (l2 > 7) l2--;
    int e100 = ((l1 - 2) % 5) * 5 + l2 % 5;
    int n100 = (19 - (l1 / 5) * 5) - l2 / 5;
    if (e100 < 0 || e100 > 6 || n100 < 0 || n100 > 12) return false;
    size_t half = digits.size() / 2;
    std::string es = digits.substr(0, half), ns = digits.substr(half);
    while (es.size() < 5) es += '0';
    while (ns.size() < 5) ns += '0';
    e = e100 * 100000.0 + atol(es.c_str());
    n = n100 * 100000.0 + atol(ns.c_str());
    return true;
}

bool parseLatLon(const std::string& text, double& lat, double& lon) {
    double a, b;
    if (sscanf(text.c_str(), "%lf , %lf", &a, &b) != 2 && sscanf(text.c_str(), "%lf %lf", &a, &b) != 2) return false;
    if (a < -90 || a > 90 || b < -180 || b > 180) return false;
    lat = a; lon = b;
    return true;
}

double distanceM(double lat1, double lon1, double lat2, double lon2) {
    double p1 = lat1 * kDeg, p2 = lat2 * kDeg, dp = (lat2 - lat1) * kDeg, dl = (lon2 - lon1) * kDeg;
    double h = sin(dp / 2) * sin(dp / 2) + cos(p1) * cos(p2) * sin(dl / 2) * sin(dl / 2);
    return 6371000.0 * 2 * atan2(sqrt(h), sqrt(1 - h));
}

double bearingDeg(double lat1, double lon1, double lat2, double lon2) {
    double p1 = lat1 * kDeg, p2 = lat2 * kDeg, dl = (lon2 - lon1) * kDeg;
    double y = sin(dl) * cos(p2), x = cos(p1) * sin(p2) - sin(p1) * cos(p2) * cos(dl);
    double b = atan2(y, x) / kDeg;
    return b < 0 ? b + 360 : b;
}

} // namespace osgrid
} // namespace meshdeck
