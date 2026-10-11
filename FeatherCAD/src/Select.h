// Select.h - choosing objects by what they are: Quick Select ("all circles
// with radius 5") and Select Similar.
#pragma once
#include <string>

#include "Entity.h"

enum class QProp { Any, Length, Radius, Diameter, Area, CentreX, CentreY, Vertices, Closed };
enum class QOp { Equal, NotEqual, Greater, Less };

constexpr int kQPropCount = 9;
const wchar_t* QPropName(QProp p);

struct SelectQuery {
    int type = -1;  // an EntityType, or -1 for any object
    QProp prop = QProp::Any;
    QOp op = QOp::Equal;
    double value = 0;
};

// The value of a property, or false if the object does not have it
// (circles have no vertices, open shapes no area).
bool PropertyOf(const Entity& e, QProp prop, double& value);
bool Matches(const Entity& e, const SelectQuery& q);
// Area inside a closed shape.
bool AreaOf(const Entity& e, double& area);
// Select Similar: the same kind of object (arrays of the same kind).
bool SameKind(const Entity& a, const Entity& b);
