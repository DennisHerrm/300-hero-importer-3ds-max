// ATTRAPPE - siehe tools/sdkstub/README.md
//
// UtilityObj und IUtil. max.h zieht diesen Header NICHT mit -
// wer ihn vergisst, bekommt "Basisklasse undefiniert".
#ifndef EG3D_STUB_UTILAPI_H
#define EG3D_STUB_UTILAPI_H

#include "max.h"

class IUtil {
public:
    virtual ~IUtil() = default;
    virtual void CloseUtility() = 0;
};

class UtilityObj {
public:
    virtual ~UtilityObj() = default;
    virtual void BeginEditParams(Interface* ip, IUtil* iu) = 0;
    virtual void EndEditParams(Interface* ip, IUtil* iu) = 0;
    virtual void SelectionSetChanged(Interface*, IUtil*) {}
    virtual void DeleteThis() {}
};

#endif
