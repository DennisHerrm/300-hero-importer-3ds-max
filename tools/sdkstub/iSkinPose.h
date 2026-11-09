// ATTRAPPE - siehe tools/sdkstub/README.md
//
// ISkinPose steht im echten SDK in iSkinPose.h (2016-2027). Nachgebildet
// ist nur, was eg3dimport.cpp aufruft: GetISkinPose, die drei Enable-
// Schalter und SetSkinPose.
#ifndef EG3D_STUB_ISKINPOSE_H
#define EG3D_STUB_ISKINPOSE_H
#include "max.h"


class ISkinPose {
public:
    static ISkinPose* GetISkinPose(INode& n) {
        return static_cast<ISkinPose*>(n.GetInterface(0));
    }
    virtual ~ISkinPose() {}
    virtual void EnableSkinPos(bool) = 0;
    virtual void EnableSkinRot(bool) = 0;
    virtual void EnableSkinScale(bool) = 0;
    virtual void SetSkinPose(TimeValue) = 0;
    virtual void AssumeSkinPose(TimeValue) = 0;
    virtual void SetSkinPoseMode(bool) = 0;
};

#endif
