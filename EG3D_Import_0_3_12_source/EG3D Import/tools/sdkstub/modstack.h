// ATTRAPPE - siehe tools/sdkstub/README.md
#ifndef EG3D_STUB_MODSTACK_H
#define EG3D_STUB_MODSTACK_H

#include "max.h"

class IDerivedObject : public Object {
public:
    virtual void AddModifier(Modifier*, void* = nullptr, int = 0) {}
    virtual int NumModifiers() { return 0; }
    virtual Modifier* GetModifier(int) { return nullptr; }
};

inline IDerivedObject* CreateDerivedObject(Object*) { return nullptr; }

#endif  // EG3D_STUB_MODSTACK_H
