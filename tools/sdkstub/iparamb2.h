// ATTRAPPE - siehe tools/sdkstub/README.md
//
// iparamb2.h traegt im echten SDK ClassDesc2 und das
// Function-Publishing. Max 2016 zieht den Header NICHT ueber
// max.h mit herein; die spaeteren Jahrgaenge schon. Deshalb bindet
// der Quelltext ihn ausdruecklich ein, und deshalb steht er hier.
#ifndef EG3D_STUB_IPARAMB2_H
#define EG3D_STUB_IPARAMB2_H

#include "max.h"
#include "ifnpub.h"

class ClassDesc {
public:
    virtual ~ClassDesc() = default;
    virtual int IsPublic() = 0;
    virtual void* Create(BOOL loading = FALSE) = 0;
    virtual const MCHAR* ClassName() = 0;
    virtual SClass_ID SuperClassID() = 0;
    virtual Class_ID ClassID() = 0;
    virtual const MCHAR* Category() = 0;
    virtual const MCHAR* InternalName() { return nullptr; }
    virtual HINSTANCE HInstance() { return nullptr; }
    virtual int NumParamBlockDescs() { return 0; }
};

class ClassDesc2 : public ClassDesc {
public:
#if MAX_RELEASE >= 24000
    // NonLocalizedClassName gibt es erst ab Max 2022 (MAX_RELEASE
    // 24000). Der Stub bildet genau diese Grenze nach, damit ein
    // falsch platziertes override in beiden Zweigen auffaellt.
    virtual const MCHAR* NonLocalizedClassName() { return ClassName(); }
#endif
};

#endif  // EG3D_STUB_IPARAMB2_H
