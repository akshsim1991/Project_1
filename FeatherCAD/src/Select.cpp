// Select.cpp - see Select.h.
#include "Select.h"

#include "Annot.h"
#include "Curve.h"

const wchar_t* QPropName(QProp p) {
    switch (p) {
        case QProp::Any: return L"(any: select all of the type)";
        case QProp::Length: return L"Length";
        case QProp::Radius: return L"Radius";
        case QProp::Diameter: return L"Diameter";
        case QProp::Area: return L"Area";
        case QProp::CentreX: return L"Centre X";
        case QProp::CentreY: return L"Centre Y";
        case QProp::Vertices: return L"Number of vertices";
        case QProp::Closed: return L"Closed (1 = yes, 0 = no)";
    }
    return L"";
}

bool AreaOf(const Entity& e, double& area) {
    switch (e.type) {
        case EntityType::Circle:
            area = kPi * e.radius * e.radius;
            return true;
        case EntityType::Ellipse:
            if (!e.closed) return false;
            area = kPi * Length(e.p1) * Length(e.p1) * e.radius;
            return true;
        case EntityType::Polyline: {
            if (!e.closed || e.verts.size() < 3) return false;
            // Shoelace for the straight outline, plus the circle segment of each arc.
            double a = 0;
            const size_t n = e.verts.size();
            for (size_t i = 0; i < n; ++i) {
                const Vertex& v = e.verts[i];
                const Vertex& w = e.verts[(i + 1) % n];
                a += Cross(v.p, w.p) / 2;
                if (v.bulge != 0) {
                    const Prim p = ArcFromBulge(v.p, w.p, v.bulge);
                    const double theta = 4 * std::atan(v.bulge);
                    // A positive bulge lies to the right of travel: outside a
                    // counter-clockwise outline, so its circle segment adds.
                    a += p.r * p.r / 2 * (theta - std::sin(theta));
                }
            }
            area = std::fabs(a);
            return true;
        }
        case EntityType::Spline: {
            if (!e.closed || SplineTrimmed(e)) return false;
            std::vector<Vec2> pts;
            SplinePoints(e, pts);
            double a = 0;
            for (size_t i = 0; i + 1 < pts.size(); ++i) a += Cross(pts[i], pts[i + 1]) / 2;
            area = std::fabs(a);
            return true;
        }
        default:
            return false;
    }
}

bool PropertyOf(const Entity& e, QProp prop, double& value) {
    switch (prop) {
        case QProp::Any:
            value = 0;
            return true;
        case QProp::Length:
            if (e.type == EntityType::Point || e.type == EntityType::Array || IsAnnotation(e) || e.type == EntityType::Insert)
                return false;
            value = IsCurve(e) ? CurveLength(e) : LengthOf(e);
            return true;
        case QProp::Radius:
        case QProp::Diameter:
            if (e.type != EntityType::Circle && e.type != EntityType::Arc) return false;
            value = prop == QProp::Radius ? e.radius : 2 * e.radius;
            return true;
        case QProp::Area:
            return AreaOf(e, value);
        case QProp::CentreX:
        case QProp::CentreY: {
            Vec2 c;
            if (e.type == EntityType::Circle || e.type == EntityType::Arc || e.type == EntityType::Ellipse ||
                e.type == EntityType::Point)
                c = e.p0;
            else
                c = Bounds(e).Center();
            value = prop == QProp::CentreX ? c.x : c.y;
            return true;
        }
        case QProp::Vertices:
            if (e.type != EntityType::Polyline && e.type != EntityType::Spline) return false;
            value = (double)e.verts.size();
            return true;
        case QProp::Closed:
            if (e.type == EntityType::Circle) value = 1;
            else if (e.type == EntityType::Polyline || e.type == EntityType::Spline || e.type == EntityType::Ellipse)
                value = e.closed ? 1 : 0;
            else return false;
            return true;
    }
    return false;
}

bool Matches(const Entity& e, const SelectQuery& q) {
    if (q.type >= 0 && (int)e.type != q.type) return false;
    if (q.prop == QProp::Any) return true;
    double v;
    if (!PropertyOf(e, q.prop, v)) return false;
    const double tol = 1e-6 * std::max(1.0, std::fabs(q.value));
    switch (q.op) {
        case QOp::Equal: return std::fabs(v - q.value) <= tol;
        case QOp::NotEqual: return std::fabs(v - q.value) > tol;
        case QOp::Greater: return v > q.value + tol;
        case QOp::Less: return v < q.value - tol;
    }
    return false;
}

bool SameKind(const Entity& a, const Entity& b) {
    if (a.type != b.type) return false;
    if (a.type == EntityType::Array && a.array && b.array) return a.array->kind == b.array->kind;
    return true;
}
