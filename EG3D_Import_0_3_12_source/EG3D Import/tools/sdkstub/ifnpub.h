// ATTRAPPE - siehe tools/sdkstub/README.md
//
// Function Publishing: FPStaticInterface, Interface_ID, p_end,
// die FN_*-Makros. Im echten SDK zieht iparamb2.h diesen Header
// mit, aber ihn ausdruecklich einzubinden ist der sichere Weg.
#ifndef EG3D_STUB_IFNPUB_H
#define EG3D_STUB_IFNPUB_H

#include "max.h"

// ------------------------------------------------------------
//  Function Publishing
// ------------------------------------------------------------

// Interface_ID kommt im echten SDK aus iFnPub.h, das iparamb2.h
// mitzieht. Es ist bewusst ein eigener Typ und kein Class_ID -
// beide vertauscht zu haben faellt sonst erst zur Laufzeit auf.
class Interface_ID {
public:
    Interface_ID() = default;
    Interface_ID(unsigned long a, unsigned long b) : a_(a), b_(b) {}
    unsigned long PartA() const { return a_; }
    unsigned long PartB() const { return b_; }
    bool operator==(const Interface_ID& o) const { return a_ == o.a_ && b_ == o.b_; }
private:
    unsigned long a_ = 0;
    unsigned long b_ = 0;
};
enum ParamType2 {
    TYPE_VOID, TYPE_INT, TYPE_FLOAT, TYPE_BOOL, TYPE_STRING,
    TYPE_POINT3, TYPE_MATRIX3, TYPE_INODE, TYPE_VALUE, TYPE_FPVALUE
};

#define FP_CORE 0x0001
#define FP_STATIC_METHODS 0x0002
#define FP_MIXIN 0x0004

// Der Abschluss der Deskriptorliste heisst p_end, NICHT end.
// Der Stub hatte zuerst "end" - das uebersetzte hier und
// scheiterte im echten Build mit C2065. Genau die Sorte Fehler,
// gegen die der Stub eigentlich schuetzen soll; er kann es nur,
// wenn die Namen nachgeschlagen statt geraten sind.
enum { p_end = 0 };

class FPInterfaceDesc;

class FPInterface {
public:
    virtual ~FPInterface() = default;
};

class FPStaticInterface : public FPInterface {
public:
    FPStaticInterface() = default;
};

// Die Makros des echten SDK erzeugen eine Funktionstabelle. Fuer
// die Uebersetzungspruefung reicht es, dass sie syntaktisch
// aufgehen und die Methodennamen im Klassenkoerper stehen bleiben.
// Im echten SDK erzeugt DECLARE_DESCRIPTOR einen Konstruktor, der
// die Deskriptorliste entgegennimmt: Interface_ID, interner Name,
// String-Ressource, ClassDesc*, Flags und danach die
// Funktionsbeschreibungen bis "end". Die Attrappe muss dieselbe
// Aufrufform annehmen, sonst prueft sie die Aufrufstelle nicht.
#define DECLARE_DESCRIPTOR(cls) \
    template <typename... Args> \
    explicit cls(Interface_ID, const MCHAR*, int, ClassDesc*, unsigned long, \
                 Args&&...) {} \
    cls() = default; \
    FPInterfaceDesc* GetDesc() { return nullptr; }

#define BEGIN_FUNCTION_MAP
#define END_FUNCTION_MAP
#define FN_0(a, b, c)
#define FN_1(a, b, c, d)
#define FN_2(a, b, c, d, e)
#define FN_3(a, b, c, d, e, f)
#define FN_4(a, b, c, d, e, f, g)
#define FN_5(a, b, c, d, e, f, g, h)
#define VFN_0(a, b)
#define VFN_1(a, b, c)
#define VFN_2(a, b, c, d)


#endif  // EG3D_STUB_IFNPUB_H
