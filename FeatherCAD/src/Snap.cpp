// Snap.cpp - see Snap.h.
#include "Snap.h"

#include <algorithm>

const wchar_t* SnapName(unsigned mode) {
    switch (mode) {
        case kSnapEndpoint: return L"Endpoint";
        case kSnapMidpoint: return L"Midpoint";
        case kSnapCentre: return L"Centre";
        case kSnapQuadrant: return L"Quadrant";
        case kSnapIntersection: return L"Intersection";
        case kSnapPerpendicular: return L"Perpendicular";
        case kSnapTangent: return L"Tangent";
        case kSnapNearest: return L"Nearest";
    }
    return L"";
}

const wchar_t* SnapKeyword(unsigned mode) {
    switch (mode) {
        case kSnapEndpoint: return L"END";
        case kSnapMidpoint: return L"MID";
        case kSnapCentre: return L"CEN";
        case kSnapQuadrant: return L"QUA";
        case kSnapIntersection: return L"INT";
        case kSnapPerpendicular: return L"PER";
        case kSnapTangent: return L"TAN";
        case kSnapNearest: return L"NEA";
    }
    return L"";
}

namespace {
// Lower ranks win ties: an end point beats the midpoint at the same place.
int Rank(unsigned mode) {
    switch (mode) {
        case kSnapEndpoint: return 0;
        case kSnapIntersection: return 1;
        case kSnapCentre: return 2;
        case kSnapQuadrant: return 3;
        case kSnapMidpoint: return 4;
        case kSnapPerpendicular: return 5;
        case kSnapTangent: return 6;
        default: return 7;
    }
}

struct Best {
    SnapResult result;
    double score = std::numeric_limits<double>::infinity();
    void Offer(unsigned mode, Vec2 p, uint64_t id, double dist, double penalty, double aperture) {
        if (dist > aperture) return;
        const double score_ = dist + penalty + Rank(mode) * aperture * 0.01;
        if (score_ < score) {
            score = score_;
            result = {true, mode, p, id};
        }
    }
};

struct NearPrim {
    Prim prim;
    uint64_t id;
};
}  // namespace

SnapResult FindSnap(const Document& doc, const SnapQuery& q) {
    Best best;
    if (!q.modes) return best.result;
    const double ap = q.aperture;
    const Box search = Box(q.cursor, q.cursor).Inflated(ap);
    std::vector<NearPrim> near;
    std::vector<Prim> prims;
    std::vector<Vec2> pts;

    for (const auto& kv : doc.Entities()) {
        const Entity& e = kv.second;
        const uint64_t id = kv.first;
        const Box bounds = Bounds(e);
        const bool curveNear = bounds.Intersects(search);
        // Centres: also found when the cursor is on the circle itself.
        if ((q.modes & kSnapCentre) && (e.type == EntityType::Circle || e.type == EntityType::Arc ||
                                        e.type == EntityType::Polyline || e.type == EntityType::Ellipse ||
                                        e.type == EntityType::Array)) {
            pts.clear();
            Centres(e, pts);
            for (Vec2 c : pts) {
                best.Offer(kSnapCentre, c, id, Dist(c, q.cursor), 0, ap);
                if (curveNear) {
                    const double onCurve = DistanceTo(e, q.cursor);
                    if (onCurve <= ap) best.Offer(kSnapCentre, c, id, onCurve, ap * 0.5, ap);
                }
            }
        }
        if (!curveNear) continue;
        auto offerAll = [&](unsigned mode, void (*collect)(const Entity&, std::vector<Vec2>&)) {
            if (!(q.modes & mode)) return;
            pts.clear();
            collect(e, pts);
            for (Vec2 p : pts) best.Offer(mode, p, id, Dist(p, q.cursor), 0, ap);
        };
        offerAll(kSnapEndpoint, EndPoints);
        offerAll(kSnapMidpoint, MidPoints);
        offerAll(kSnapQuadrant, Quadrants);

        prims.clear();
        Primitives(e, prims);
        for (const Prim& p : prims)
            if (p.Bounds().Inflated(ap).Contains(q.cursor) && Distance(p, q.cursor) <= ap) near.push_back({p, id});
    }

    if ((q.modes & kSnapIntersection) && near.size() >= 2) {
        const size_t n = std::min<size_t>(near.size(), 200);
        for (size_t i = 0; i < n; ++i) {
            for (size_t j = i + 1; j < n; ++j) {
                pts.clear();
                Intersect(near[i].prim, near[j].prim, pts);
                for (Vec2 p : pts) {
                    // Joints of a polyline are end points, not intersections.
                    if (near[i].id == near[j].id &&
                        (Near(p, near[i].prim.a) || Near(p, near[i].prim.b)))
                        continue;
                    best.Offer(kSnapIntersection, p, near[i].id, Dist(p, q.cursor), 0, ap);
                }
            }
        }
    }
    for (const NearPrim& np : near) {
        if ((q.modes & kSnapPerpendicular) && q.hasBase) {
            Vec2 foot;
            if (PerpendicularFoot(np.prim, q.base, foot) && !Near(foot, q.base))
                best.Offer(kSnapPerpendicular, foot, np.id, Dist(foot, q.cursor), 0, ap);
        }
        if ((q.modes & kSnapTangent) && q.hasBase) {
            pts.clear();
            TangentPoints(np.prim, q.base, pts);
            for (Vec2 p : pts) best.Offer(kSnapTangent, p, np.id, Dist(p, q.cursor), 0, ap);
        }
        if (q.modes & kSnapNearest) {
            const Vec2 p = ClosestPoint(np.prim, q.cursor);
            best.Offer(kSnapNearest, p, np.id, Dist(p, q.cursor), ap, ap * 3);
        }
    }
    return best.result;
}
