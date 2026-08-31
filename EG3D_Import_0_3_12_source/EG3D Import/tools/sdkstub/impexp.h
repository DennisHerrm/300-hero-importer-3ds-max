// ATTRAPPE - siehe tools/sdkstub/README.md
//
// SceneImport steht im echten SDK in impexp.h. Max 2016 zieht den
// Header nicht ueber max.h mit herein.
#ifndef EG3D_STUB_IMPEXP_H
#define EG3D_STUB_IMPEXP_H

#include "max.h"

class SceneImport {
public:
    virtual ~SceneImport() = default;
    virtual int ExtCount() = 0;
    virtual const MCHAR* Ext(int n) = 0;
    virtual const MCHAR* LongDesc() = 0;
    virtual const MCHAR* ShortDesc() = 0;
    virtual const MCHAR* AuthorName() = 0;
    virtual const MCHAR* CopyrightMessage() = 0;
    virtual const MCHAR* OtherMessage1() = 0;
    virtual const MCHAR* OtherMessage2() = 0;
    virtual unsigned int Version() = 0;
    virtual void ShowAbout(HWND hWnd) = 0;
    virtual int DoImport(const MCHAR* name, ImpInterface* ii, Interface* i,
                         BOOL suppressPrompts = FALSE) = 0;
};

#endif  // EG3D_STUB_IMPEXP_H
