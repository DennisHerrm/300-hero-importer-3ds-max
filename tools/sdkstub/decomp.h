// ATTRAPPE - siehe tools/sdkstub/README.md
#ifndef EG3D_STUB_DECOMP_H
#define EG3D_STUB_DECOMP_H
#include "max.h"
struct AffineParts {
    Point3 t;
    Quat q;
    Quat u;
    Point3 k;
    float f = 1.0f;
};
inline void decomp_affine(const Matrix3&, AffineParts* parts) { (void)parts; }
#endif
